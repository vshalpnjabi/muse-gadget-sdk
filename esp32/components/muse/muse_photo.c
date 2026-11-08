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
 * The Muse app's picture as the face (muse_photo.h). The picture is shown in a
 * circle, a little zoomed on the face, and moved as a whole: there's no
 * per-feature animation, so the motion is the body language (breathing,
 * leaning in, a pulse with the voice, a sway) and the UI's ring carries the
 * mode's colour. The motion is quantised (half-pixel steps), so a frame is
 * redrawn only when what it shows moves, and sampling is nearest-neighbour,
 * one table walk per row, cheap enough for every strip LVGL asks for.
 */
#include "muse_photo.h"

#include <math.h>
#include <string.h>

#define TAU 6.2831853f
/* tools/muse/photo_avatar.py crops the picture to what the circle shows, so
 * the circle holds all of it, centred. */
#define VIEW_SHARE 1.0f
#define FOCUS_X 0.50f
#define FOCUS_Y 0.50f
#define EDGE 2   /* px between the circle and the canvas edge, for the UI's ring */

static int s_size = 320;
/* What's shown: zoom in 1/1024 steps, offsets in half pixels, brightness in 1/16. */
static int s_zoom = 1024, s_dx2, s_dy2, s_bright = 16;

void muse_photo_set_size(int px)
{
    s_size = px > 8 ? px : 8;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

bool muse_photo_render(const muse_pose_t *p)
{
    float t = p->t, mt = p->mode_t, lvl = clampf(p->level, 0, 1);
    float zoom = 1.0f, dx = 0, dy = 0, bright = 1.0f;
    float breath = sinf(TAU * t / 4.5f);
    switch (p->mode) {
    case MUSE_MODE_LISTENING: {
        float lean = clampf(mt / 0.35f, 0, 1);            /* leans in, then follows your voice */
        zoom = 1.0f + 0.035f * lean + 0.02f * lvl;
        dy = 1.5f * breath;
        break;
    }
    case MUSE_MODE_THINKING: {
        float rate = p->working > 0.5f ? 1.6f : 3.0f;    /* working sways quicker */
        dx = 4.0f * sinf(TAU * t / rate);
        zoom = 1.0f + 0.008f * breath;
        break;
    }
    case MUSE_MODE_SPEAKING:
        zoom = 1.01f + 0.04f * lvl;                       /* a pulse with Muse's voice */
        dy = -3.0f * lvl + 1.0f * breath;
        break;
    case MUSE_MODE_ERROR:
        dx = 5.0f * sinf(TAU * 7.0f * mt) * expf(-3.0f * mt);   /* a short shake */
        bright = 0.8f;
        break;
    case MUSE_MODE_OFF:
        bright = clampf(1.0f - mt, 0.25f, 1.0f);
        break;
    default:                                              /* boot, idle: breathing */
        zoom = 1.0f + 0.012f * breath;
        dy = 2.0f * sinf(TAU * t / 4.5f + 1.0f);
        break;
    }
    if (p->happy > 0.01f) {
        dy -= 10.0f * p->happy * fabsf(sinf(TAU * 2.5f * t));   /* a bounce when petted */
    }
    int z = (int)lroundf(zoom * 1024), x2 = (int)lroundf(dx * 2), y2 = (int)lroundf(dy * 2);
    int b = (int)lroundf(bright * 16);
    bool changed = z != s_zoom || x2 != s_dx2 || y2 != s_dy2 || b != s_bright;
    s_zoom = z;
    s_dx2 = x2;
    s_dy2 = y2;
    s_bright = b;
    return changed;
}

static inline uint16_t dim(uint16_t c, int b)
{
    unsigned r = (c >> 11) * b >> 4, g = ((c >> 5) & 63) * b >> 4, bl = (c & 31) * b >> 4;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

void muse_photo_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    const int side = muse_photo_side;
    const float half = (s_size - 1) * 0.5f;
    const float rc = s_size * 0.5f - EDGE;
    /* Picture pixels per screen pixel, in 16.16. */
    const float step = VIEW_SHARE * side / (2.0f * rc) / (s_zoom / 1024.0f);
    const int32_t step_fp = (int32_t)(step * 65536.0f);
    const float cx = half + s_dx2 * 0.5f, cy = half + s_dy2 * 0.5f;
    const int32_t fx_fp = (int32_t)(FOCUS_X * side * 65536.0f), fy_fp = (int32_t)(FOCUS_Y * side * 65536.0f);
    const int32_t max_fp = (side - 1) << 16;

    for (int y = y0; y <= y1; y++) {
        uint16_t *row = dst + (size_t)(y - y0) * stride_px;
        float ry = y - half;   /* the circle stays put; the picture moves inside it */
        float hw2 = rc * rc - ry * ry;
        int lo = x1 + 1, hi = x0 - 1;
        if (hw2 > 0) {
            float hw = sqrtf(hw2);
            lo = (int)ceilf(half - hw);
            hi = (int)floorf(half + hw);
            lo = lo < x0 ? x0 : lo;
            hi = hi > x1 ? x1 : hi;
        }
        if (lo > hi) {
            memset(row, 0, (size_t)(x1 - x0 + 1) * sizeof(uint16_t));
            continue;
        }
        if (lo > x0) memset(row, 0, (size_t)(lo - x0) * sizeof(uint16_t));
        if (hi < x1) memset(row + (hi - x0 + 1), 0, (size_t)(x1 - hi) * sizeof(uint16_t));

        int32_t sy = fy_fp + (int32_t)((y - cy) * step_fp);
        sy = sy < 0 ? 0 : sy > max_fp ? max_fp : sy;
        const uint16_t *src = muse_photo_px + (size_t)(sy >> 16) * side;
        int32_t sx = fx_fp + (int32_t)((lo - cx) * step_fp);
        for (int x = lo; x <= hi; x++, sx += step_fp) {
            int32_t c = sx < 0 ? 0 : sx > max_fp ? max_fp : sx;
            uint16_t px = src[c >> 16];
            row[x - x0] = s_bright < 16 ? dim(px, s_bright) : px;
        }
    }
}
