# Wake word models

microWakeWord streaming models (int8 TFLite) for `CONFIG_MUSE_WAKE_WORD`; the
build embeds the one named by `CONFIG_MUSE_WAKE_MODEL`.

- `hey_vi.tflite` — "Hey Vi" (the default), trained with the pipeline below:
  `small` MixedNet, 51,312 bytes, arena 24,044 bytes, stride 3. First model,
  synthetic voices only; on its held-out int8 streaming test: AUC 0.805,
  cutoff 0.98 → 23% false rejects, 0.94 false accepts/hour; 0.97 → 22%,
  1.50/h; 0.94 → 17–20%, ~2/h. Cutoff 0.98, window 5. To be retrained with
  more samples and real recordings.
- `hey_jarvis.tflite` — "Hey Jarvis", from ESPHome's
  [micro-wake-word-models](https://github.com/esphome/micro-wake-word-models)
  (v2), Apache-2.0. Used to bring up the pipeline. Its settings: cutoff 0.97,
  sliding window 5, tensor arena 22860 bytes.

Custom phrases (e.g. "Hey Vi") are trained with the generic pipeline in
`~/Projects/muse-wakeword` (`train_wakeword.py --config <phrase>.yaml`), which
writes `out/<name>.tflite` and `out/<name>.json`; copy the model here and set
`CONFIG_MUSE_WAKE_MODEL`, `CONFIG_MUSE_WAKE_PHRASE`, `CONFIG_MUSE_WAKE_CUTOFF`,
`CONFIG_MUSE_WAKE_WINDOW` and `CONFIG_MUSE_WAKE_ARENA` from its JSON.
