# The build framework

These plugins are built with **[sd88me/mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)**: a hand-written
VST2 wrapper for MPC OS, the adapter that runs Ableton Move "Schwung" modules unchanged, the skin generator that makes
MPC's native touchscreen pages, and the offline test. That code is sd88me's and lives in his repo; this folder holds
only **our changes to it**, as one patch against commit `39660f2b41c0a6d6e9f8c1f0e19378533d9bbc2f`.

```
framework/setup.sh        # clones it into framework/mpc-vst-plugins/ at that commit and applies the patch
```

You only need this to build from source ([BUILDING.md](../BUILDING.md)). Installing the ready-made plugins doesn't.

## What the patch changes

| Area | Change | Why |
|---|---|---|
| `adapters/schwung/module_params.py` | Reads the newer `module.json` layout (`capabilities.ui_hierarchy`) as well as `chain_params` | Most current Schwung modules only have the newer layout |
| `tools/gen_vst.py`, `tools/params.py`, `tools/studio.py` | Layout before `params.h`; `module` + hand-made `params` together; unique auto-layout tab names; option `values`/`send`; `"effect": true`; `"programs"` (with an optional live `count` and `name_at`); `"category"` (e.g. `"Sequencer"`); per-parameter `dynamic_name` / `dynamic_display` (from newer upstream) | Needed by individual ports (see each plugin's README) |
| `wrapper/vst2_wrap.c`, `wrapper/engine.h` | One engine call at a time per instance (the screen and audio threads never run engine code together); Q-Link turns accumulate on stepped values; option labels matched before numbers; the host's tempo/transport passed to engines that want it (sequencers); audio input for effects; VST programs from a preset parameter (MPC's PRESET menu), with a live count and names for lists that change; names and value text from the engine (`dynamic_name` / `dynamic_display`, as newer upstream); `HAS_TRANSPORT` (play/stop to the engine, from poloq's fork) | Crashes, Q-Link feel, sequencers, effects and presets found while porting; Mutable Vibe and MPC Plaits |
| `wrapper/scope.h` (new) | An opt-in live waveform display (not used by any plugin: animation costs MPC's screen thread too much) | Kept for reference |
| `tools/shadow_skin.py` | Display meters laid out as Akai's own skins; one Q-Link outline per column; switch/slider sizing; `when=` bands of a continuous parameter; `bw=` touch-box width | Skins converted from the Stitch designs |
| `tools/host_test.c`, `tools/test_port.sh` | A restore check (set(get()) must change nothing, ranges in the engine's own units), slow Q-Link checks, effect and program checks; macOS runs the test in a Linux container | Caught silent and broken ports before they reached a device |
| `docs/NOTES.md` | New sections with what was verified while porting (dated) | The framework's record of verified facts |

sd88me will likely bring these ports and changes into his own repo; until then this patch is the way to reproduce the
builds here.
