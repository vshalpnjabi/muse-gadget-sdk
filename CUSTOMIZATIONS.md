# Customizations on top of `main`

Everything this fork adds to [facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk),
built and tested on a **Waveshare ESP32-S3-Touch-AMOLED-1.75C** (466×466 round AMOLED,
one physical button, QMI8658 motion sensor, ES8311/ES7210 audio). Almost all code is in
`esp32/components/muse/`; the board's build switches are in
[`esp32/devices/sdkconfig.muse-waveshare-s3-175c-custom`](esp32/devices/sdkconfig.muse-waveshare-s3-175c-custom).

Most features are behind Kconfig options that default **off** upstream, so other boards
are unaffected; the `-custom` overlay turns them on for the 1.75C.

---

## 1. Voice calls

| Feature | Details | Where |
|---|---|---|
| Live calls with Muse | Hands-free realtime voice calls; Muse greets first. | `CONFIG_MUSE_REALTIME_VOICE`, `_GREETING` |
| Push-to-talk on one button | Hold to talk in a live call, release to listen; a tap starts a hands-free call or hangs up. "PUSH TO TALK" shown once at the bottom. | `CONFIG_MUSE_PTT_CALLS`, `muse_voice.c` |
| Captions in step with speech | Reply text arrives ahead of the audio; each caption is held until its words reach the speaker (+0.2 s). | `muse_voice_call.cpp` (`muse_voice_call_caption_due`) |
| Ready beep | A short rising chirp when a call goes live and when a voice note starts recording. Silent with the speaker off or in do-not-disturb. | `muse_voice.c` (`ready_beep`) |
| Hang-up blip | A soft, low 70 ms tone (about a third as loud) when a call ends. | `muse_audio.c` (`muse_audio_bye`) |
| Short captions | `CONNECTING...`, `IN CALL`; `CALL ENDED` clears after ~5 s. | `muse_voice.c` |
| Call timing logs | `first reply audio …`, `first audible reply … (… after it was live)`, uplink totals per call. | `muse_voice_call.cpp` |

Measured: the board sends the call request ~10–30 ms after a tap; Muse's server takes
~1–2 s to accept and go live; its spoken greeting comes 3–5 s after that (server-side).

## 2. Audio

| Feature | Details | Where |
|---|---|---|
| Speaker boost | Settings > Sound > Speaker boost slider, 0–24 dB (ceiling `CONFIG_MUSE_SPEAKER_MAX_DB`, 12 here). | `muse_audio.c`, `muse_settings*.c` |
| Volume button | Tap steps volume by 10 points; long press mutes. Plain icon. | `muse_ui.c` |
| Mic button | A mute toggle. | `muse_ui.c`, `muse_audio.c` |

## 3. The face (avatar)

| Feature | Details | Where |
|---|---|---|
| Photo avatar | Muse's picture from the Muse app on black, with hair cleaned, as a living face: blinks, glances, lip sync, thinking/working looks. | `muse_photo.c`, `tools/muse/photo_avatar.py` |
| Avatar type | Settings > Appearance > Avatar type: **Original** (photo, default) or **Pixel art** (refined palette/hair). | `muse_settings_ui.c` |
| Touch reactions | By where you touch: both winks, hearts, sparkles, giggles, boings…, with sounds. | `muse_photo.c`, `CONFIG_MUSE_UI_TOUCH_SOUNDS` |
| Idle life | Varied idle animations; sleeping (z's) during sleep hours. | `muse_photo.c` |
| Motion reactions | Tilt: looks toward the low side; shake: a head-only shake with motion lines, dizzy. | `muse_imu.c`, `CONFIG_MUSE_IMU` |
| Fixed size under captions | The avatar keeps its size; small captions in one bottom band. | `CONFIG_MUSE_UI_FIXED_AVATAR`, `muse_ui.c` |
| Captions setting | Settings > Appearance > Captions on/off. | `muse_settings*.c` |
| Cleaner home screen | No READY text, mode names, ring or power icon; battery as a bare percentage. | `muse_ui.c` |

## 4. Display and look

| Feature | Details | Where |
|---|---|---|
| Themes | Settings > Appearance > Theme: **Muse** (matches Vi in the Muse app), Purple, Blue, Green, Orange, Pink, Mono; icons and settings follow. | `muse_theme.c` |
| Brightness | In Appearance as well as Sound. | `muse_settings_ui.c` |
| Auto-rotate | Settings > Appearance > Auto-rotate: the screen turns with gravity (IMU). | `muse_ui.c`, `boards/muse_lcd_bands.c` |
| Always-on display | Settings > Appearance > Always-on display (off by default): when idle the screen stays on at **5%** with the face and clock instead of going dark; the first tap only wakes it. | `muse_ui.c` (`update_sleep`) |
| Button sleep | A press of the bottom button (BOOT) puts it to sleep so that only a button wakes it: touch, shaking, images from Muse and the wake word don't; the always-on display goes fully dark and the wake word stops listening. Auto-sleep is unchanged; pairing prompts still wake it. | `muse_state.c` (`muse_state_hold_asleep`), `muse_input.c` |

## 5. Clock, sleep hours, do not disturb

| Feature | Details | Where |
|---|---|---|
| Clock | Time on the home screen (SNTP once Wi-Fi is up, POSIX TZ). | `muse_clock.c`, `CONFIG_MUSE_CLOCK`, `CONFIG_MUSE_TIMEZONE` |
| 24-hour clock | Settings > Appearance > 24-hour clock (on by default). | `muse_settings*.c` |
| Sleep hours | One 24-hour dial (Settings > Sleep): Muse sleeps on the face, quiet, screen dim. | `muse_settings_ui.c` |
| Do not disturb | The board's own sounds off. | `muse_clock.c` |
| Muse commands | Muse can set them: `device.dnd`, `device.sleep_hours` (with time zone). | `main/app.c`, `main/noise_control.cpp` |

## 6. Voice guard (speaker verification)

Calls can check that the voice is the owner's, with verification done on a relay
(the owner's Mac mini; LAN base first, HTTPS fallback validated against the CA bundle).

| Feature | Details | Where |
|---|---|---|
| Voice recognition setting | Settings > Sound > **Voice recognition**, off by default: no dot, nothing sent. | `muse_vg.c`, `CONFIG_MUSE_VOICE_GUARD` |
| Windows | Last 3 s of the mic audio Muse gets (16 kHz int16), every 0.5 s, only with ≥1.2 s of speech and not while Muse talks. | `muse_vg.c` |
| Dot | Grey = no verdict / idle; green = verified; red = not recognized. Holds its colour, blinks on each verdict. | `muse_ui.c` |
| Enforcement | Only **red** (4 rejects in a row, ~1.5 s) holds the mic back from Muse; grey and green flow. Back to green on one accept; Muse hears from the accepted window's start. | `CONFIG_MUSE_VG_ENFORCE`, `muse_voice.c` |
| When not recognized | Settings: Show notice ("VOICE NOT RECOGNIZED"), Play tone, **Muse explains** (a system `conversation.item.create` + `response.create`, so Muse speaks one polite refusal; a "verified again" item ends refusal mode). | `muse_voice_call.cpp` (`send_guard_note`) |
| Calibrate voice | Settings > Sound > Calibrate voice: read a 9-sentence script (~48 s); windows go to `/window?…&calibrate=<id>`, then `/calibrate/<id>/finish`, polled; shows the new threshold or the reason. | `muse_settings_ui.c`, `muse_vg.c` |

Server contract and B2 refusal policy live with the relay (Muse Management side chat).
The token is never committed: `devices/sdkconfig.muse-waveshare-s3-175c.local`.

## 7. Wake word ("Hey Vi")

| Feature | Details | Where |
|---|---|---|
| Wake word setting | Settings > Sound > Wake word (off by default): saying the phrase wakes the screen and starts a hands-free call; listens asleep and on battery. | `muse_wake.cpp`, `CONFIG_MUSE_WAKE_WORD` |
| Engine | microWakeWord streaming int8 model on TensorFlow Lite Micro (`espressif/esp-tflite-micro`), TensorFlow's microfrontend features (vendored, Apache-2.0); arena, tables, interpreter and task in PSRAM (~300 B internal RAM). | `muse_wake.cpp`, `wake/microfrontend/` |
| Model | `wake/hey_vi.tflite`, 51,312 bytes, arena 24,044 B, cutoff 0.98, window 5. First model (synthetic voices): AUC 0.805; at 0.98 → 23% misses, 0.94 false wakes/hour. `hey_jarvis.tflite` kept as a reference. | `wake/README.md` |
| Training pipeline | Generic, any phrase from a YAML config: [vshalpnjabi/muse-wakeword](https://github.com/vshalpnjabi/muse-wakeword) (private; `train_wakeword.py --config <phrase>.yaml`), checked out at `~/Projects/muse-wakeword`. | — |

## 8. Memory (internal RAM)

The 1.75C's link needs ≥16 KiB of DMA-capable free RAM; features had pushed it below
(link stuck "Connecting", I2S boot crashes). Fixes: the voice guard's and call's CPU-only
buffers in PSRAM (`EXT_RAM_BSS_ATTR`), `CONFIG_MUSE_STATIC_BUFFERS_IN_PSRAM` on for this
board (~18 KiB), the wake word entirely in PSRAM. Boots with ~29 KiB DMA free.

## 9. Fixes along the way

- Boot crash loop with the ring removed (`NO_RING` null guard).
- Registration never sent: Wi-Fi static buffers 6, BA window 12 (DMA total ≥ 16 KiB).
- Image fetch via Muse corrupted: per-chunk sha256 verification.
- Voice guard crash going red → green (caption read with a NULL version).
- A new call no longer inherits the previous call's voice-guard note.

## 10. Tools and tests

- `tools/muse/photo_avatar.py`: builds the photo avatar (patches for eyes/mouth, preview).
- `docs/avatar-video.md`: notes on a video-avatar option.
- Host tests extended: `tests/test_muse_ptt_ui.py`, PTT frame/voice/input harnesses;
  `tests/voice_call_fakes/esp_attr.h`. Known pre-existing failure on macOS:
  `VoicePeCallLoop` (its harness doesn't compile there).

## 11. Building this fork

```sh
cd esp32
cp devices/sdkconfig.muse-waveshare-s3-175c.local.example devices/sdkconfig.muse-waveshare-s3-175c.local   # add the token
IDF_EXPORT=~/esp/esp-idf-v6.0.3/export.sh tools/muse/board.sh build s3
IDF_EXPORT=~/esp/esp-idf-v6.0.3/export.sh tools/muse/board.sh flash s3 /dev/cu.usbmodem101
```

`board.sh` applies, in order: `sdkconfig.defaults`, `devices/sdkconfig.muse`,
`devices/sdkconfig.muse-waveshare-s3-175c`, then this fork's `-custom` overlay and the
git-ignored `.local` secrets (the Muse SDK token and the voice guard token; see
`.local.example`).

### Versions and release notes

[`RELEASE_NOTES.md`](RELEASE_NOTES.md) lists each release, newest first. Its top
`## x.y.z` heading is the firmware's version: **Settings > Version** shows
`x.y.z - <commit>` and opens the commit, build time and the notes (a `+` after the commit
means it was built with uncommitted changes). To release: add a `## x.y.z — date`
section at the top, commit, then build and flash.

### Safeguards

- **Config lock**: `devices/sdkconfig.muse-waveshare-s3-175c.lock` pins all 2,451 resolved
  settings of the known-good build (secrets left out). `board.sh build` fails and lists any
  setting that drifts from it, so a dropped option (a ring, a label, a Wi-Fi buffer) can't
  reach the board unnoticed. After a deliberate change:
  `python3 tools/muse/config_lock.py update build-muse-waveshare-s3-175c/sdkconfig devices/sdkconfig.muse-waveshare-s3-175c.lock`,
  then commit the lock.
- **Flash backups**: every successful `board.sh flash` saves the exact images, the build's
  sdkconfig, `BUILD_INFO` (commit, time) and a `flash.sh` to
  `esp32/firmware-backups/<profile>/<date>/` (the day's last flash; the newest 7 kept;
  `latest` links the newest). Restore: `esp32/firmware-backups/waveshare-s3-175c/latest/flash.sh /dev/cu.usbmodem101`.
  Git-ignored: the images carry the tokens.
