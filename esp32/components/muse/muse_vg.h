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

/* Counts verdicts as they come: the dot blinks on each. */
uint32_t muse_vg_seq(void);

/* The latest verdict, for the face's dot: false if none has come (grey). */
bool muse_vg_latest(float *score, bool *accept);
