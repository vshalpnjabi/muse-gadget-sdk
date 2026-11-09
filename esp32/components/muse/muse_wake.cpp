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
 * The wake word (muse_wake.h). The idle mic's PCM is copied into a stream
 * buffer; a low-priority task turns it into microWakeWord's features (the
 * TFLite Micro audio frontend: 40 mel channels, 30 ms windows every 10 ms,
 * noise reduction, PCAN, log scale), quantizes them to int8, and runs the
 * streaming model every `stride` frames. A detection is the mean of the last
 * few probabilities over the cutoff; after one the model's state is reset and
 * it rests a moment. The arena and buffers are in PSRAM, leaving internal RAM
 * (the link's DMA headroom) alone.
 */
#include "muse_wake.h"

#include <atomic>
#include <new>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/micro/schema/schema_generated.h"

extern "C" {
#include "muse_settings.h"
#include "muse_state.h"
#include "muse_voice.h"
}

static const char *TAG = "muse_wake";

extern const uint8_t wake_model_start[] asm("_binary_wake_model_tflite_start");
extern const uint8_t wake_model_end[] asm("_binary_wake_model_tflite_end");

#define RATE 16000
#define STEP_SAMPLES (RATE / 100)          /* 10 ms of audio per feature frame */
#define FEATURES 40
#define BUF_BYTES (RATE * 2 / 2)           /* 0.5 s of mic waiting for the task */
#define COOLDOWN_US 2000000LL              /* after a detection: no second one this soon */
#define WINDOW_MAX 16

static StreamBufferHandle_t s_buf;
static std::atomic<bool> s_ready{false}, s_detected{false};
static tflite::MicroInterpreter *s_interp;
static tflite::MicroResourceVariables *s_vars;
static FrontendState s_frontend;
static int s_stride;

extern "C" bool muse_wake_listening(void)
{
    return s_ready.load() && muse_settings_wake_word();
}

extern "C" void muse_wake_feed(const int16_t *pcm, size_t frames)
{
    if (s_buf && muse_wake_listening()) {
        xStreamBufferSend(s_buf, pcm, frames * sizeof(int16_t), 0);   /* never waits: drops if behind */
    }
}

extern "C" bool muse_wake_take(void)
{
    return s_detected.exchange(false);
}

static bool load_frontend(void)
{
    FrontendConfig c;
    c.window.size_ms = 30;
    c.window.step_size_ms = 10;
    c.filterbank.num_channels = FEATURES;
    c.filterbank.lower_band_limit = 125.0f;
    c.filterbank.upper_band_limit = 7500.0f;
    c.noise_reduction.smoothing_bits = 10;
    c.noise_reduction.even_smoothing = 0.025f;
    c.noise_reduction.odd_smoothing = 0.06f;
    c.noise_reduction.min_signal_remaining = 0.05f;
    c.pcan_gain_control.enable_pcan = 1;
    c.pcan_gain_control.strength = 0.95f;
    c.pcan_gain_control.offset = 80.0f;
    c.pcan_gain_control.gain_bits = 21;
    c.log_scale.enable_log = 1;
    c.log_scale.scale_shift = 6;
    return FrontendPopulateState(&c, &s_frontend, RATE);
}

static bool load_model(void)
{
    const tflite::Model *model = tflite::GetModel(wake_model_start);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGW(TAG, "model schema %lu, expected %d", (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }
    /* The ops microWakeWord's streaming models use. */
    static tflite::MicroMutableOpResolver<20> ops;
    ops.AddCallOnce();
    ops.AddVarHandle();
    ops.AddReshape();
    ops.AddReadVariable();
    ops.AddStridedSlice();
    ops.AddConcatenation();
    ops.AddAssignVariable();
    ops.AddConv2D();
    ops.AddMul();
    ops.AddAdd();
    ops.AddMean();
    ops.AddFullyConnected();
    ops.AddLogistic();
    ops.AddQuantize();
    ops.AddDepthwiseConv2D();
    ops.AddAveragePool2D();
    ops.AddMaxPool2D();
    ops.AddPad();
    ops.AddPack();
    ops.AddSplitV();

    uint8_t *arena = static_cast<uint8_t *>(
        heap_caps_aligned_alloc(16, CONFIG_MUSE_WAKE_ARENA, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!arena) {
        return false;
    }
    tflite::MicroAllocator *alloc = tflite::MicroAllocator::Create(arena, CONFIG_MUSE_WAKE_ARENA);
    s_vars = tflite::MicroResourceVariables::Create(alloc, 20);
    /* The interpreter object in PSRAM too: internal RAM is the DMA headroom. */
    void *mem = heap_caps_malloc(sizeof(tflite::MicroInterpreter), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) {
        return false;
    }
    s_interp = new (mem) tflite::MicroInterpreter(model, ops, alloc, s_vars);
    if (s_interp->AllocateTensors() != kTfLiteOk) {
        ESP_LOGW(TAG, "the model doesn't fit its %d-byte arena", CONFIG_MUSE_WAKE_ARENA);
        return false;
    }
    TfLiteTensor *in = s_interp->input(0);
    if (in->type != kTfLiteInt8 || in->dims->size != 3 || in->dims->data[2] != FEATURES) {
        ESP_LOGW(TAG, "unexpected model input");
        return false;
    }
    s_stride = in->dims->data[1];
    ESP_LOGI(TAG, "wake word \"%s\": model %u bytes, arena %u of %d used, stride %d", CONFIG_MUSE_WAKE_PHRASE,
             (unsigned)(wake_model_end - wake_model_start), (unsigned)s_interp->arena_used_bytes(),
             CONFIG_MUSE_WAKE_ARENA, s_stride);
    return true;
}

static void wake_task(void *arg)
{
    (void)arg;
    int16_t pcm[STEP_SAMPLES];
    size_t have = 0;
    int frames = 0;
    uint8_t probs[WINDOW_MAX] = {0};
    int prob_at = 0;
    const int window = CONFIG_MUSE_WAKE_WINDOW < WINDOW_MAX ? CONFIG_MUSE_WAKE_WINDOW : WINDOW_MAX;
    const int cutoff = CONFIG_MUSE_WAKE_CUTOFF * 255 / 100;
    int64_t quiet_until = 0;
    bool was_listening = false;
    for (;;) {
        bool listening = muse_wake_listening() && !muse_voice_in_call() &&
                         muse_state_mode(NULL) != MUSE_MODE_LISTENING;
        if (!listening) {
            if (was_listening) {
                xStreamBufferReset(s_buf);
                have = 0;
            }
            was_listening = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!was_listening) {
            /* Fresh: no state from before the pause. */
            FrontendReset(&s_frontend);
            s_vars->ResetAll();
            memset(probs, 0, sizeof(probs));
            frames = 0;
            was_listening = true;
        }
        have += xStreamBufferReceive(s_buf, reinterpret_cast<uint8_t *>(pcm) + have * sizeof(int16_t),
                                     (STEP_SAMPLES - have) * sizeof(int16_t), pdMS_TO_TICKS(100)) /
                sizeof(int16_t);
        if (have < STEP_SAMPLES) {
            continue;
        }
        have = 0;
        size_t used = 0;
        FrontendOutput out = FrontendProcessSamples(&s_frontend, pcm, STEP_SAMPLES, &used);
        if (!out.size) {
            continue;
        }
        /* microWakeWord's feature quantization: (value * 256 + 333) / 666 - 128. */
        int8_t *in = s_interp->input(0)->data.int8 + frames * FEATURES;
        for (int i = 0; i < FEATURES; i++) {
            int32_t v = ((int32_t)out.values[i] * 256 + 333) / 666 - 128;
            in[i] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
        }
        if (++frames < s_stride) {
            continue;
        }
        frames = 0;
        if (s_interp->Invoke() != kTfLiteOk) {
            continue;
        }
        probs[prob_at] = s_interp->output(0)->data.uint8[0];
        prob_at = (prob_at + 1) % window;
        int sum = 0;
        for (int i = 0; i < window; i++) {
            sum += probs[i];
        }
        int64_t now = esp_timer_get_time();
        if (sum / window > cutoff && now >= quiet_until) {
            ESP_LOGI(TAG, "wake word heard (mean %d/255, cutoff %d)", sum / window, cutoff);
            s_detected.store(true);
            muse_state_nudge();   /* the voice task may be waiting */
            s_vars->ResetAll();
            memset(probs, 0, sizeof(probs));
            quiet_until = now + COOLDOWN_US;
        }
    }
}

extern "C" void muse_wake_start(void)
{
    s_buf = xStreamBufferCreateWithCaps(BUF_BYTES, sizeof(int16_t) * STEP_SAMPLES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf || !load_frontend() || !load_model()) {
        ESP_LOGW(TAG, "wake word couldn't start");
        return;
    }
    if (xTaskCreatePinnedToCoreWithCaps(wake_task, "muse_wake", 6144, NULL, 3, NULL, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGW(TAG, "wake word task couldn't start");
        return;
    }
    s_ready.store(true);
}
