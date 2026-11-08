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

/*
 * Voice guard, shadow mode (CONFIG_MUSE_VOICE_GUARD): a copy of the mic PCM
 * that goes to Muse is kept (the last ~4 s), and while a call or voice note is
 * on, every 0.5 s its last 3 s go to a speaker-verification service whose
 * verdict is logged and shown as a dot. Nothing about the Muse audio changes:
 * the verdict is only displayed.
 */

/* From muse_audio_read(): the PCM exactly as handed on (16 kHz mono). Voice task. */
void muse_vg_tee(const int16_t *pcm, size_t frames);

/* Starts the worker. Call once after audio is up. */
void muse_vg_start(void);

/* Whether it's on (a token set and the worker started): the face shows its dot. */
bool muse_vg_running(void);

/* The call's verification: unverified at its start, verified on an accepted
 * window, lost after rejects in a row (quiet windows aren't scored, so they
 * never count against it). */
typedef enum { MUSE_VG_UNVERIFIED, MUSE_VG_VERIFIED, MUSE_VG_LOST } muse_vg_state_t;
muse_vg_state_t muse_vg_state(void);

/*
 * The uplink gate (CONFIG_MUSE_VG_ENFORCE), voice task only. Every mic chunk
 * is in the guard's ring already (muse_vg_tee), so the voice task sends from
 * there: muse_vg_uplink_next() gives the next audio Muse may have (none while
 * gating; once verified, from the accepted window on, then live), and
 * muse_vg_uplink_sent() moves past what Muse took. Audio that isn't wanted
 * (Muse talking, the button not held) is passed over with muse_vg_uplink_skip().
 */
bool muse_vg_gating(void);
size_t muse_vg_uplink_next(int16_t *out, size_t cap);
void muse_vg_uplink_sent(size_t n);
void muse_vg_uplink_skip(void);
void muse_vg_uplink_held(size_t n);   /* counts held-back audio, for the call's log line */

/* Voice calibration (Settings > Sound > Calibrate voice): the windows go to
 * the relay's calibration store; the server builds and swaps the print. */
typedef enum {
    MUSE_VG_CAL_IDLE,
    MUSE_VG_CAL_RECORDING,
    MUSE_VG_CAL_BUILDING,
    MUSE_VG_CAL_OK,
    MUSE_VG_CAL_FAILED,
} muse_vg_cal_phase_t;
typedef struct {
    muse_vg_cal_phase_t phase;
    int uploaded, rejected;      /* windows */
    float threshold;             /* OK */
    int windows_used;            /* OK */
    char reason[32];             /* FAILED: the server's, or unreachable / timeout / not_supported */
} muse_vg_cal_status_t;
bool muse_vg_cal_start(void);    /* false if the guard isn't running */
void muse_vg_cal_finish(void);   /* the script's been read */
void muse_vg_cal_cancel(void);
void muse_vg_cal_status(muse_vg_cal_status_t *out);

/* Counts verdicts as they come: the dot blinks on each. */
uint32_t muse_vg_seq(void);

/* The latest verdict, for the face's dot: false if none has come (grey). */
bool muse_vg_latest(float *score, bool *accept);
