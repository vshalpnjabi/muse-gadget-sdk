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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Wake word (CONFIG_MUSE_WAKE_WORD): a microWakeWord streaming model (int8
 * TFLite, embedded from CONFIG_MUSE_WAKE_MODEL) listens to the idle mic while
 * Settings > Sound > Wake word is on, and a detection starts a hands-free call.
 */

/* Starts the listening task. Once, after audio is up. */
void muse_wake_start(void);

/* From muse_audio_read(): the mic PCM (16 kHz mono). Copies, never waits. */
void muse_wake_feed(const int16_t *pcm, size_t frames);

/* Whether it's listening now (the setting on, the model loaded): the voice
 * task then keeps reading the mic instead of resting asleep. */
bool muse_wake_listening(void);

/* True once per detection: the voice task starts a call. */
bool muse_wake_take(void);

#ifdef __cplusplus
}
#endif
