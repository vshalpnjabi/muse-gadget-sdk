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
#include <stdint.h>

#include "muse_pixel.h"

/*
 * The Muse app's picture of your Muse as the face: a portrait on black the board
 * animates itself (breathing, leaning in to listen, a pulse with Muse's voice,
 * a sway while it thinks). Built in when components/muse/avatar/muse_photo_img.c
 * exists (tools/muse/photo_avatar.py makes it); MUSE_HAS_PHOTO_AVATAR is then 1.
 * Same contract as muse_pixel_render / _set_size / _scale.
 */

/* The picture, square, RGB565 as muse_pixel_scale writes it (muse_photo_img.c). */
extern const uint16_t muse_photo_px[];
extern const int muse_photo_side;

/* The face's moving parts, drawn by tools/muse/photo_avatar.py from the
 * picture itself (--eyes, --mouth): each is a rectangle of the picture, in its
 * own pixels, swapped in while that part moves. None without those options. */
typedef enum {
    MUSE_PHOTO_PATCH_NONE,
    MUSE_PHOTO_EYES_CLOSED,   /* a blink */
    MUSE_PHOTO_EYES_HAPPY,    /* closed in an upward curve: petted */
    MUSE_PHOTO_MOUTH_1,       /* open, small to wide, with Muse's voice */
    MUSE_PHOTO_MOUTH_2,
    MUSE_PHOTO_MOUTH_3,
    MUSE_PHOTO_EYES_LEFT,     /* idle glances, as you look at the screen */
    MUSE_PHOTO_EYES_RIGHT,
    MUSE_PHOTO_MOUTH_SMILE,   /* an idle smile */
    MUSE_PHOTO_EYES_WINK,     /* the screen's right eye closed: a touch reaction */
    MUSE_PHOTO_EYES_WINK_L,   /* the screen's left eye closed */
} muse_photo_part_t;

typedef struct {
    muse_photo_part_t part;
    int16_t x, y, w, h;       /* in the picture's pixels */
    const uint16_t *px;       /* w * h, RGB565 */
} muse_photo_patch_t;

extern const muse_photo_patch_t muse_photo_patches[];
extern const int muse_photo_patch_count;

/* A touch on the face at (u, v), each 0..1 across and down the avatar: the
 * next pet (muse_state_make_happy) reacts to where it landed. The hair gets a
 * ruffle and sparkles, an eye winks, the cheeks and mouth giggle with hearts,
 * the chin nods, a shoulder makes Muse look that way, and three quick taps a
 * playful head shake. now_s: any clock in seconds, to tell quick taps.
 * Returns the touch sound to play (MUSE_SFX_*), or -1. */
int muse_photo_touch(float u, float v, float now_s);

/* Moves the picture for this frame. True if what it shows changed. */
bool muse_photo_render(const muse_pose_t *pose);

void muse_photo_set_size(int px);

void muse_photo_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1);
