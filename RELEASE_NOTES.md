# Release notes

The firmware shows the top version below (Settings > Version), with its commit and
build time. Add a new section at the top for each release.

## 1.1.0 — 2026-11-09

The fork's features work on other boards; names, avatar and relay are settings.

- Settings > Hardware: board, chip, CPU, radio, MAC, flash, PSRAM, free RAM, screen,
  touch, audio, motion sensor, buttons, power, uptime, ESP-IDF and what the build has.
- Brightness lives in Appearance only (Sound keeps it on boards without audio).
- Names: the companion's (`CONFIG_MUSE_NAME`, here "Vi") in Settings, and the owner's
  (`CONFIG_MUSE_OWNER_NAME`) in the voice guard's spoken refusals.
- Avatar: your avatar files can live in any folder (`CONFIG_MUSE_AVATAR_DIR`); a photo
  face's touch zones and head shake fit that face (`photo_avatar.py --eyes --mouth --chin`).
- Other boards: the motion sensor on every board with a QMI8658 (1.75/1.75C, 1.43, 1.46,
  1.8, 2.06, 2.16, LCD-1.28); wake word, voice guard and the rest on any board with PSRAM
  (`MUSE_FEATURES=1 tools/muse/board.sh build <board>`).
- Voice guard: its relay addresses and speaker are per-machine settings (`.local`), and it
  stays off until token, speaker and an address are set.

## 1.0.0 — 2026-11-09

First release of the Waveshare 1.75C fork.

- Calls: live voice calls; hold the top button to talk; a tap starts or ends a call.
- Calls: captions in step with Muse's voice; a beep when Muse is ready; a soft blip at hang-up.
- Face: Muse's photo from the app, alive; or the pixel-art avatar (Avatar type).
- Face: touch reactions with sounds; tilt and shake reactions; idle animations.
- Look: themes, auto-rotate, brightness, always-on display at 5%.
- Home screen: no READY text, mode names, ring or power icon; battery as a percentage.
- Time: clock (24-hour), sleep hours on one dial, do not disturb.
- Sound: speaker boost slider; volume taps by 10; the mic button mutes.
- Voice recognition (off by default): only an unrecognized voice is held back; optional notice, tone and Muse's spoken refusal; Calibrate voice.
- Wake word "Hey Vi" (off by default): starts a call hands-free.
- Bottom button: sleep that only a button can wake.
- Reliability: more free memory for the Muse link; a config lock on every build; daily flash backups.
