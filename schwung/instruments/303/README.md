# 303

**Synth** · TB-303 bass line: Open303 with the Devilfish mods, a drive stage and 13 presets. · maker in MPC: Robin Schmidt · licence: GPL-3.0

<img src="screenshots/page_0.png" width="760" alt="303 on the MPC touchscreen">

A Roland TB-303 emulation built on Robin Schmidt's Open303 engine, with the "Devilfish" modifications (separate normal and accent decay, filter tracking, slide time, overdriven sub) and a soft/RAT-style drive after the filter. Play it from MPC's pads, keys or a clip: overlapping notes slide, high velocities accent. Monophonic, as the original. Turn CUTOFF, RESONANCE, ENV MOD and DECAY while a pattern runs for the classic acid squelch; the DEVILFISH page holds the extra envelope, filter and slide controls.

## On the MPC

- In the plugin browser: **[SYN] 303** by **Robin Schmidt** (Synth)
- Files: `/sdcard/vst/acid303.so`, screen in `/sdcard/Synths/Robin Schmidt - VST - [SYN] 303/`
- 18 parameters (all automatable) on 2 pages

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. 303 MAIN

<img src="screenshots/page_0.png" width="760" alt="303, page 303 MAIN">

Q-Link columns: **1** WAVEFORM, TUNING  ·  **2** CUTOFF, RESONANCE, ENV MOD, DECAY  ·  **3** ACCENT, VOLUME  ·  **4** DRIVE MODEL, DRIVE, DRIVE MIX, SHAPER DRIVE

### 2. DEVILFISH MOD

<img src="screenshots/page_1.png" width="760" alt="303, page DEVILFISH MOD">

Q-Link columns: **1** DEVILFISH, NORMAL DECAY, ACCENT DECAY, SOFT ATTACK  ·  **2** FEEDBACK HPF, SLIDE TIME

## Install

From the top of this repo (see [INSTALL.md](../../../INSTALL.md)):

```
./install.sh <mpc-address> 303
```

## Where it comes from

- Schwung module "303" v0.3.3 by Robin Schmidt / midilab / davemollen / Charles Vestal (Open303 + Devilfish)
- Licence: GPL-3.0, as declared in the module's `src/module.json` (text: [licenses/](../../../licenses/))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Every parameter renamed readably (e.g. NORMAL DECAY, SHAPER DRIVE); WAVEFORM and DRIVE MODEL as switches.
- **Wavetable init (2026-10-02)**: Open303 cleared four entries past its prototype table at start-up (into the next member, which is filled afterwards, so harmless, but undefined). `rosic_MipMappedWaveTable.cpp` under `MPC_PORT`; diff in `upstream-changes.diff`. Found with `dev-tools/fuzz`.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).
- Presets (2026-10-03): the engine has none, so the port brings 13 (`presets.json`: Classic Acid, Squelch Lead, Deep Sub,
  Rubber Square, Plucky Saw, Long Sweep, Rat Acid, Distorted Square, Acid Screamer, three Devilfish patches and Init),
  in MPC's PRESET menu. Each sets every control; levels matched within about 4.5 dB.
- Q-Links (2026-10-03, design QA): one column per panel on the main page, VCO | VCF | ACCENT + OUT | DRIVE, so the
  outline MPC draws round the active column frames one panel (it used to split the VCF across two columns).

## Files

| Path | What |
|---|---|
| `screenshots/` | the pages as MPC draws them |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry (its presets/kits are in the repo's `presets/` folder, not here) |
| `vst.json` | build settings: name, maker, sources, compiler flags |
| `params.json` | the plugin's parameters as MPC sees them (VST index = order) |
| `params.base.json` | the engine's own parameter list it was derived from |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `303.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `upstream-changes.diff` | every local change to the upstream source |

To rebuild from source see [BUILDING.md](../../../BUILDING.md); to change the screen, [RESKINNING.md](../../../RESKINNING.md).
