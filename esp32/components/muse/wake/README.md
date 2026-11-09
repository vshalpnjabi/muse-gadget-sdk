# Wake word models

microWakeWord streaming models (int8 TFLite) for `CONFIG_MUSE_WAKE_WORD`; the
build embeds the one named by `CONFIG_MUSE_WAKE_MODEL`.

- `hey_jarvis.tflite` — "Hey Jarvis", from ESPHome's
  [micro-wake-word-models](https://github.com/esphome/micro-wake-word-models)
  (v2), Apache-2.0. Used to bring up the pipeline. Its settings: cutoff 0.97,
  sliding window 5, tensor arena 22860 bytes.

Custom phrases (e.g. "Hey Vi") are trained with the generic pipeline in
`~/Projects/muse-wakeword` (`train_wakeword.py --config <phrase>.yaml`), which
writes `out/<name>.tflite` and `out/<name>.json`; copy the model here and set
`CONFIG_MUSE_WAKE_MODEL`, `CONFIG_MUSE_WAKE_PHRASE`, `CONFIG_MUSE_WAKE_CUTOFF`,
`CONFIG_MUSE_WAKE_WINDOW` and `CONFIG_MUSE_WAKE_ARENA` from its JSON.
