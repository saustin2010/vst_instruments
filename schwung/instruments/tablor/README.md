# Tablor

**Synth** · A two-oscillator wavetable synth that ships with its wavetables and 9 factory presets. · maker in MPC: athousanddetails · licence: BSD-3-Clause

<img src="screenshots/page_0.png" width="760" alt="Tablor on the MPC touchscreen">

Two wavetable oscillators, each scanning its own table (the table's name shows next to it, and the arrows step through the library), with unison and shape controls, a sub and noise, a filter, amp and filter envelopes, two modulation envelopes and a voice section. Its wavetable library ships with it; add your own tables to /sdcard/vst/tablor/wavetables on the MPC.

Its 9 factory presets (Init, First Contact, Neu Bass, Formant Keys, Dust Pad, Sub Punch, Glass Bells, Res Bass, E Piano) are in MPC's PRESET menu and on the VOICE page's PRESET selector (2026-10-04).

## On the MPC

- In the plugin browser: **[SYN] Tablor** by **athousanddetails** (Synth)
- Files: the release installs one folder, `/sdcard/Synths/athousanddetails - VST - [SYN] Tablor/`, holding `tablor.so`, its screen and its library (`tablor/`). The vst_instruments installer puts `tablor.so` in `/sdcard/vst/` and its data in `/sdcard/vst/tablor/` instead; the plugin finds them either way (its data folder is `tablor/` next to the `.so`).
- 69 parameters (all automatable) on 6 pages

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAIN

<img src="screenshots/page_0.png" width="760" alt="Tablor, page MAIN">

Q-Link columns: **1** WT1 TABLE  ·  **2** WT1 POSITION, WT1 LEVEL, WT1 TUNE, WT1 UNISON  ·  **3** WT2 TABLE  ·  **4** WT2 POSITION, WT2 LEVEL, WT2 TUNE, WT2 UNISON

### 2. FILTER

<img src="screenshots/page_1.png" width="760" alt="Tablor, page FILTER">

Q-Link columns: **1** CUTOFF, RESONANCE, FILTER TYPE  ·  **2** FILTER ENV, KEY TRACK, VEL TRACK  ·  **3** SUB LEVEL, SUB WAVE, SUB TUNE  ·  **4** NOISE LEVEL, NOISE TYPE

### 3. SHAPE

<img src="screenshots/page_2.png" width="760" alt="Tablor, page SHAPE">

Q-Link columns: **1** WT1 DETUNE, WT1 SPREAD, WT1 PAN  ·  **2** WT2 DETUNE, WT2 SPREAD, WT2 PAN  ·  **3** WT1 BEND, WT1 FORMANT  ·  **4** WT2 BEND, WT2 FORMANT

### 4. ENVELOPES

<img src="screenshots/page_3.png" width="760" alt="Tablor, page ENVELOPES">

Q-Link columns: **1** VCA ATTACK, VCA DECAY, VCA SUSTAIN, VCA RELEASE  ·  **2** VELOCITY  ·  **3** FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE

### 5. MOD ENVS

<img src="screenshots/page_4.png" width="760" alt="Tablor, page MOD ENVS">

Q-Link columns: **1** EG1 A, EG1 D, EG1 S, EG1 R  ·  **2** EG1 DST, EG1 AMT  ·  **3** EG2 A, EG2 D, EG2 S, EG2 R  ·  **4** EG2 DST, EG2 AMT

### 6. VOICE

<img src="screenshots/page_5.png" width="760" alt="Tablor, page VOICE">

Q-Link columns: **1** VOICE MODE, VOICES, LEGATO  ·  **2** GLIDE, GLIDE MODE  ·  **3** BEND RANGE, VOLUME  ·  **4** PRESET

## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `SYN-Tablor-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/mpc-vst-tablor/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.
Your own files in `tablor/wavetables/` are kept when you update or uninstall.

**With the rest of the collection:** from [vst_instruments](https://github.com/saustin2010/vst_instruments) ([INSTALL.md](https://github.com/saustin2010/vst_instruments/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> tablor
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.

## Where it comes from

- Upstream: https://github.com/athousanddetails/schwung-tablor
- Vendored at commit d51887187f7a03f7b32cc4a74f5c60c5532e65fa 2026-09-03
- Schwung module "Tablor" v1.3.3 by athousanddetails; after Wavetable (Roland Rabien / FigBug, BSD-3)
- Licence: BSD-3-Clause ([`LICENSE`](LICENSE))
- Wavetables (shipped, from Tablor's repo): Adventure Kid, 65 tables by Kristoffer Ekstrand, public domain; Neu
  KatalYst, 50 tables, free to use ("use them in all your synths"), as credited in Tablor's README
- MPC port and screen: [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/tablor`), built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- `src/dsp/wt/scanner.h`: the wavetables are read from the plugin's data folder, `<data folder>/wavetables` (`/sdcard/vst/tablor/wavetables` with this repo's installer, the folder beside the plugin in a release), where the shipped packs go and you add your own; the first-run copy into a user folder is skipped (`-DMPC_PORT`).
- `src/dsp/tablor_plugin.cpp`: `wt1_name`/`wt2_name` readouts (the table's file name) for the screen.
- `src/dsp/tablor_plugin.cpp` (2026-10-04): the factory presets on the MPC: `preset` / `preset_name` / `preset_count` / `preset_name_at:<n>` (appended params, and MPC's PRESET menu); choosing one resets to the defaults and applies its `TBLR2;` blob, as Move's preset browser does. The presets' Move wavetable paths (`/data/UserData/UserLibrary/Wavetables/`) map to `<data folder>/wavetables/`; they're read when the plugin is created (MPC reads the menu's size then); the state remembers the chosen preset, so a reopened project keeps its edits.
- `src/dsp/wt/loader.h` + `tb_destroy_instance`: the loader thread is joined before the instance is deleted. Upstream deletes members the loader may still be publishing into (ASan heap-use-after-free on a quick insert/remove, found by the offline test 2026-10-01).
- Diff: `upstream-changes.diff`.
- `params.base.json` comes from the module's `chain_params` (what the engine actually takes, saved as `chain_params.engine.json`), not its menu tree.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).

## Files

| Path | What |
|---|---|
| `screenshots/` | the pages as MPC draws them |
| `release/` | `library.json` (where its library comes from, pinned) and `fetch-library.py` (fetches it) |
| `.github/workflows/release.yml` | the release build (GitHub Actions, a draft release) |
| `FRAMEWORK.md` | the changes to sd88me's tools this plugin is built with, and why |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry (not used by the release) |
| `vst.json` | build settings: name, maker, sources, compiler flags |
| `params.json` | the plugin's parameters as MPC sees them (VST index = order) |
| `params.base.json` | the engine's own parameter list it was derived from |
| `chain_params.engine.json` | what the engine reports it takes (its `chain_params`) |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `tablor.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `upstream-changes.diff` | every local change to the upstream source |
| `UPSTREAM` | where the source came from, and at which commit |

## Building

With Docker (32-bit ARM emulation for the build), Python 3 and the tools from
[saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features) (sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)
plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`, and [FRAMEWORK.md](FRAMEWORK.md) says which changes this plugin uses and why):

```
git clone -b steve-features https://github.com/saustin2010/mpc-vst-plugins
MPC_VST=$PWD/mpc-vst-plugins
python3 release/fetch-library.py library        # its library, from its project at a pinned commit
bash "$MPC_VST/tools/build_port.sh" vst.json        # build/: tablor.so, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](https://github.com/saustin2010/vst_instruments/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](https://github.com/saustin2010/vst_instruments/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/tablor`), next to the other plugins
and the tools that made its screen, and published to [mpc-vst-tablor](https://github.com/saustin2010/mpc-vst-tablor) for its
releases. Issues and pull requests are welcome in either.
