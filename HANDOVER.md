# Handover — 2026-11-09 — Muse Gadget fork for the Waveshare 1.75C

Read this first in a new session. Feature list: [CUSTOMIZATIONS.md](CUSTOMIZATIONS.md).

## TL;DR

- Branch `worktree-call-greeting-delay` (this fork) holds everything; the board runs
  commit `f9c0a69`-era firmware plus the build-config overlay below. It boots with ~29 KiB
  DMA free, connects to the Muse VM in ~1.5 s.
- **Voice guard** works end to end (gate on red only, notice/tone/"Muse explains" refusal,
  calibration). **Don't re-run Calibrate voice** until the relay's self-check is fixed (it
  accepted Vishal's wife). Voice recognition setting is **off by default**.
- **"Hey Vi" wake word** is on the board (setting off by default). First model is weak:
  ~23% misses, ~0.94 false wakes/hour at cutoff 0.98. Next: retrain with more samples and
  real recordings.
- Builds are reproducible from git: `devices/sdkconfig.muse-waveshare-s3-175c-custom` +
  git-ignored `devices/sdkconfig.muse-waveshare-s3-175c.local` (token).

## What was done (by topic)

1. **Calls**: realtime voice, push-to-talk on one button, captions paced to the audio,
   ready beep / hang-up blip, short captions, timing logs. Greeting delay (3–5 s after
   live) is Muse's server; the user chose to leave it.
2. **Face, look, time, audio**: photo avatar + pixel art, touch/idle/motion animations,
   themes, auto-rotate, always-on display (5%), clock (24 h), sleep hours dial, DND,
   speaker boost slider, volume/mic buttons. See CUSTOMIZATIONS.md.
3. **Voice guard** (`components/muse/muse_vg.c`): windows to a relay on the Mac mini
   (`http://192.168.1.214:8792`, fallback `https://vpmacmini.tail94daa8.ts.net/vg`),
   dot, enforcement on red only, settings (Voice recognition, Show notice, Play tone,
   Muse explains), Calibrate voice. "Muse explains" uses standard realtime events
   (system `conversation.item.create` + `response.create`); confirmed working live.
4. **Wake word** (`components/muse/muse_wake.cpp`): microWakeWord on esp-tflite-micro,
   vendored TF microfrontend, all in PSRAM. Model `wake/hey_vi.tflite`.
5. **RAM**: link needs ≥16 KiB DMA-capable free; fixed by moving CPU-only buffers to PSRAM
   and `CONFIG_MUSE_STATIC_BUFFERS_IN_PSRAM=y` for this board.

## Current state

- **Repo**: worktree `…/facebookincubator__muse-gadget-sdk/.claude/worktrees/call-greeting-delay`,
  branch `worktree-call-greeting-delay`, ~36 commits ahead of `main`. Pushed to the user's
  fork (see External context). The worktree may be deleted; the fork branch is the copy.
- **Board**: Waveshare 1.75C on `/dev/cu.usbmodem101`, flashed with the "Hey Vi" build.
- **Outside the repo**:
  - `~/Projects/muse-wakeword/` — generic wake-word training (`setup.sh`,
    `train_wakeword.py`, `wakeword.yaml`, `README.md`), venv `.venv` (Python 3.11),
    shared data `data/` (~26 GB), per-phrase work `work/hey_vi/`, outputs `out/`.
    Not a git repo.
  - Voice guard relay + token on the Mac mini (`~/voice-guardrail/token.txt`).
- **Background jobs**: none.

## Decisions (and why)

- **Gate only on red** (not grey): grey gating made Muse silent and skip its greeting;
  the user chose "always greet and talk when grey". Trade-off: a stranger's first ~3–5 s
  reaches Muse before red.
- **Red after 4 rejects in a row** at 0.5 s windows (~1.5 s), **green on one accept**.
  Considered: require 2 accepts / ≥0.55 to clear red (borderline 0.48–0.52 accepts let
  another voice back in). Not done yet — awaiting the user.
- **Muse explains via standard events**, not a custom `hatch.voice_guard` type (the app's
  parser drops unknown types).
- **Refusal policy on Muse's side**: refuse only on the gadget's NOT-verified item; never
  from `/status`; each call starts out of refusal mode (confirmed by Muse Management).
- **Wake word on-device, custom model** (not ESP-SR stock phrase, not server-side).
  Listens on battery too (user's choice). Model kept `small`.
- **Token** only in git-ignored `.local` / build sdkconfig; never committed.

## Open questions / blockers

1. **Voice guard false accept**: the calibrated print accepted Vishal's wife; Muse
   Management rolled back to the hand-built `vishal_gadget` print (threshold 0.48) and
   added a 0.48 floor. Five models couldn't separate the two voices on this mic. Retest
   with the user + wife on the rolled-back print; consider stricter "clear red".
2. **"Hey Vi" accuracy**: three live detections (scores 254/250/250) all started calls;
   the user hasn't said which were genuine. 2 came 13–15 s after a call ended → maybe add
   a post-call quiet period.
3. **Battery** with wake word / always-on display: not measured.
4. **Tunnel warnings** (`server reset tunnel stream`) seen on the raw tunnel; calls
   unaffected; not investigated.

## Next steps (in order)

1. Ask the user which wake detections were real; add a 5–10 s post-call quiet period if
   any were false.
2. Retrain "Hey Vi": more positives (10k+, more phonetic variants), 40k steps, and add
   50–100 real recordings from the board (add a capture mode). Re-test; swap
   `wake/hey_vi.tflite`, update cutoff in `-custom`.
3. Voice guard: wife/owner test on the rolled-back print; decide "2 accepts or ≥0.55 to
   clear red".
4. Measure battery drain with wake word / AOD on vs off.

## Commands worth knowing

```sh
cd esp32
IDF_EXPORT=~/esp/esp-idf-v6.0.3/export.sh tools/muse/board.sh build s3
IDF_EXPORT=~/esp/esp-idf-v6.0.3/export.sh tools/muse/board.sh flash s3 /dev/cu.usbmodem101
# host tests (macOS needs a cc wrapper that drops -no-pie; VoicePeCallLoop fails on macOS, known)
CJSON_SOURCE_DIR=<espressif cjson component dir> python3 -m unittest tests.test_muse_ptt_ui tests.test_muse_voice_call
# talk to Muse through the board (and its management side chat)
python3 tools/muse/chat.py --port /dev/cu.usbmodem101 "message"   # or --file F, --status
# wake word training (any phrase)
~/Projects/muse-wakeword/.venv/bin/python ~/Projects/muse-wakeword/train_wakeword.py --config ~/Projects/muse-wakeword/wakeword.yaml [--stage data|samples|features|train|export]
```

Gotchas: the sample stage needs `TORCH_FORCE_NO_WEIGHTS_ONLY_LOAD=1` (Piper's pickled
checkpoint; the user runs it). Keep `datasets` 3.x (4+ needs torchcodec; old pyarrow +
TF 2.21 deadlock/abort). Training needs `tensorboard`.

## External context

- **Upstream**: facebookincubator/muse-gadget-sdk (`main`).
- **Fork**: vshalpnjabi/muse-gadget-sdk, branch `worktree-call-greeting-delay`.
- **Muse Management side chat**: reached via `tools/muse/chat.py`; it owns the relay,
  print bank, calibration server and refusal policy.
- **Accounts**: GitHub `vshalpnjabi`; Muse SDK token in the board's NVS; voice guard token
  on the Mac mini. No secrets in this repo.

## Don't forget

- The Mac mini's disk was reported 100% full by Muse — the relay may fail if it stays full.
- Old build folder moved to the job's tmp during wrap-up; the fresh build in
  `esp32/build-muse-waveshare-s3-175c` comes from git config + `.local`.
- `CONFIG_MUSE_VG_TOKEN` lives in `esp32/devices/sdkconfig.muse-waveshare-s3-175c.local`
  (git-ignored) on this machine only.
