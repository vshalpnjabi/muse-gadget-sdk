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

#include "muse_theme.h"

#include "muse_settings.h"

/* Two hand-set themes, then solid colours whose surfaces are made from the accent. */
static muse_theme_t s_themes[] = {
    /* Muse: Vi in the Muse app. Navy like his hoodie, warm beige like his tee,
     * cream type, brown-grey for what's quiet. */
    { "Muse", 0xf5ece0, 0x9c9184, 0xf0e4d0, 0xf5ece0, 0x172033, 0x26334f, 0x1d2840, 0xe7c9a0, 0x10151f, 0x1b2233,
      0x34405a },
    /* Purple: the original. */
    { "Purple", 0xf2efff, 0x8b84a8, 0xd8d2ff, 0xf2efff, 0x1a1530, 0x2e2552, 0x2a2345, 0xa77dff, 0x140f22, 0x1d1733,
      0x3a3358 },
    { "Blue", 0, 0, 0, 0, 0, 0, 0, 0x5cb8ff, 0, 0, 0 },
    { "Green", 0, 0, 0, 0, 0, 0, 0, 0x5fd99a, 0, 0, 0 },
    { "Orange", 0, 0, 0, 0, 0, 0, 0, 0xffa24f, 0, 0, 0 },
    { "Pink", 0, 0, 0, 0, 0, 0, 0, 0xff7fb0, 0, 0, 0 },
    { "Mono", 0, 0, 0, 0, 0, 0, 0, 0xe8e8e8, 0, 0, 0 },
};
#define THEMES (int)(sizeof(s_themes) / sizeof(s_themes[0]))

static int s_current;

/* a toward b by t/256 */
static uint32_t mix(uint32_t a, uint32_t b, int t)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int ca = (a >> sh) & 0xff, cb = (b >> sh) & 0xff;
        out |= (uint32_t)(ca + (cb - ca) * t / 256) << sh;
    }
    return out;
}

/* A solid theme's surfaces and type, from its accent: tinted near-blacks and near-whites. */
static void derive(muse_theme_t *th)
{
    uint32_t a = th->accent;
    th->text = mix(0xffffff, a, 20);
    th->lit = th->text;
    th->caption = mix(0xffffff, a, 50);
    th->dim = mix(0x8a8a8a, a, 90);
    th->card = mix(0x0a0a0a, a, 30);
    th->card_pressed = mix(0x0a0a0a, a, 64);
    th->slider_bg = mix(0x0a0a0a, a, 50);
    th->ring_bg = mix(0x000000, a, 22);
    th->meter_off = mix(0x0a0a0a, a, 34);
    th->dot_off = mix(0x202020, a, 70);
}

void muse_theme_init(void)
{
    for (int i = 0; i < THEMES; i++) {
        if (!s_themes[i].text) derive(&s_themes[i]);
    }
    int t = muse_settings_theme();
    s_current = t >= 0 && t < THEMES ? t : 0;
}

const muse_theme_t *muse_theme(void)
{
    return &s_themes[s_current];
}

int muse_theme_count(void)
{
    return THEMES;
}

const muse_theme_t *muse_theme_at(int i)
{
    return &s_themes[i < 0 || i >= THEMES ? 0 : i];
}
