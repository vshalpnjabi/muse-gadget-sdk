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

#include <stdint.h>

/*
 * The UI's colours (Settings > Appearance > Theme): the face's icons and
 * captions and the settings pages. Picked once at start, from the "theme"
 * setting: the face is built once, so a new theme shows after a restart.
 */

typedef struct {
    const char *name;
    uint32_t text, dim, caption, lit;           /* type and icons */
    uint32_t card, card_pressed, slider_bg;     /* settings surfaces */
    uint32_t accent;                            /* switches, sliders, checks */
    uint32_t ring_bg, meter_off, dot_off;       /* the face's quiet parts */
} muse_theme_t;

/* The theme in use (chosen at start). */
const muse_theme_t *muse_theme(void);

/* Every theme, for the picker: index 0..muse_theme_count()-1. */
int muse_theme_count(void);
const muse_theme_t *muse_theme_at(int i);

/* From the setting; call once, after muse_settings_init(). */
void muse_theme_init(void);
