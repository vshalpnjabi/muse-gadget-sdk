/*
 * Forced into the microfrontend's sources (CMakeLists.txt): their tables
 * (FFT, filterbank, window, noise estimate) go to PSRAM, not the internal
 * RAM the link and I2S need for DMA. Only the wake task uses them.
 */
#pragma once

#include <stdlib.h>

#include "esp_heap_caps.h"

#define malloc(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define calloc(n, s) heap_caps_calloc((n), (s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define realloc(p, n) heap_caps_realloc((p), (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define free(p) heap_caps_free(p)
