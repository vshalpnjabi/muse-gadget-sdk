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
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * User settings, persisted in NVS. Setters save immediately and then notify the
 * change listener (from the caller's task), which applies the setting to the
 * hardware. Brightness, auto-sleep and the speaker are polled where they're used,
 * as is the reply text size, which notifies nothing.
 */

#define MUSE_SSID_MAX 32
#define MUSE_PASS_MAX 64
#define MUSE_HOST_MAX 63
#define MUSE_VM_MAX 63
#define MUSE_TOKEN_MAX 1023

#define MUSE_MIC_GAIN_MAX 36      /* dB; ES7210 PGA, applied in 3 dB steps */

typedef enum {
    MUSE_SETTING_VOLUME,
    MUSE_SETTING_SPEAKER,
    MUSE_SETTING_MIC_GAIN,
    MUSE_SETTING_BRIGHTNESS,
    MUSE_SETTING_SLEEP,
    MUSE_SETTING_WIFI,          /* on/off or credentials */
    MUSE_SETTING_BLE,
    MUSE_SETTING_HATCH,
    MUSE_SETTING_VOICE_NOTES,
    MUSE_SETTING_AVATAR,
    MUSE_SETTING_CAPTIONS,
    MUSE_SETTING_QUIET,
    MUSE_SETTING_SPEAKER_BOOST,         /* do not disturb, sleep hours or the time zone */
} muse_setting_t;

/* Which avatar the face shows (Settings > Appearance): the drawn pixel one
 * (muse_pixel.c), or the Muse app's picture (muse_photo.c) where it's built in. */
enum { MUSE_AVATAR_PIXEL, MUSE_AVATAR_PHOTO, MUSE_AVATAR_COUNT };

typedef void (*muse_setting_cb_t)(muse_setting_t what);

esp_err_t muse_settings_init(void);
void muse_settings_set_listener(muse_setting_cb_t cb);

int muse_settings_volume(void);         /* 0..100 */
bool muse_settings_speaker_on(void);    /* off: replies are shown, not played */
int muse_settings_mic_gain(void);       /* dB, 0..MUSE_MIC_GAIN_MAX */
int muse_settings_brightness(void);     /* 10..100 */
int muse_settings_sleep_s(void);        /* 0 = never */
/* Reply text size on small screens with full-screen replies. */
enum { MUSE_TEXT_SMALL, MUSE_TEXT_MEDIUM, MUSE_TEXT_LARGE };
int muse_settings_text_size(void);
bool muse_settings_wifi_on(void);
bool muse_settings_ble_on(void);
/* StopWatch: false selects realtime PTT; true selects voice notes only. */
bool muse_settings_voice_notes(void);
void muse_settings_set_voice_notes(bool on);
int muse_settings_avatar(void);          /* MUSE_AVATAR_* */
void muse_settings_set_avatar(int which);
/* Settings > Appearance > Captions: what you said and Muse's replies as text on
 * the face. Off, the face shows neither and Muse keeps its size; status
 * captions (CONNECTING, PUSH TO TALK, errors) still show. On by default. */
bool muse_settings_captions(void);
void muse_settings_set_captions(bool on);
/* Settings > Appearance > Theme: an index into muse_theme_at(), shown after a restart. */
/* Sound > Speaker boost: the speaker's gain at volume 100, 0..MUSE_SPEAKER_BOOST_MAX dB. */
#define MUSE_SPEAKER_BOOST_MAX 24
int muse_settings_speaker_boost(void);
void muse_settings_set_speaker_boost(int db);
int muse_settings_theme(void);
void muse_settings_set_theme(int theme);
/* Settings > Appearance > Auto-rotate: the screen turns to how the board is held (motion sensor). */
bool muse_settings_autorotate(void);
void muse_settings_set_autorotate(bool on);
/* Settings > Appearance > 24-hour clock: 21:47 rather than 9:47 on the face (on by default). */
bool muse_settings_clock24(void);
void muse_settings_set_clock24(bool on);
/* Settings > Sound, the voice guard: when a call's voice isn't recognized, show a notice
 * (on), play a tone (on), and/or have Muse say so (off: needs Muse's side). */
/* Settings > Sound > Voice recognition: the whole voice guard on or off (off: no dot, nothing sent). */
bool muse_settings_vg_on(void);
void muse_settings_set_vg_on(bool on);
bool muse_settings_vg_notice(void);
bool muse_settings_vg_tone(void);
bool muse_settings_vg_explain(void);
void muse_settings_set_vg_notice(bool on);
void muse_settings_set_vg_tone(bool on);
void muse_settings_set_vg_explain(bool on);

/* Do not disturb (Settings > Sleep, or asked of Muse): the board makes none
 * of its own sounds (touch, motion, chirps); Muse's answers to you still play. */
bool muse_settings_dnd(void);
void muse_settings_set_dnd(bool on);
/* Sleep hours, local time: from `from` o'clock to `to` o'clock (it may wrap
 * past midnight); -1 for none. Muse sleeps on the face, quiet, the screen dim. */
int muse_settings_sleep_from(void);
int muse_settings_sleep_to(void);
void muse_settings_set_sleep_hours(int from, int to);
/* The time zone, as a POSIX TZ string (e.g. PST8PDT,M3.2.0,M11.1.0). */
#define MUSE_TZ_MAX 47
void muse_settings_tz(char out[MUSE_TZ_MAX + 1]);
void muse_settings_set_tz(const char *tz);

void muse_settings_wifi(char ssid[MUSE_SSID_MAX + 1], char pass[MUSE_PASS_MAX + 1]);
void muse_settings_hatch_host(char out[MUSE_HOST_MAX + 1]);
void muse_settings_hatch_vm(char out[MUSE_VM_MAX + 1]);
void muse_settings_hatch_token(char out[MUSE_TOKEN_MAX + 1]);
size_t muse_settings_hatch_token_len(void);

void muse_settings_set_volume(int pct);
void muse_settings_set_speaker_on(bool on);
void muse_settings_set_mic_gain(int db);
void muse_settings_set_brightness(int pct);
void muse_settings_set_sleep_s(int secs);
void muse_settings_set_text_size(int size);
void muse_settings_set_wifi_on(bool on);
void muse_settings_set_ble_on(bool on);
/* A network name is remembered first among the saved ones and joined now;
 * an empty ssid forgets every saved network. */
void muse_settings_set_wifi(const char *ssid, const char *pass);
void muse_settings_set_hatch_host(const char *host);
void muse_settings_set_hatch_vm(const char *vm);
/* append=true adds to the stored token (for chunked BLE writes). */
esp_err_t muse_settings_set_hatch_token(const char *token, bool append);
