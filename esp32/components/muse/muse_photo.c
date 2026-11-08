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
 * The Muse app's picture as the face (muse_photo.h). The picture, its backdrop
 * already black, fills the avatar's square. The face lives through patches
 * the host drew from the picture itself: it blinks every few seconds, squints
 * happily with a little open smile when petted, and opens its mouth with
 * Muse's voice. Around that, gentle body language: breathing when idle,
 * leaning in to listen, a slow sway while thinking, a small nod while talking.
 * Motion is quantised (half-pixel steps), so a frame is redrawn only when
 * what it shows changes; sampling is nearest-neighbour, a table walk per row.
 */
#include "muse_photo.h"

#include <math.h>
#include <string.h>

#define TAU 6.2831853f
/* tools/muse/photo_avatar.py crops the picture to head and shoulders and
 * turns its backdrop black, so the canvas holds all of it, centred. */
#define VIEW_SHARE 1.0f
#define FOCUS_X 0.50f
#define FOCUS_Y 0.50f

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

static const muse_photo_patch_t *patch(muse_photo_part_t part)
{
    for (int i = 0; i < muse_photo_patch_count; i++) {
        if (muse_photo_patches[i].part == part) return &muse_photo_patches[i];
    }
    return NULL;
}

/* The patches showing this frame: eyes, mouth (NULL: the picture's own). */
static const muse_photo_patch_t *s_eyes, *s_mouth;

/* Blinks come every 2.5 to 5.5 s, now and then a double one. */
static float s_blink_at = 2.0f, s_blink_end;

static float next_rand(void)
{
    static uint32_t x = 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return (x & 0xffff) / 65535.0f;
}

/* Idle life: every 6 to 12 s one small thing, glancing aside, looking
 * around, a smile, a double blink, and once Muse has been quiet a minute, a
 * yawn. Another mode drops it. */
enum { ACT_NONE, ACT_GLANCE_L, ACT_GLANCE_R, ACT_LOOK_AROUND, ACT_SMILE, ACT_YAWN, ACT_COUNT };
static int s_act;
static float s_act_t0, s_act_len, s_next_act = 5.0f;

static void idle_life(float t, float mt, float *dx, float *dy, muse_photo_part_t *eyes, muse_photo_part_t *mouth)
{
    if (s_act == ACT_NONE && t >= s_next_act) {
        float r = next_rand();
        s_act = r < 0.22f ? ACT_GLANCE_L : r < 0.44f ? ACT_GLANCE_R : r < 0.62f ? ACT_LOOK_AROUND
              : r < 0.84f ? ACT_SMILE : mt > 60.0f ? ACT_YAWN : ACT_SMILE;
        if (next_rand() < 0.2f) {
            s_blink_at = t;   /* a double blink, before or over whatever comes */
        }
        static const float LEN[ACT_COUNT] = { 0, 1.4f, 1.4f, 3.0f, 2.5f, 2.4f };
        s_act_t0 = t;
        s_act_len = LEN[s_act];
    }
    if (s_act == ACT_NONE) {
        return;
    }
    float a = t - s_act_t0;
    if (a >= s_act_len) {
        s_act = ACT_NONE;
        s_next_act = t + 6.0f + 6.0f * next_rand();
        return;
    }
    float ease = clampf(a / 0.25f, 0, 1) * clampf((s_act_len - a) / 0.25f, 0, 1);   /* in and out */
    switch (s_act) {
    case ACT_GLANCE_L:
    case ACT_GLANCE_R:
        *eyes = s_act == ACT_GLANCE_L ? MUSE_PHOTO_EYES_LEFT : MUSE_PHOTO_EYES_RIGHT;
        *dx += (s_act == ACT_GLANCE_L ? -2.0f : 2.0f) * ease;   /* the head follows a little */
        break;
    case ACT_LOOK_AROUND: {
        bool left = a < 1.3f, right = a > 1.7f;
        *eyes = left ? MUSE_PHOTO_EYES_LEFT : right ? MUSE_PHOTO_EYES_RIGHT : MUSE_PHOTO_PATCH_NONE;
        *dx += 2.5f * sinf(TAU * (a / s_act_len) - TAU / 4) * ease;   /* left, then right */
        break;
    }
    case ACT_SMILE:
        *mouth = MUSE_PHOTO_MOUTH_SMILE;
        break;
    case ACT_YAWN:
        *mouth = MUSE_PHOTO_MOUTH_3;
        if (a > 0.3f && a < s_act_len - 0.4f) *eyes = MUSE_PHOTO_EYES_CLOSED;
        *dy -= 2.0f * ease;   /* a slight stretch */
        break;
    default:
        break;
    }
}

bool muse_photo_render(const muse_pose_t *p)
{
    float t = p->t, mt = p->mode_t, lvl = clampf(p->level, 0, 1);
    float zoom = 1.0f, dx = 0, dy = 0, bright = 1.0f;
    float breath = sinf(TAU * t / 4.5f);
    switch (p->mode) {
    case MUSE_MODE_LISTENING: {
        float lean = clampf(mt / 0.4f, 0, 1);              /* leans in a touch */
        zoom = 1.0f + 0.02f * lean;
        dy = 0.8f * breath;
        break;
    }
    case MUSE_MODE_THINKING: {
        float rate = p->working > 0.5f ? 2.2f : 3.5f;      /* working sways quicker */
        dx = 2.0f * sinf(TAU * t / rate);
        dy = 0.6f * breath;
        break;
    }
    case MUSE_MODE_SPEAKING:
        dy = -1.5f * lvl + 0.6f * breath;                  /* a small nod with the words */
        break;
    case MUSE_MODE_ERROR:
        dx = 4.0f * sinf(TAU * 7.0f * mt) * expf(-3.0f * mt);   /* a short shake */
        bright = 0.8f;
        break;
    case MUSE_MODE_OFF:
        bright = clampf(1.0f - mt, 0.25f, 1.0f);
        break;
    default:                                               /* boot, idle: breathing */
        zoom = 1.0f + 0.006f * breath;
        dy = 1.0f * sinf(TAU * t / 4.5f + 1.0f);
        break;
    }
    bool happy = p->happy > 0.25f;
    if (p->happy > 0.01f) {
        dy -= 6.0f * p->happy * fabsf(sinf(TAU * 2.0f * t));   /* a bounce when petted */
    }
    muse_photo_part_t idle_eyes = MUSE_PHOTO_PATCH_NONE, idle_mouth = MUSE_PHOTO_PATCH_NONE;
    if (p->mode == MUSE_MODE_IDLE && !happy) {
        idle_life(t, mt, &dx, &dy, &idle_eyes, &idle_mouth);
    } else if (s_act != ACT_NONE || t > s_next_act) {
        s_act = ACT_NONE;   /* busy: start over a little after it's idle again */
        s_next_act = t + 4.0f;
    }

    /* Eyes: happy beats a blink; asleep, they close. */
    if (t >= s_blink_at) {
        s_blink_end = t + 0.13f;
        s_blink_at = t + (next_rand() < 0.15f ? 0.35f : 2.5f + 3.0f * next_rand());
    }
    const muse_photo_patch_t *eyes = NULL;
    if (happy) {
        eyes = patch(MUSE_PHOTO_EYES_HAPPY);
    } else if (t < s_blink_end || (p->mode == MUSE_MODE_OFF && mt > 0.3f)) {
        eyes = patch(MUSE_PHOTO_EYES_CLOSED);
    } else if (idle_eyes != MUSE_PHOTO_PATCH_NONE) {
        eyes = patch(idle_eyes);
    }
    /* Mouth: open with the voice, a small smile when petted. */
    const muse_photo_patch_t *mouth = NULL;
    if (p->mode == MUSE_MODE_SPEAKING) {
        mouth = lvl > 0.55f ? patch(MUSE_PHOTO_MOUTH_3) : lvl > 0.25f ? patch(MUSE_PHOTO_MOUTH_2)
              : lvl > 0.06f ? patch(MUSE_PHOTO_MOUTH_1) : NULL;
    } else if (happy) {
        mouth = patch(MUSE_PHOTO_MOUTH_1);
    } else if (idle_mouth != MUSE_PHOTO_PATCH_NONE) {
        mouth = patch(idle_mouth);
    }

    int z = (int)lroundf(zoom * 1024), x2 = (int)lroundf(dx * 2), y2 = (int)lroundf(dy * 2);
    int b = (int)lroundf(bright * 16);
    bool changed = z != s_zoom || x2 != s_dx2 || y2 != s_dy2 || b != s_bright || eyes != s_eyes ||
                   mouth != s_mouth;
    s_zoom = z;
    s_dx2 = x2;
    s_dy2 = y2;
    s_bright = b;
    s_eyes = eyes;
    s_mouth = mouth;
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
    /* Picture pixels per screen pixel, in 16.16: the picture fills the canvas. */
    const float step = VIEW_SHARE * side / (float)s_size / (s_zoom / 1024.0f);
    const int32_t step_fp = (int32_t)(step * 65536.0f);
    const float cx = half + s_dx2 * 0.5f, cy = half + s_dy2 * 0.5f;
    const int32_t fx_fp = (int32_t)(FOCUS_X * side * 65536.0f), fy_fp = (int32_t)(FOCUS_Y * side * 65536.0f);
    const int32_t max_fp = (side - 1) << 16;

    for (int y = y0; y <= y1; y++) {
        uint16_t *row = dst + (size_t)(y - y0) * stride_px;
        int32_t sy = fy_fp + (int32_t)((y - cy) * step_fp);
        if (sy < 0 || sy > max_fp) {
            memset(row, 0, (size_t)(x1 - x0 + 1) * sizeof(uint16_t));   /* moved past the picture: black */
            continue;
        }
        int iy = sy >> 16;
        const uint16_t *src = muse_photo_px + (size_t)iy * side;
        /* The face's patches crossing this row: their pixels stand in for the picture's. */
        const muse_photo_patch_t *on[2];
        int n_on = 0;
        if (s_eyes && iy >= s_eyes->y && iy < s_eyes->y + s_eyes->h) on[n_on++] = s_eyes;
        if (s_mouth && iy >= s_mouth->y && iy < s_mouth->y + s_mouth->h) on[n_on++] = s_mouth;
        int32_t sx = fx_fp + (int32_t)((x0 - cx) * step_fp);
        for (int x = x0; x <= x1; x++, sx += step_fp) {
            uint16_t px = 0;
            if (sx >= 0 && sx <= max_fp) {
                int ix = sx >> 16;
                px = src[ix];
                for (int k = 0; k < n_on; k++) {
                    const muse_photo_patch_t *q = on[k];
                    if (ix >= q->x && ix < q->x + q->w) {
                        px = q->px[(size_t)(iy - q->y) * q->w + (ix - q->x)];
                        break;
                    }
                }
            }
            row[x - x0] = s_bright < 16 ? dim(px, s_bright) : px;
        }
    }
}
