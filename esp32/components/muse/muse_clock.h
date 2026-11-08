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

#pragma once

#include <stdbool.h>
#include <time.h>

/*
 * Local time for the face (CONFIG_MUSE_CLOCK): set from the internet (SNTP)
 * once Wi-Fi is up, in the time zone of muse_settings_tz(), and what follows
 * from it: whether it's sleep hours, and whether the board should be quiet.
 */

/* Applies the time zone. Call once at start. */
void muse_clock_start(void);

/* Starts the time sync once Wi-Fi is up. Call every second or so. */
void muse_clock_tick(void);

/* Re-applies the time zone after it changed (MUSE_SETTING_QUIET). */
void muse_clock_apply_tz(void);

/* The local time; false until it has been set. */
bool muse_clock_now(struct tm *out);

/* Inside the sleep hours now (false with none, or no time yet). */
bool muse_clock_sleep_hours(void);

/* No sounds of the board's own: do not disturb, or sleep hours. */
bool muse_clock_quiet(void);
