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
 * QMI8658 accelerometer, read for the face (muse_imu.h). Only the
 * accelerometer is needed: gravity, low-passed, gives the tilt, and how far
 * the force strays from 1 g gives the shake. The task's stack is in PSRAM
 * (it never writes flash), so it costs no internal RAM beyond its handle.
 */
#include "muse_imu.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "muse_imu";

#define REG_WHO_AM_I 0x00   /* 0x05 */
#define REG_CTRL1 0x02      /* bit 6: register address auto-increment */
#define REG_CTRL2 0x03      /* accelerometer: full scale [6:4], data rate [3:0] */
#define REG_CTRL7 0x08      /* bit 0: accelerometer on */
#define REG_AX_L 0x35       /* AX, AY, AZ, little-endian int16 each */
#define WHO_AM_I 0x05
#define ACC_4G_125HZ 0x16   /* +-4 g (8192 LSB/g), ~125 Hz */
#define LSB_PER_G 8192.0f
#define PERIOD_MS 20

static i2c_master_dev_handle_t s_dev;
static _Atomic float s_tilt_x, s_tilt_y, s_shake;
static atomic_bool s_present;

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 50);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 50);
}

static void imu_task(void *arg)
{
    (void)arg;
    float gx = 0, gy = 0, gz = 1, shake = 0;
    bool first = true;
    for (;;) {
        uint8_t raw[6];
        if (rd(REG_AX_L, raw, sizeof(raw)) == ESP_OK) {
            float ax = (int16_t)(raw[0] | raw[1] << 8) / LSB_PER_G;
            float ay = (int16_t)(raw[2] | raw[3] << 8) / LSB_PER_G;
            float az = (int16_t)(raw[4] | raw[5] << 8) / LSB_PER_G;
            if (first) {
                gx = ax, gy = ay, gz = az;
                first = false;
            }
            /* Gravity: a slow low-pass. Shake: the force off 1 g, held and let go. */
            gx += (ax - gx) * 0.12f;
            gy += (ay - gy) * 0.12f;
            gz += (az - gz) * 0.12f;
            float off = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
            static int n;
            if (++n == 2000 / PERIOD_MS) {   /* once, settled: how it rests (about 1 g in all) */
                ESP_LOGI(TAG, "gravity x %.2f y %.2f z %.2f g", gx, gy, gz);
            }
            float hit = off > 0.35f ? (off - 0.35f) / 1.2f : 0;
            shake = hit > shake ? (hit > 1 ? 1 : hit) : shake * 0.96f;
            /* Movement, not how it rests: the tilt is gravity's change from a
             * slow baseline (about 4 s), so a board standing on a desk looks
             * straight ahead, and tipping it from there is what turns Muse's eyes.
             * The chip's X across the screen, Y up it. */
            static float bx, by;
            static bool based;
            if (!based) {
                bx = gx, by = gy;
                based = true;
            }
            bx += (gx - bx) * 0.005f;
            by += (gy - by) * 0.005f;
            float tx = -(gx - bx) * 2.0f, ty = (gy - by) * 2.0f;
            atomic_store(&s_tilt_x, tx > 1 ? 1 : tx < -1 ? -1 : tx);
            atomic_store(&s_tilt_y, ty > 1 ? 1 : ty < -1 ? -1 : ty);
            atomic_store(&s_shake, shake);
        }
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}

int muse_imu_init(void *bus)
{
    static const uint8_t ADDRS[] = { 0x6b, 0x6a };
    for (size_t i = 0; i < sizeof(ADDRS); i++) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ADDRS[i],
            .scl_speed_hz = 400000,
        };
        if (i2c_master_bus_add_device((i2c_master_bus_handle_t)bus, &cfg, &s_dev) != ESP_OK) {
            continue;
        }
        uint8_t id = 0;
        if (rd(REG_WHO_AM_I, &id, 1) == ESP_OK && id == WHO_AM_I) {
            ESP_LOGI(TAG, "QMI8658 at 0x%02x", ADDRS[i]);
            break;
        }
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    if (!s_dev) {
        ESP_LOGW(TAG, "no motion sensor answered");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(wr(REG_CTRL1, 0x40), TAG, "ctrl1");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL2, ACC_4G_125HZ), TAG, "ctrl2");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL7, 0x01), TAG, "ctrl7");
    if (xTaskCreatePinnedToCoreWithCaps(imu_task, "muse_imu", 3072, NULL, 2, NULL, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGW(TAG, "no task for the motion sensor");
        return ESP_ERR_NO_MEM;
    }
    atomic_store(&s_present, true);
    return ESP_OK;
}

void muse_motion(muse_motion_t *out)
{
    out->present = atomic_load(&s_present);
    out->tilt_x = atomic_load(&s_tilt_x);
    out->tilt_y = atomic_load(&s_tilt_y);
    out->shake = atomic_load(&s_shake);
}
