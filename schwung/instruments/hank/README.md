# Hank

**Synth** · Two-operator FM on one page: ratio, brightness, bite, an envelope pair, noise and a tone sweep. · maker in MPC: Charles Vestal · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Hank on the MPC touchscreen">

A small, immediate FM synth, as its author puts it: "two-operator FM on eight knobs, one page: ratio, brightness, bite, one envelope pair, noise and a tone sweep". Add voices, glide and transpose, and that's it. 32 built-in presets cover basses, keys, bells and plucks.

## On the MPC

- In the plugin browser: **Hank** by **Charles Vestal** (Synth)
- Files: `/sdcard/vst/hank.so`, screen in `/sdcard/Synths/Charles Vestal - VST - Hank/`
- 16 parameters (all automatable) on 1 page

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. HANK

<img src="screenshots/page_0.png" width="760" alt="Hank, page HANK">

Q-Link columns: **1** PATCH, RATIO, BRIGHT, BITE (MOD)  ·  **2** TONE, ATTACK, DECAY, SUSTAIN  ·  **3** NOISE, GLIDE, VOICES, TRANSPOSE  ·  **4** VOLUME

## Install

From the top of this repo (see [INSTALL.md](../../../INSTALL.md)):

```
./install.sh <mpc-address> hank
```

## Where it comes from

- Schwung module "Hank" v0.6.0 by charlesvestal
- Licence: MIT, as declared in the module's `src/module.json` (text: [licenses/](../../../licenses/))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Presets work: the preset control only reached presets 0 and 1 (its range comes from the engine at run time, which the build ignored); now a PATCH browser over all 32.
- **Sine lookup fixed (2026-10-02)**: a float rounding edge (a phase of -0.000001 wrapped to exactly 1.0) read one past Hank's sine table. `src/dsp/hank_engine.cpp` under `MPC_PORT` (vst.json defines it); diff in `upstream-changes.diff`. Found with `dev-tools/fuzz`.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).

## Files

| Path | What |
|---|---|
| `screenshots/` | the pages as MPC draws them |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry (its presets/kits are in the repo's `presets/` folder, not here) |
| `vst.json` | build settings: name, maker, sources, compiler flags |
| `params.json` | the plugin's parameters as MPC sees them (VST index = order) |
| `params.base.json` | the engine's own parameter list it was derived from |
| `params.pre-stitch.json` | the parameter list before the Stitch screen renamed controls |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `hank.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `upstream-changes.diff` | every local change to the upstream source |

To rebuild from source see [BUILDING.md](../../../BUILDING.md); to change the screen, [RESKINNING.md](../../../RESKINNING.md).
