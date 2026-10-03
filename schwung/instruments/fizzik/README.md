# Fizzik

**Synth** · Physical modelling: an exciter and two coupled resonators (string, beam, plate, membrane). · maker in MPC: Filliformes · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Fizzik on the MPC touchscreen">

A physical-modelling instrument. An exciter (an impulse and noise mix with crackle, colour, its own envelope and resonance, velocity-sensitive) sets two resonators ringing; each can be a string, a beam, a plate or a membrane, with its own structure, decay, damping, position, tuning and tension, and COUPLE lets the two feed each other. After them: a filter with six voicings (SVF, SEM, MS-20, Steiner, two ladders), drive, chorus, delay, reverb and a limiter, two LFOs and aftertouch presets (bow, swell, vibrato...). 31 named presets and three randomise buttons. Plucked and struck tones, bowed glass, bells, metal and wood that react to how hard you play.

## On the MPC

- In the plugin browser: **Fizzik** by **Filliformes** (Synth)
- Files: `/sdcard/vst/fizzik.so`, screen in `/sdcard/Synths/Filliformes - VST - Fizzik/`
- 72 parameters (all automatable) on 5 pages

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAIN

<img src="screenshots/page_0.png" width="760" alt="Fizzik, page MAIN">

Q-Link columns: **1** MODEL A, MODEL B, COUPLE, BALANCE  ·  **2** CUTOFF, RESONANCE, FILTER TYPE, VOICING  ·  **3** DRIVE, WIDTH, LEVEL  ·  **4** PRESET

### 2. EXCITER

<img src="screenshots/page_1.png" width="760" alt="Fizzik, page EXCITER">

Q-Link columns: **1** EXC MIX, CRACKLE, COLOR, ATTACK  ·  **2** DECAY, EXC RESO, VEL LEVEL, VEL COLOR  ·  **3** STRUCTURE A, DECAY A, DAMP A, POSITION A  ·  **4** TONE A, TUNE A, TENSION A

### 3. RESONATOR B

<img src="screenshots/page_2.png" width="760" alt="Fizzik, page RESONATOR B">

Q-Link columns: **1** STRUCTURE B, DECAY B, DAMP B, POSITION B  ·  **2** TONE B, TUNE B, TENSION B  ·  **3** GLIDE, AMP ATK, AMP REL, SPREAD  ·  **4** REVERB, REV SIZE, REV DAMP

### 4. DELAY / FX

<img src="screenshots/page_3.png" width="760" alt="Fizzik, page DELAY / FX">

Q-Link columns: **1** DELAY MIX, DLY TIME, DLY FBK, DLY TONE  ·  **2** TONE, BODY, CHORUS, CHO RATE  ·  **3** CHO DEPTH, GLUE, LIM DRIVE, LIM CEIL

### 5. LFO / AT

<img src="screenshots/page_4.png" width="760" alt="Fizzik, page LFO / AT">

Q-Link columns: **1** LFO1 RATE, LFO1 DEPTH, LFO1 SHAPE, LFO1 TARGET  ·  **2** LFO2 RATE, LFO2 DEPTH, LFO2 SHAPE, LFO2 TARGET  ·  **3** AT PRESET, AT BRIGHT, AT BOW, AT CUTOFF  ·  **4** AT VIB, AT BEND, AT VIB RATE, AT CURVE

## Install

From the top of this repo (see [INSTALL.md](../../../INSTALL.md)):

```
./install.sh <mpc-address> fizzik
```

## Where it comes from

- Upstream: https://github.com/filliformes/fizzik-move
- Vendored at commit ff9148897d44090e4887c31871f9cd846cbb9f90 2026-09-18
- Schwung module "Fizzik" v0.1.1 by Filliformes
- Licence: MIT ([`LICENSE`](LICENSE))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- `src/dsp/fizzik.c` (`-DMPC_PORT`, 2026-10-02): the waveguide's fractional read wraps a position that float rounding left at exactly the delay length (one past the line; found with dev-tools/fuzz). Diff: `upstream-changes.diff`.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).
- Presets in MPC's PRESET menu (2026-10-03): its 31 presets are VST programs (vst.json `programs`), so the PRESET
  dropdown in the plugin header, also on the arrangement screen, lists and loads them.

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
| `fizzik.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `upstream-changes.diff` | every local change to the upstream source |
| `UPSTREAM` | where the source came from, and at which commit |

To rebuild from source see [BUILDING.md](../../../BUILDING.md); to change the screen, [RESKINNING.md](../../../RESKINNING.md).
