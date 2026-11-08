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

#include "muse_clock.h"

#include <stdlib.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

#include "muse_settings.h"
#include "muse_wifi.h"

static const char *TAG = "muse_clock";

void muse_clock_apply_tz(void)
{
    char tz[MUSE_TZ_MAX + 1];
    muse_settings_tz(tz);
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "time zone %s", tz);
}

static void on_sync(struct timeval *tv)
{
    (void)tv;
    struct tm now;
    if (muse_clock_now(&now)) {
        ESP_LOGI(TAG, "time set: %04d-%02d-%02d %02d:%02d", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                 now.tm_hour, now.tm_min);
    }
}

void muse_clock_start(void)
{
    muse_clock_apply_tz();
}

void muse_clock_tick(void)
{
    static bool started;
    if (started || !muse_wifi_connected()) {
        return;   /* the time sync starts once the network's up; it resyncs hourly by itself */
    }
    started = true;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_sync;
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "no time sync");
    }
}

bool muse_clock_now(struct tm *out)
{
    time_t t = time(NULL);
    localtime_r(&t, out);
    return out->tm_year + 1900 >= 2025;   /* before it's set, the clock starts in 1970 */
}

bool muse_clock_sleep_hours(void)
{
    int from = muse_settings_sleep_from(), to = muse_settings_sleep_to();
    struct tm now;
    if (from < 0 || to < 0 || !muse_clock_now(&now)) {
        return false;
    }
    int h = now.tm_hour;
    return from < to ? h >= from && h < to : h >= from || h < to;   /* 23 to 7 wraps midnight */
}

bool muse_clock_quiet(void)
{
    return muse_settings_dnd() || muse_clock_sleep_hours();
}
