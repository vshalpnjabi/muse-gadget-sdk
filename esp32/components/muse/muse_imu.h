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

/*
 * The board's motion sensor (a QMI8658 6-axis IMU on the shared I2C bus), as
 * the face uses it: which way the board is tipped, and whether it's being
 * shaken. A task reads it at 50 Hz and keeps these up to date.
 */

typedef struct {
    bool present;
    float tilt_x;   /* -1 .. 1: tipped left .. right since it last rested (a few seconds), as you look at it */
    float tilt_y;   /* -1 .. 1: tipped top edge down .. bottom edge down, likewise */
    float shake;    /* 0 still .. 1 shaken hard; decays over about a second */
} muse_motion_t;

/* `bus` is an i2c_master_bus_handle_t. ESP_OK if the sensor answered. */
int muse_imu_init(void *bus);

/* The latest motion; .present is false without a sensor. Any task. */
void muse_motion(muse_motion_t *out);
