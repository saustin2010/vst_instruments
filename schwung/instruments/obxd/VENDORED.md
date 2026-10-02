# OB-Xd: vendored engine and local changes

`src/` is the Schwung OB-Xd module (reales' OB-Xd, ported by charlesvestal), module.json version 0.4.9, GPL-3.0,
copied as it was on 2026-09-29 from the Schwung module catalogue. One local change, marked `MPC port`:

- `src/dsp/obxd_plugin.cpp`, `v2_init_default_patch()`: calls `synth->processBrightness(1.0f)` (and stores 1.0).
  The Init patch `memset`s every parameter to 0 and never calls `processBrightness`, so the voices' brightness filter
  stays uninitialised and the fallback patch (used when no preset bank is found) is silent. Added 2026-09-30.

Everything else that made OB-Xd work on the MPC is port configuration, not engine code: `params.json` (native 0-100
ranges), `vst.json` (`MODULE_DIR` = /sdcard/vst/obxd, where `presets/factory.fxb` is shipped).
