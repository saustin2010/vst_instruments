# Mr Drums: vendored engine and local changes

`src/` is the Schwung "MrDrums" module (move-anything contributors), module.json version 0.0.4, MIT, copied on
2026-09-29. The original of the patched file is kept as `bak-2026-09-29-deployed/mrdrums_plugin.cpp.orig`.

## Local change: sample kits from folders (2026-10-01, every hunk marked `MPC port`)
`src/dsp/mrdrums_plugin.cpp`. On the Move each pad's sample is chosen in a file browser (`pad_sample_path`, a text
path); a VST parameter on the MPC can't carry text, so the plugin could never load a sample and was silent.
- Every sub-folder of `<module_dir>/kits` (= /sdcard/vst/mrdrums/kits) is a kit, sorted by name; its .wav/.aif/.aiff
  files, sorted by name, go to pads 1-16 (extra files ignored; missing pads left empty).
- New keys: `kit` (set: load kit N; get: the index the loaded pads came from), `kit_count`, `kit_name`.
- The kit is decoded on the calling (UI) thread into a staging set; the audio thread swaps it in at the top of
  `render_block` after `mrdrums_engine_all_notes_off()`, so a sample buffer is never freed while a voice reads it.
  A kit request while the previous one is still waiting to be swapped in is ignored.
- A new instance with no samples loads the first kit.
- Pad paths are stored relative to the module dir (`kits/<kit>/<file>`), so the engine's own state save/restore
  reloads the same samples with a project.

## Local change: no sample freed under a playing voice (2026-10-02, marked `MPC port`)
`src/dsp/mrdrums_plugin.cpp`. Upstream's `set_pad_sample_path` and `clear_pad_sample` freed a pad's sample buffer
while voices could still be playing it, and the next render read the freed memory: a likely crash when a project
loads (its state restore sets every pad's path) or a pad's sample changes while pads ring. Found with
`dev-tools/fuzz` (ASan heap-use-after-free, on one thread as well as two). Both now silence the voices reading that
buffer first (`stop_voices_reading`). The kit loader above was already safe.

## Bundled kit
`src/kits/01_Starter/` holds 8 drum hits synthesised by `dev-tools/skin-redesign/make_starter_kit.py`
(no third-party samples). Add kits by copying folders of WAVs to /sdcard/vst/mrdrums/kits/ (USB or SFTP).
