<!--
Copyright (c) Meta Platforms, Inc. and affiliates.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Muse Gadget — Waveshare 1.75C fork

A personal fork of [facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk)
that turns a **Waveshare ESP32-S3-Touch-AMOLED-1.75C** into a richer Muse companion:
live voice calls with push-to-talk on its one button, a living photo of Muse as the face,
themes, a clock with sleep hours and do-not-disturb, motion reactions, an always-on
display, speaker verification (voice guard) and an on-device **"Hey Vi"** wake word.

**[CUSTOMIZATIONS.md](CUSTOMIZATIONS.md)** lists every feature and change on top of `main`,
with where it lives and how it's configured. **[HANDOVER.md](HANDOVER.md)** is the
working state: what's done, what's open, and how to pick it up.

## Highlights

- **Calls**: hands-free or hold-to-talk on one button; captions in step with Muse's voice;
  a ready beep and a soft hang-up blip.
- **Face**: Muse's photo from the app, alive (blinks, lip sync, touch reactions with sounds,
  tilt and shake reactions), or the pixel-art avatar.
- **Look**: themes (including the Muse app's own look), auto-rotate, brightness,
  always-on display at 5%.
- **Settings > Hardware**: everything about the board, live.
- **Generic**: the companion's and owner's names, the avatar folder and the voice guard's
  relay are settings; the features build on other boards (`MUSE_FEATURES=1`, see
  [CUSTOMIZATIONS.md](CUSTOMIZATIONS.md#11-other-boards)).
- **Time**: clock (24-hour by default), sleep hours on one dial, do not disturb; Muse can
  set them for you.
- **Voice guard**: checks a call's voice is the owner's via a verification relay; only an
  unrecognized voice is held back, with an optional notice, tone and a polite spoken refusal
  from Muse; on-device voice calibration.
- **Wake word**: "Hey Vi" on the board (microWakeWord on TensorFlow Lite Micro, 51 KB model),
  trained with a generic pipeline for any phrase.

## Build and flash (1.75C)

```sh
cd esp32
# Tokens, the voice guard's relay and your avatar folder (git-ignored)
cp devices/sdkconfig.muse-waveshare-s3-175c.local.example devices/sdkconfig.muse-waveshare-s3-175c.local
IDF_EXPORT=~/esp/esp-idf-v6.0.3/export.sh tools/muse/board.sh build s3
IDF_EXPORT=~/esp/esp-idf-v6.0.3/export.sh tools/muse/board.sh flash s3 /dev/cu.usbmodem101
```

ESP-IDF v6.0.3. The board's fork options are in
[`esp32/devices/sdkconfig.muse-waveshare-s3-175c-custom`](esp32/devices/sdkconfig.muse-waveshare-s3-175c-custom);
see [`esp32/README.md`](esp32/README.md) for the SDK itself, pairing and other boards.

---

## Upstream: Muse Gadgets

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".github/images/muse-gadgets-dark.png">
    <img src=".github/images/muse-gadgets-light.png" width="900" alt="Muse gadgets: a Waveshare round AMOLED, an M5Stack StickS3, Muse Home Link, a Raspberry Pi and a Seeed reTerminal e-ink display">
  </picture>
</p>

Muse gadgets are open source devices you build yourself. Program an
off-the-shelf ESP32 board or set up a Raspberry Pi with our device SDKs, then
connect Muse to your displays, buttons, sensors, actuators, and whatever else
you've got lying on your workbench.

We open sourced the SDKs and firmware here. It's built by hackers, for hackers,
just for fun. Side effects of tinkering may include bricked boards, voided
warranties, brownouts, or bankruptcies. Proceed at your own risk!

| | |
|---|---|
| [**ESP32 Device SDK**](esp32) | Connect your ESP32 board to Muse through our open source SDK. Throw in a screen to show images, add audio in and out, or wire up other sensors. |
| [**Linux Device SDK**](linux) | Turn that spare Raspberry Pi or Linux box into a Muse gadget. Hack in your own commands to let Muse handle sysadmin chores or your Home Assistant setup. |
| [**Muse Gadgets Test**](android/musegadgetstest) ([iPhone](ios/musegadgetstest)) | Stream your phone's camera, or a clip, to an ESP32 gadget's screen over Wi-Fi: H.264 or VP8 decoded on the board, 30 fps on the round 466 px boards. [`protocols/VIDEO.md`](protocols/VIDEO.md) explains how. |

Before you flash or pair a gadget, get an
[SDK token](https://gadgets.muse.ai/settings/sdk-tokens) and review the
[Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms). Every gadget needs a
token to pair.

ESP32 and Linux gadgets pair with the Muse app on iOS and Android, via
Settings > Devices. Turn on Developer mode there first, then look for devices
prefixed with "MuseGadget".
Each directory has a `README.md` to get started and an `AGENTS.md` for coding
agents like [Muse Code](https://developer.meta.com/ai/lp/muse-code/).

For battery-input power profiling, see the [porting recipe](esp32/docs/power/README.md).
The opt-in diagnostic core is board-neutral; boards supply its hooks and state
matrices, and [power_sweep.py](esp32/tools/power/power_sweep.py) drives the
USB-free sweep. The isolated diagnostic does not pair or need an SDK token;
fixture safety and explicit hardware authorization still come first, as the
[PPK2 rules](esp32/docs/power/ppk2.md) describe.

### Community

Meet other hackers who are building and customizing Muse gadgets in our
community [Discord](https://discord.gg/3bhjCkZdd6). Get inspired, support each
other, and share what you make.

## License

Muse Gadgets is licensed under the Apache License, Version 2.0, found in
[`LICENSE`](LICENSE), except for these third-party files, which keep their
upstream licenses:

| Path | Upstream | License |
|---|---|---|
| [`esp32/components/minimp3/include/minimp3.h`](esp32/components/minimp3) | [lieff/minimp3](https://github.com/lieff/minimp3) | CC0-1.0, see [`LICENSE`](esp32/components/minimp3/LICENSE) |
| [`esp32/components/muse/boards/ostb_echoear_display_init.h`](esp32/components/muse/boards/ostb_echoear_display_init.h) | [78/xiaozhi-esp32 ESP-VoCat panel sequence](https://github.com/78/xiaozhi-esp32/blob/0d576d3d4c049c6f55eaf879725dc23e516511b4/main/boards/espressif/esp-vocat/esp_vocat.cc) | MIT, retained in the file header |
| JD9853A initialization sequence in [`board_seeed_xiao_s3_147.c`](esp32/components/muse/boards/board_seeed_xiao_s3_147.c) | [Seeed-Studio/Seeed_GFX2](https://github.com/Seeed-Studio/Seeed_GFX2) | MIT, see [`Seeed_GFX2.txt`](esp32/devices/licenses/Seeed_GFX2.txt) |
| [`esp32/components/muse/boards/viewe_smartring_display_init.h`](esp32/components/muse/boards/viewe_smartring_display_init.h) | [VIEWESMART/smartring_plus](https://github.com/VIEWESMART/smartring_plus) panel sequence | Apache-2.0 under VIEWE's copyright, in the file header |
| [`esp32/main/pixel_font.c`](esp32/main/pixel_font.c) | Adafruit GFX `glcdfont.c` | BSD-2-Clause, in the file header |
| [`esp32/components/muse/fonts/`](esp32/components/muse/fonts) | GNU Unifont 16.0.04, Noto Sans SC, and Montserrat Medium via LVGL, subsets (see its README) | SIL Open Font License 1.1 (Unifont also GPL-2.0+ with the font embedding exception) |
| [`esp32/components/muse/eink/fonts/montserrat.ttf`](esp32/components/muse/eink/fonts) | Montserrat Medium, via LVGL v9.5.0 | SIL Open Font License 1.1 |
| [`esp32/components/muse/eink/fonts/unscii.ttf`](esp32/components/muse/eink/fonts) | unscii-8 | Public domain |
| [`esp32/components/muse/eink/fonts/symbols.ttf`](esp32/components/muse/eink/fonts) | Font Awesome 5 Free, via LVGL v9.5.0 | SIL Open Font License 1.1 (icons CC BY 4.0) |
| [`esp32/main/epd_ssd1677.c`](esp32/main/epd_ssd1677.c) | Seeed-reTerminal-E10xx-Firmware's SSD1677 refresh sequence | MIT, retained in the file header |
| [`esp32/components/tinyh264/src`](esp32/components/tinyh264) | the Android Open Source Project's H.264 decoder, via [udevbe/tinyh264](https://github.com/udevbe/tinyh264) | Apache-2.0, AOSP's copyright, see [`LICENSE`](esp32/components/tinyh264/LICENSE) |
| [`esp32/components/libvpx_vp8/src`](esp32/components/libvpx_vp8) | [libvpx](https://chromium.googlesource.com/webm/libvpx) v1.16.0's VP8 decoder, trimmed | BSD-3-Clause, the WebM Project authors' copyright, see [`LICENSE`](esp32/components/libvpx_vp8/LICENSE), with Google's patent grant in [`PATENTS`](esp32/components/libvpx_vp8/PATENTS) |
| [`esp32/components/muse/wake/microfrontend`](esp32/components/muse/wake/microfrontend) | TensorFlow Lite Micro's audio microfrontend, with KissFFT | Apache-2.0 (TensorFlow, see [`LICENSE`](esp32/components/muse/wake/microfrontend/LICENSE)); KissFFT BSD-3-Clause, in its headers |
| [`esp32/components/muse/wake/hey_jarvis.tflite`](esp32/components/muse/wake) | ESPHome [micro-wake-word-models](https://github.com/esphome/micro-wake-word-models) | Apache-2.0 |
| [`esp32/components/muse/wake/hey_vi.tflite`](esp32/components/muse/wake) | Trained here with [microWakeWord](https://github.com/kahrendt/microWakeWord) on Piper TTS samples and AudioSet / FMA / MIT RIR / microWakeWord negative data | Mixed dataset licences: personal, non-commercial use |

Dependencies fetched at build time are under their own licenses: ESP-IDF
components (into `esp32/managed_components/`), and the simulator's LVGL and
SDL (listed in [`esp32/simulator/THIRD_PARTY.md`](esp32/simulator/THIRD_PARTY.md)).

The Apache License does not cover the [Jollybot avatar](esp32/avatar).

Muse Gadgets Test's sample clips aren't in this repository: the app fetches a few
seconds of the Blender Foundation's open movies (CC BY 3.0) when one is
played, and shows their credits.

Muse Gadgets Test can use x264 (GPL-2.0) on the phone; this repository carries none
of its code. An APK or iPhone app you build with it is under the GPL
([`android/musegadgetstest/README.md`](android/musegadgetstest/README.md),
[`ios/musegadgetstest/README.md`](ios/musegadgetstest/README.md)).
