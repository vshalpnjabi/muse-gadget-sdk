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

#include "muse_settings.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "muse_link.h"

static const char *TAG = "muse_settings";

#define NS "muse"
#define DEFAULT_HOST "hatch.metaaivm.com"

static struct {
    uint8_t volume;
    bool speaker_on;
    uint8_t mic_gain;
    uint8_t brightness;
    uint16_t sleep_s;
    uint8_t text_size;
    bool wifi_on;
    bool ble_on;
    char ssid[MUSE_SSID_MAX + 1];
    char pass[MUSE_PASS_MAX + 1];
    char host[MUSE_HOST_MAX + 1];
    char vm[MUSE_VM_MAX + 1];
    char token[MUSE_TOKEN_MAX + 1];
} s = {
    .volume = CONFIG_MUSE_DEFAULT_VOLUME,
    .speaker_on = true,
    .mic_gain = 30,
    .brightness = 100,
    .sleep_s = 120,
    .text_size = MUSE_TEXT_MEDIUM,
    .wifi_on = true,
    .host = DEFAULT_HOST,
};

static SemaphoreHandle_t s_lock;
/* StopWatch samples this at the next press, never during an active call. */
static atomic_bool s_voice_notes;
/* The UI reads it each frame. The photo is the default where it's built in. */
#if MUSE_HAS_PHOTO_AVATAR
static atomic_int s_avatar = MUSE_AVATAR_PHOTO;
#else
static atomic_int s_avatar = MUSE_AVATAR_PIXEL;
#endif
/* Kconfig always sets these; the fallbacks are for host harnesses with a partial sdkconfig.h. */
#ifndef CONFIG_MUSE_DEFAULT_THEME
#define CONFIG_MUSE_DEFAULT_THEME 1
#endif
#ifndef CONFIG_MUSE_TIMEZONE
#define CONFIG_MUSE_TIMEZONE "UTC0"
#endif
static atomic_bool s_captions = true;
static atomic_bool s_dnd;
static atomic_int s_theme = CONFIG_MUSE_DEFAULT_THEME;
static atomic_int s_sleep_from = -1, s_sleep_to = -1;
static char s_tz[MUSE_TZ_MAX + 1] = CONFIG_MUSE_TIMEZONE;   /* under s_lock */
static nvs_handle_t s_nvs;
static muse_setting_cb_t s_listener;

#define LOCKED(body) do { xSemaphoreTake(s_lock, portMAX_DELAY); body; xSemaphoreGive(s_lock); } while (0)

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Missing keys leave the default in place. */
static void load_str(const char *key, char *out, size_t len)
{
    size_t n = len;
    nvs_get_str(s_nvs, key, out, &n);
}

static void load_u8(const char *key, uint8_t *out)
{
    nvs_get_u8(s_nvs, key, out);
}

static void save_u8(const char *key, uint8_t v)
{
    nvs_set_u8(s_nvs, key, v);
    nvs_commit(s_nvs);
}

static void save_str(const char *key, const char *v)
{
    nvs_set_str(s_nvs, key, v);
    nvs_commit(s_nvs);
}

static void notify(muse_setting_t what)
{
    if (s_listener) {
        s_listener(what);
    }
}

esp_err_t muse_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS layout changed, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    s_lock = xSemaphoreCreateMutex();
    err = nvs_open(NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t b;
    load_u8("volume", &s.volume);
    if (nvs_get_u8(s_nvs, "speaker", &b) == ESP_OK) {
        s.speaker_on = b;
    }
    load_u8("mic_gain", &s.mic_gain);
#if CONFIG_MUSE_PTT_CALLS
    if (nvs_get_u8(s_nvs, "voice_notes", &b) == ESP_OK) {
        atomic_store(&s_voice_notes, b != 0);
    }
#endif
    if (nvs_get_u8(s_nvs, "captions", &b) == ESP_OK) {
        atomic_store(&s_captions, b != 0);
    }
    if (nvs_get_u8(s_nvs, "theme", &b) == ESP_OK) {
        atomic_store(&s_theme, b);
    }
    if (nvs_get_u8(s_nvs, "dnd", &b) == ESP_OK) {
        atomic_store(&s_dnd, b != 0);
    }
    if (nvs_get_u8(s_nvs, "sl_from", &b) == ESP_OK) {
        atomic_store(&s_sleep_from, b < 24 ? b : -1);
    }
    if (nvs_get_u8(s_nvs, "sl_to", &b) == ESP_OK) {
        atomic_store(&s_sleep_to, b < 24 ? b : -1);
    }
    size_t tz_len = sizeof(s_tz);
    if (nvs_get_str(s_nvs, "tz", s_tz, &tz_len) != ESP_OK) {
        strlcpy(s_tz, CONFIG_MUSE_TIMEZONE, sizeof(s_tz));
    }
#if MUSE_HAS_PHOTO_AVATAR
    if (nvs_get_u8(s_nvs, "avatar", &b) == ESP_OK && b < MUSE_AVATAR_COUNT) {
        atomic_store(&s_avatar, b);
    }
#endif
    load_u8("bright", &s.brightness);
    nvs_get_u16(s_nvs, "sleep_s", &s.sleep_s);
    load_u8("text_size", &s.text_size);
    if (nvs_get_u8(s_nvs, "wifi_on", &b) == ESP_OK) {
        s.wifi_on = b;
    }
    if (nvs_get_u8(s_nvs, "ble_on", &b) == ESP_OK) {
        s.ble_on = b;
    }
    load_str("ssid", s.ssid, sizeof(s.ssid));
    load_str("pass", s.pass, sizeof(s.pass));
    load_str("host", s.host, sizeof(s.host));
    load_str("vm", s.vm, sizeof(s.vm));
    load_str("token", s.token, sizeof(s.token));

    s.volume = clampi(s.volume, 0, 100);
    s.mic_gain = clampi(s.mic_gain, 0, MUSE_MIC_GAIN_MAX);
    s.brightness = clampi(s.brightness, 10, 100);
    s.text_size = clampi(s.text_size, MUSE_TEXT_SMALL, MUSE_TEXT_LARGE);
    ESP_LOGI(TAG, "vol %d%s, mic %d dB, bright %d, sleep %ds, wifi %s (%s), ble %s, muse %s",
             s.volume, s.speaker_on ? "" : " (speaker off)", s.mic_gain, s.brightness, s.sleep_s, s.wifi_on ? "on" : "off",
             "network saved by Link", s.ble_on ? "on" : "off", s.token[0] ? "token set" : "no token");
    return ESP_OK;
}

void muse_settings_set_listener(muse_setting_cb_t cb)
{
    s_listener = cb;
}

int muse_settings_volume(void) { return s.volume; }
bool muse_settings_speaker_on(void) { return s.speaker_on; }
int muse_settings_mic_gain(void) { return s.mic_gain; }
int muse_settings_brightness(void) { return s.brightness; }
int muse_settings_sleep_s(void) { return s.sleep_s; }
int muse_settings_text_size(void) { return s.text_size; }
bool muse_settings_wifi_on(void) { return s.wifi_on; }
bool muse_settings_ble_on(void) { return s.ble_on; }
bool muse_settings_voice_notes(void) { return atomic_load(&s_voice_notes); }

void muse_settings_set_voice_notes(bool on)
{
    atomic_store(&s_voice_notes, on);
    save_u8("voice_notes", on);
    notify(MUSE_SETTING_VOICE_NOTES);
}

bool muse_settings_captions(void) { return atomic_load(&s_captions); }

int muse_settings_theme(void) { return atomic_load(&s_theme); }

void muse_settings_set_theme(int theme)
{
    theme = clampi(theme, 0, 254);
    atomic_store(&s_theme, theme);
    save_u8("theme", (uint8_t)theme);
}

bool muse_settings_dnd(void) { return atomic_load(&s_dnd); }

void muse_settings_set_dnd(bool on)
{
    atomic_store(&s_dnd, on);
    save_u8("dnd", on);
    notify(MUSE_SETTING_QUIET);
}

int muse_settings_sleep_from(void) { return atomic_load(&s_sleep_from); }
int muse_settings_sleep_to(void) { return atomic_load(&s_sleep_to); }

void muse_settings_set_sleep_hours(int from, int to)
{
    if (from < 0 || from > 23 || to < 0 || to > 23 || from == to) {
        from = to = -1;   /* none */
    }
    atomic_store(&s_sleep_from, from);
    atomic_store(&s_sleep_to, to);
    save_u8("sl_from", (uint8_t)(from < 0 ? 255 : from));
    save_u8("sl_to", (uint8_t)(to < 0 ? 255 : to));
    notify(MUSE_SETTING_QUIET);
}

void muse_settings_tz(char out[MUSE_TZ_MAX + 1])
{
    LOCKED(strlcpy(out, s_tz, MUSE_TZ_MAX + 1));
}

void muse_settings_set_tz(const char *tz)
{
    LOCKED(strlcpy(s_tz, tz && tz[0] ? tz : CONFIG_MUSE_TIMEZONE, sizeof(s_tz)));
    nvs_set_str(s_nvs, "tz", s_tz);
    nvs_commit(s_nvs);
    notify(MUSE_SETTING_QUIET);
}

void muse_settings_set_captions(bool on)
{
    atomic_store(&s_captions, on);
    save_u8("captions", on);
    notify(MUSE_SETTING_CAPTIONS);
}

int muse_settings_avatar(void) { return atomic_load(&s_avatar); }

void muse_settings_set_avatar(int which)
{
#if !MUSE_HAS_PHOTO_AVATAR
    which = MUSE_AVATAR_PIXEL;   /* nothing else is built in */
#endif
    which = clampi(which, 0, MUSE_AVATAR_COUNT - 1);
    atomic_store(&s_avatar, which);
    save_u8("avatar", (uint8_t)which);
    notify(MUSE_SETTING_AVATAR);
}

/* Home Link owns the saved networks (this is the first); the local copy is only a fallback. */
void muse_settings_wifi(char ssid[MUSE_SSID_MAX + 1], char pass[MUSE_PASS_MAX + 1])
{
    if (muse_link_wifi_get(ssid, pass)) {
        return;
    }
    LOCKED({
        strlcpy(ssid, s.ssid, MUSE_SSID_MAX + 1);
        if (pass) {
            strlcpy(pass, s.pass, MUSE_PASS_MAX + 1);
        }
    });
}

void muse_settings_hatch_host(char out[MUSE_HOST_MAX + 1])
{
    LOCKED(strlcpy(out, s.host, MUSE_HOST_MAX + 1));
}

void muse_settings_hatch_vm(char out[MUSE_VM_MAX + 1])
{
    LOCKED(strlcpy(out, s.vm, MUSE_VM_MAX + 1));
}

void muse_settings_hatch_token(char out[MUSE_TOKEN_MAX + 1])
{
    LOCKED(strlcpy(out, s.token, MUSE_TOKEN_MAX + 1));
}

size_t muse_settings_hatch_token_len(void)
{
    size_t n;
    LOCKED(n = strlen(s.token));
    return n;
}

void muse_settings_set_volume(int pct)
{
    s.volume = clampi(pct, 0, 100);
    save_u8("volume", s.volume);
    notify(MUSE_SETTING_VOLUME);
}

void muse_settings_set_speaker_on(bool on)
{
    s.speaker_on = on;
    save_u8("speaker", on);
    notify(MUSE_SETTING_SPEAKER);
}

void muse_settings_set_mic_gain(int db)
{
    s.mic_gain = clampi(db, 0, MUSE_MIC_GAIN_MAX);
    save_u8("mic_gain", s.mic_gain);
    notify(MUSE_SETTING_MIC_GAIN);
}

void muse_settings_set_brightness(int pct)
{
    s.brightness = clampi(pct, 10, 100);
    save_u8("bright", s.brightness);
    notify(MUSE_SETTING_BRIGHTNESS);
}

void muse_settings_set_sleep_s(int secs)
{
    s.sleep_s = clampi(secs, 0, 3600);
    nvs_set_u16(s_nvs, "sleep_s", s.sleep_s);
    nvs_commit(s_nvs);
    notify(MUSE_SETTING_SLEEP);
}

void muse_settings_set_text_size(int size)
{
    s.text_size = clampi(size, MUSE_TEXT_SMALL, MUSE_TEXT_LARGE);
    save_u8("text_size", s.text_size);   /* the UI polls it */
}

void muse_settings_set_wifi_on(bool on)
{
    s.wifi_on = on;
    save_u8("wifi_on", on);
    notify(MUSE_SETTING_WIFI);
}

void muse_settings_set_ble_on(bool on)
{
    s.ble_on = on;
    save_u8("ble_on", on);
    notify(MUSE_SETTING_BLE);
}

void muse_settings_set_wifi(const char *ssid, const char *pass)
{
    if (muse_link_wifi_set(ssid ? ssid : "", ssid && ssid[0] && pass ? pass : "")) {
        ESP_LOGI(TAG, "wifi network: %s", ssid && ssid[0] ? ssid : "(all forgotten)");
        notify(MUSE_SETTING_WIFI);
        return;
    }
    LOCKED({
        strlcpy(s.ssid, ssid ? ssid : "", sizeof(s.ssid));
        strlcpy(s.pass, s.ssid[0] && pass ? pass : "", sizeof(s.pass));
        save_str("ssid", s.ssid);
        save_str("pass", s.pass);
    });
    ESP_LOGI(TAG, "wifi network: %s", s.ssid[0] ? s.ssid : "(forgotten)");
    notify(MUSE_SETTING_WIFI);
}

void muse_settings_set_hatch_host(const char *host)
{
    LOCKED({
        strlcpy(s.host, host && host[0] ? host : DEFAULT_HOST, sizeof(s.host));
        save_str("host", s.host);
    });
    notify(MUSE_SETTING_HATCH);
}

void muse_settings_set_hatch_vm(const char *vm)
{
    LOCKED({
        strlcpy(s.vm, vm ? vm : "", sizeof(s.vm));
        save_str("vm", s.vm);
    });
    notify(MUSE_SETTING_HATCH);
}

esp_err_t muse_settings_set_hatch_token(const char *token, bool append)
{
    esp_err_t err = ESP_OK;
    LOCKED({
        size_t have = append ? strlen(s.token) : 0;
        size_t add = strlen(token ? token : "");
        if (have + add > MUSE_TOKEN_MAX) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            memcpy(s.token + have, token, add);
            s.token[have + add] = '\0';
            save_str("token", s.token);
        }
    });
    if (err == ESP_OK) {
        notify(MUSE_SETTING_HATCH);
    }
    return err;
}
