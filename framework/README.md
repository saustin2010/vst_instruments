# The build framework

These plugins are built with **[sd88me/mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)**: a hand-written
VST2 wrapper for MPC OS, the adapter that runs Ableton Move "Schwung" modules unchanged, the skin generator that makes
MPC's native touchscreen pages, and the offline test. That code is sd88me's and lives in his repo; this folder holds
only **our changes to it**, as one patch against commit `39660f2b41c0a6d6e9f8c1f0e19378533d9bbc2f`.

```
framework/setup.sh        # clones it into framework/mpc-vst-plugins/ at that commit and applies the patch
```

You only need this to build from source ([BUILDING.md](../BUILDING.md)). Installing the ready-made plugins doesn't.

## The release tools: `saustin2010/mpc-vst-plugins`, branch `steve-features`

The plugins that have their own repos (docs/catalogue-migration.md) are released with sd88me's **current** tools plus 17
changes, kept as commits on the branch `steve-features` of the fork
[saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features) (made 2026-10-08, on his
main at #229). `framework/setup-release.sh` fetches them at the pinned commit; `dev-tools/catalogue/check.sh` builds with
them. Each plugin's `FRAMEWORK.md` lists the ones it uses, what they do there and what to change once sd88me has them
(written by `dev-tools/catalogue/prepare.py`). Each commit is meant to be offered to him as a pull request.

| Commit | Change | Why | Used by |
|---|---|---|---|
| [`b1c39bc`](https://github.com/saustin2010/mpc-vst-plugins/commit/b1c39bc) | A hand-made `params.json` beside a Schwung `module` in `vst.json` | Without it sd88me's tools take the params file alone and don't link the Schwung adapter: the build fails. | 18 of the Schwung instruments (those with their own `params.json`) |
| [`50f459d`](https://github.com/saustin2010/mpc-vst-plugins/commit/50f459d) | The host test runs in Docker on macOS; `TEST_DOCKER_ARGS` | Apple's AddressSanitizer hangs on macOS 26 before the test starts. | building on a Mac |
| [`81f473b`](https://github.com/saustin2010/mpc-vst-plugins/commit/81f473b) | `focus_ring=1`: the touched control highlighted | His default draws no focus ring since 26 September; our screens were checked with it. | all 30 |
| [`01da4b3`](https://github.com/saustin2010/mpc-vst-plugins/commit/01da4b3) | `qlink_box=slot`: Q-Link column outlines by whole slots | His outlines are tighter (knob rings) and count buttons; ours were checked on the Live II. | all 30 |
| [`81a2edd`](https://github.com/saustin2010/mpc-vst-plugins/commit/81a2edd) | `when=<param>:<i>/<N>`: a picture per band of a continuous parameter | Envelope displays drawn per sustain level; without it the build stops. | 8: Aphex, Braids, Denis, Hera, MonkSynth, Moog, Noisemaker, Hank |
| [`6c87b6d`](https://github.com/saustin2010/mpc-vst-plugins/commit/6c87b6d) | `tools_repo` input of the release workflow | Lets a plugin repo build with the fork's tools. | every plugin repo |
| [`8a6ed80`](https://github.com/saustin2010/mpc-vst-plugins/commit/8a6ed80) | Option `values` / `send`; labels matched before numbers; no read past an option list | Engines that parse their own words or values; the overflow crashed Aphex under ASan in his wrapper. | values/send: Eucalypso, MIDI Player, Moog, Super Arp; the fixes: all |
| [`2da5066`](https://github.com/saustin2010/mpc-vst-plugins/commit/2da5066) | `mpc_engine_transport()`: tempo, song position and play state each buffer | Without it the sequencers pass offline but never play in time on the MPC. | 6: Eucalypso, Grids, Groove Bank, MIDI Player, Pixel Walkers, Super Arp |
| [`6303f4e`](https://github.com/saustin2010/mpc-vst-plugins/commit/6303f4e) | Host test: a wheel click on a long whole-number range moves 1/100 of it | The test expected one step per click on a 0-5000 ms range. | the host test |
| [`ea27eda`](https://github.com/saustin2010/mpc-vst-plugins/commit/ea27eda) | `"programs"` `count`, `name_at`, `name` | MPC's PRESET menu without empty slots, and with names. | 12: Braids, Chordism, Hera, MonkSynth, Mono Voice, Moog, Mr Drums, Noisemaker, NuSaw, OB-Xd, Hank, Tablor |
| [`55e60a0`](https://github.com/saustin2010/mpc-vst-plugins/commit/55e60a0) | `"clamped": true` params | Values the engine holds to what it has loaded; the host test skips them. | Groove Bank, MIDI Player |
| [`9595071`](https://github.com/saustin2010/mpc-vst-plugins/commit/9595071) | The release `install.sh` matches the plugin's path as text (`grep -F`) | Our `[SYN]`-style names made it refuse to install (found on the Live II). | all 30 |
| [`10d8f5f`](https://github.com/saustin2010/mpc-vst-plugins/commit/10d8f5f) | `QLINK_TRAVEL` and `SET_IF_CHANGED`, opt-in | Q-Link ticks add up like a detented knob, as our wrapper does; his stepped an option per tick and switches flipped on the Live II. | all 30 |
| [`f210a56`](https://github.com/saustin2010/mpc-vst-plugins/commit/f210a56) | Host test with `QLINK_TRAVEL` | Placeholder params and restore tolerance. | the host test |
| [`94e85dd`](https://github.com/saustin2010/mpc-vst-plugins/commit/94e85dd) | Host test: a preset's option value is what the option sends | An option list like `-`, `0` ... `127` with values -1 ... 127 made the test expect the wrong option. | the host test (both Stevequencers) |
| [`7afa72e`](https://github.com/saustin2010/mpc-vst-plugins/commit/7afa72e) | `"live"` params in `vst.json`: display params the engine moves by itself, reported to MPC after every block | The step light follows playback; without it, it stays where it was when the page was drawn. | 2: Stevequencer, Stevequencer 16 |
| [`7f0653c`](https://github.com/saustin2010/mpc-vst-plugins/commit/7f0653c) | A host set of parameter 0 waits for the next block; a pair that ends where it started is dropped; the host test replays it | On insert and on every STOP, MPC's host (JUCE) sets parameter 0 to the far end and straight back: where that is a preset, every knob moved since went back to it (Live II trace, 2026-10-10). | all: the 7 with a preset or kit there (Fizzik, Helm, Hera, Hush One, NuSaw, Libpo32, Percolator) lost edits on STOP. Offered to sd88me as [#279](https://github.com/sd88me/mpc-vst-plugins/pull/279) (2026-10-11) |

Most are opt-in: they change nothing unless a plugin's own files ask for them (a layout line, a `params.json` or `vst.json`
key, a `defines` entry, the transport hook), so sd88me's own ports build as before. The rest are fixes: the option-list read
past its end, the installer's path match, the host test (macOS, long ranges, travel) and the release workflow's `tools_repo`. `tools/build.sh` still uses the older base and patch below until all 40 build on these.
The pin is `7f0653c` since 2026-10-10 (parameter 0 held until the next block: MPC's STOP toggle); every plugin's
`release.yml` names it, and each one's next release builds with it.
To move the pin: commit on `steve-features`, push it to the fork, set `REF` in `setup-release.sh`, run
`python3 dev-tools/catalogue/prepare.py --ready` (it rewrites each plugin's `release.yml` and `FRAMEWORK.md`), then
`check.sh` the plugins.

## What the patch changes

| Area | Change | Why |
|---|---|---|
| `adapters/schwung/module_params.py` | Reads the newer `module.json` layout (`capabilities.ui_hierarchy`) as well as `chain_params` | Most current Schwung modules only have the newer layout |
| `tools/gen_vst.py`, `tools/params.py`, `tools/studio.py` | Layout before `params.h`; `module` + hand-made `params` together; unique auto-layout tab names; option `values`/`send`; `"effect": true`; `"programs"` (with an optional live `count` and `name_at`); `"category"` (e.g. `"Sequencer"`); per-parameter `dynamic_name` / `dynamic_display` (from newer upstream) | Needed by individual ports (see each plugin's README) |
| `wrapper/vst2_wrap.c`, `wrapper/engine.h` | One engine call at a time per instance (the screen and audio threads never run engine code together); Q-Link turns accumulate on stepped values; option labels matched before numbers; the host's tempo/transport passed to engines that want it (sequencers); audio input for effects; VST programs from a preset parameter (MPC's PRESET menu), with a live count and names for lists that change; names and value text from the engine (`dynamic_name` / `dynamic_display`, as newer upstream); `HAS_TRANSPORT` (play/stop to the engine, from poloq's fork); MIDI CC 20-35 move the first page's Q-Links and NRPN n sets parameter n on any page (sequencer lanes, no MIDI learn); a host set of parameter 0 held until the next block (MPC's STOP toggle, 2026-10-10) | Crashes, Q-Link feel, sequencers, effects and presets found while porting; Mutable Vibe and MPC Plaits; presets reloading on STOP |
| `tools/host_test.c` | Restore, slow Q-Link, effect and program checks; MPC's parameter-0 toggle on STOP replayed with every knob moved (`param0_toggle_check`) | Bugs found on the device, caught offline from then on |
| `wrapper/scope.h` (new) | An opt-in live waveform display (not used by any plugin: animation costs MPC's screen thread too much) | Kept for reference |
| `tools/shadow_skin.py` | Display meters laid out as Akai's own skins; one Q-Link outline per column; switch/slider sizing; `when=` bands of a continuous parameter; `bw=` touch-box width | Skins converted from the Stitch designs |
| `tools/host_test.c`, `tools/test_port.sh` | A restore check (set(get()) must change nothing, ranges in the engine's own units), slow Q-Link checks, effect and program checks; macOS runs the test in a Linux container | Caught silent and broken ports before they reached a device |
| `docs/NOTES.md` | New sections with what was verified while porting (dated) | The framework's record of verified facts |

sd88me will likely bring these ports and changes into his own repo; until then this patch is the way to reproduce the
builds here.
