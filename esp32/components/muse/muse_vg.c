/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Voice guard in shadow mode (muse_vg.h). The tee only copies: if the worker
 * holds the ring at that moment, the piece is left out of the copy rather
 * than the voice task waiting, so the Muse audio is never delayed. The worker
 * posts the last 3 s every 1.5 s while a call or voice note is on and polls
 * each window's verdict every 0.5 s (up to 10 s), home base first, the HTTPS
 * one (CA-bundle validated) if home fails. The verdict is logged and shown;
 * nothing acts on it.
 */
#include "muse_vg.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "muse_state.h"
#include "muse_voice.h"
#include "muse_wifi.h"

static const char *TAG = "muse_vg";

#define RATE 16000
#define RING_SAMPLES (RATE * 4)        /* the last ~4 s */
#define WINDOW_SAMPLES (RATE * 3)      /* what each window sends */
#define POST_EVERY_US 1500000LL
#define POLL_EVERY_US 500000LL
#define VERDICT_WAIT_US 10000000LL
#define HOME_TIMEOUT_MS 1500
#define AWAY_TIMEOUT_MS 6000
#define HOME_RETRY_US 60000000LL       /* home failed: the HTTPS base for this long */
#define PENDING_MAX 8
#define RESP_MAX 512
/* Only windows with the caller's speech are scored, so quiet isn't a red. */
#define SLICE 480                       /* 30 ms */
#define SPEECH_OVER_FLOOR_DB 12.0f      /* a slice this far over the window's quiet is speech */
#define SPEECH_MIN_DB -50.0f            /* and at least this loud */
#define SPEECH_MIN_MS 1200              /* of the window's 3 s */
#define MUSE_ECHO_US 3000000LL          /* Muse spoke within this: its voice is in the window */

/* The ring: written by the voice task, read by the worker. */
static int16_t *s_ring;
static size_t s_head;                  /* next write */
static volatile uint32_t s_written;    /* samples ever written (wrapping) */
static SemaphoreHandle_t s_ring_lock;

/* The latest verdict, for the face. */
static atomic_bool s_running, s_have, s_accept;
static _Atomic float s_score;

typedef struct {
    bool on;
    int base;                          /* 0 home, 1 away */
    char id[48];
    int64_t posted_us, next_poll_us;
    int speech_ms;
} pending_t;
static pending_t s_pending[PENDING_MAX];

static esp_http_client_handle_t s_cli[2];
static char s_resp[RESP_MAX];
static size_t s_resp_len;
static int64_t s_home_down_until;
static int64_t s_muse_spoke_us = -MUSE_ECHO_US;   /* last seen speaking */

void muse_vg_tee(const int16_t *pcm, size_t frames)
{
    if (!s_ring || !frames || xSemaphoreTake(s_ring_lock, 0) != pdTRUE) {
        return;   /* never wait: the Muse audio goes on regardless */
    }
    while (frames) {
        size_t n = RING_SAMPLES - s_head < frames ? RING_SAMPLES - s_head : frames;
        memcpy(s_ring + s_head, pcm, n * sizeof(int16_t));
        s_head = (s_head + n) % RING_SAMPLES;
        s_written += n;
        pcm += n;
        frames -= n;
    }
    xSemaphoreGive(s_ring_lock);
}

bool muse_vg_running(void)
{
    return atomic_load(&s_running);
}

bool muse_vg_latest(float *score, bool *accept)
{
    if (!atomic_load(&s_have)) {
        return false;
    }
    *score = atomic_load(&s_score);
    *accept = atomic_load(&s_accept);
    return true;
}

static esp_err_t on_http(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA && e->data_len > 0) {
        size_t room = RESP_MAX - 1 - s_resp_len;
        size_t n = (size_t)e->data_len < room ? (size_t)e->data_len : room;
        memcpy(s_resp + s_resp_len, e->data, n);
        s_resp_len += n;
        s_resp[s_resp_len] = '\0';
    }
    return ESP_OK;
}

static const char *base_url(int base)
{
    return base ? CONFIG_MUSE_VG_AWAY_BASE : CONFIG_MUSE_VG_HOME_BASE;
}

/* One request on base's kept-alive client; the HTTP status, or -1. The reply is in s_resp. */
static int request(int base, bool post, const char *path, const void *body, int len)
{
    char url[256];
    snprintf(url, sizeof(url), "%s%s", base_url(base), path);
    if (!s_cli[base]) {
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = base ? AWAY_TIMEOUT_MS : HOME_TIMEOUT_MS,
            .event_handler = on_http,
            .buffer_size = 1024,
            .buffer_size_tx = 1024,
            .keep_alive_enable = true,
            .crt_bundle_attach = esp_crt_bundle_attach,   /* full validation for the HTTPS base */
            .disable_auto_redirect = true,
        };
        s_cli[base] = esp_http_client_init(&cfg);
        if (!s_cli[base]) {
            return -1;
        }
    }
    esp_http_client_handle_t c = s_cli[base];
    esp_http_client_set_url(c, url);
    esp_http_client_set_method(c, post ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    esp_http_client_set_header(c, "X-VG-Token", CONFIG_MUSE_VG_TOKEN);
    if (post) {
        esp_http_client_set_header(c, "Content-Type", "application/octet-stream");
        esp_http_client_set_post_field(c, body, len);
    } else {
        esp_http_client_delete_header(c, "Content-Type");
        esp_http_client_set_post_field(c, NULL, 0);
    }
    s_resp_len = 0;
    s_resp[0] = '\0';
    esp_err_t err = esp_http_client_perform(c);
    if (err != ESP_OK) {
        /* A broken kept-alive connection: start that base afresh next time. */
        esp_http_client_cleanup(c);
        s_cli[base] = NULL;
        ESP_LOGW(TAG, "%s %s: %s", post ? "POST" : "GET", base ? "away" : "home", esp_err_to_name(err));
        return -1;
    }
    return esp_http_client_get_status_code(c);
}

/* The id from {"id": "..."} (or a number); false without one. */
static bool parse_id(char *out, size_t cap)
{
    cJSON *root = cJSON_Parse(s_resp);
    const cJSON *id = root ? cJSON_GetObjectItemCaseSensitive(root, "id") : NULL;
    bool ok = false;
    if (cJSON_IsString(id) && id->valuestring[0]) {
        strlcpy(out, id->valuestring, cap);
        ok = true;
    } else if (cJSON_IsNumber(id)) {
        snprintf(out, cap, "%.0f", id->valuedouble);
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

/* The verdict once the JSON has "accept"; false while it's still pending. */
static bool parse_verdict(float *score, bool *accept)
{
    cJSON *root = cJSON_Parse(s_resp);
    const cJSON *a = root ? cJSON_GetObjectItemCaseSensitive(root, "accept") : NULL;
    bool done = false;
    if (cJSON_IsBool(a) || cJSON_IsNumber(a)) {
        *accept = cJSON_IsBool(a) ? cJSON_IsTrue(a) : a->valuedouble != 0;
        const cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "score");
        *score = cJSON_IsNumber(s) ? (float)s->valuedouble : 0.0f;
        done = true;
    }
    cJSON_Delete(root);
    return done;
}

/* Posts the last 3 s; on the home base unless it failed lately, then the HTTPS one. */
/* How much of the window is speech, in ms, and its loud end in dBFS: slices
 * well over the window's own quiet (its 20th percentile) and over a floor. */
static int speech_ms(const int16_t *win, float *peak_db)
{
    enum { N = WINDOW_SAMPLES / SLICE };
    static float db[N], sorted[N];
    for (int i = 0; i < N; i++) {
        double acc = 0;
        for (int j = 0; j < SLICE; j++) {
            double x = win[i * SLICE + j];
            acc += x * x;
        }
        db[i] = 10.0f * log10f((float)(acc / SLICE / (32768.0 * 32768.0)) + 1e-10f);
        sorted[i] = db[i];
    }
    for (int i = 1; i < N; i++) {   /* insertion sort: 100 values */
        float v = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > v) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = v;
    }
    float floor_db = sorted[N / 5];
    float gate = floor_db + SPEECH_OVER_FLOOR_DB > SPEECH_MIN_DB ? floor_db + SPEECH_OVER_FLOOR_DB : SPEECH_MIN_DB;
    int voiced = 0;
    for (int i = 0; i < N; i++) {
        voiced += db[i] > gate;
    }
    *peak_db = sorted[N - 1];
    return voiced * SLICE * 1000 / RATE;
}

static void post_window(int16_t *win, int64_t now)
{
    if (xSemaphoreTake(s_ring_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    size_t start = (s_head + RING_SAMPLES - WINDOW_SAMPLES) % RING_SAMPLES;
    size_t first = RING_SAMPLES - start < WINDOW_SAMPLES ? RING_SAMPLES - start : WINDOW_SAMPLES;
    memcpy(win, s_ring + start, first * sizeof(int16_t));
    memcpy(win + first, s_ring, (WINDOW_SAMPLES - first) * sizeof(int16_t));
    xSemaphoreGive(s_ring_lock);

    /* Not the caller speaking: grey, not sent. */
    float peak_db;
    int ms = speech_ms(win, &peak_db);
    const char *skip = now - s_muse_spoke_us < MUSE_ECHO_US ? "muse speaking" : ms < SPEECH_MIN_MS ? "quiet" : NULL;
    if (skip) {
        ESP_LOGI(TAG, "VG window skipped (%s) speech_ms=%d peak=%.0fdB", skip, ms, peak_db);
        atomic_store(&s_have, false);
        return;
    }

    static char path[96];
    snprintf(path, sizeof(path), "/window?speaker=%s&format=pcm16", CONFIG_MUSE_VG_SPEAKER);
    int base = now < s_home_down_until ? 1 : 0;
    int status = request(base, true, path, win, WINDOW_SAMPLES * sizeof(int16_t));
    if (base == 0 && (status < 200 || status >= 300)) {
        s_home_down_until = now + HOME_RETRY_US;   /* home errored or timed out: the HTTPS base */
        base = 1;
        status = request(base, true, path, win, WINDOW_SAMPLES * sizeof(int16_t));
    }
    char id[48];
    if (status < 200 || status >= 300 || !parse_id(id, sizeof(id))) {
        ESP_LOGW(TAG, "VG window post failed (%s, HTTP %d)", base ? "away" : "home", status);
        return;
    }
    for (int i = 0; i < PENDING_MAX; i++) {
        if (!s_pending[i].on) {
            s_pending[i] = (pending_t){ .on = true, .base = base, .posted_us = now, .next_poll_us = now + POLL_EVERY_US,
                                        .speech_ms = ms };
            strlcpy(s_pending[i].id, id, sizeof(s_pending[i].id));
            return;
        }
    }
    ESP_LOGW(TAG, "VG window id=%s dropped: too many waiting", id);
}

static void poll_verdicts(int64_t now)
{
    for (int i = 0; i < PENDING_MAX; i++) {
        pending_t *p = &s_pending[i];
        if (!p->on || now < p->next_poll_us) {
            continue;
        }
        if (now - p->posted_us > VERDICT_WAIT_US) {
            ESP_LOGI(TAG, "VG window id=%s no verdict after %d ms", p->id, (int)((now - p->posted_us) / 1000));
            atomic_store(&s_have, false);   /* grey: no verdict came */
            p->on = false;
            continue;
        }
        char path[80];
        snprintf(path, sizeof(path), "/verdict/%s", p->id);
        int status = request(p->base, false, path, NULL, 0);
        float score = 0;
        bool accept = false;
        if (status >= 200 && status < 300 && parse_verdict(&score, &accept)) {
            int rtt = (int)((esp_timer_get_time() - p->posted_us) / 1000);
            ESP_LOGI(TAG, "VG window id=%s score=%.3f accept=%d rtt_ms=%d speech_ms=%d", p->id, score, accept, rtt,
                     p->speech_ms);
            atomic_store(&s_score, score);
            atomic_store(&s_accept, accept);
            atomic_store(&s_have, true);
            p->on = false;
        } else {
            p->next_poll_us = now + POLL_EVERY_US;
        }
    }
}

static bool active(void)
{
    /* A call, or a voice note being recorded: what goes to Muse. */
    return muse_voice_in_call() || muse_state_mode(NULL) == MUSE_MODE_LISTENING;
}

static void vg_task(void *arg)
{
    (void)arg;
    int16_t *win = heap_caps_malloc(WINDOW_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!win) {
        ESP_LOGW(TAG, "no memory for a window");
        vTaskDelete(NULL);
        return;
    }
    int64_t next_post = 0;
    uint32_t posted_at = 0;
    bool was_active = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now = esp_timer_get_time();
        poll_verdicts(now);
        bool on = active();
        if (muse_voice_call_view() == MUSE_VOICE_VIEW_SPEAKING) {
            s_muse_spoke_us = now;
        }
        if (on && !was_active) {
            /* A new session: the ring still holds the last one's audio, so the
             * first window waits for 3 s of this one's. */
            posted_at = s_written + (WINDOW_SAMPLES - RATE * 3 / 2);
        }
        was_active = on;
        if (!on || !muse_wifi_connected() || now < next_post) {
            continue;
        }
        /* Only once 1.5 s of new audio has come in since the last window. */
        if ((int32_t)(s_written - posted_at) < RATE * 3 / 2) {
            continue;
        }
        posted_at = s_written;
        next_post = now + POST_EVERY_US;
        post_window(win, now);
    }
}

void muse_vg_start(void)
{
    if (!CONFIG_MUSE_VG_TOKEN[0]) {
        ESP_LOGW(TAG, "voice guard on, but no token is set (menuconfig): nothing is sent");
        return;
    }
    /* The lock first: the tee only looks at the ring once it's published. */
    int16_t *ring = heap_caps_calloc(RING_SAMPLES, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_ring_lock = xSemaphoreCreateMutex();
    if (!ring || !s_ring_lock) {
        ESP_LOGW(TAG, "voice guard couldn't start");
        return;
    }
    s_ring = ring;
    if (xTaskCreatePinnedToCoreWithCaps(vg_task, "muse_vg", 8192, NULL, 2, NULL, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGW(TAG, "voice guard couldn't start");
        return;
    }
    atomic_store(&s_running, true);
    ESP_LOGI(TAG, "voice guard (shadow mode): speaker %s, home %s, away %s", CONFIG_MUSE_VG_SPEAKER,
             CONFIG_MUSE_VG_HOME_BASE, CONFIG_MUSE_VG_AWAY_BASE);
}
