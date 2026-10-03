# OB-Xd

**Synth** · Oberheim OB-X: reales' OB-Xd with its 128 factory presets. · maker in MPC: reales · licence: GPL-3.0

<img src="screenshots/page_0.png" width="760" alt="OB-Xd on the MPC touchscreen">

The OB-Xd emulation of Oberheim's OB-X: two oscillators per voice with sync, cross-modulation and pulse width, a mixer with noise, a 12/24 dB multimode filter, filter and amp envelopes, an LFO routed to pitch, filter and pulse width, and "voice variation" controls that detune each voice's oscillator, filter and envelopes slightly, as analog voices do. 128 factory presets ship with it, and any `.fxb` banks you add show up in its BANK selector.

## On the MPC

- In the plugin browser: **OB-Xd** by **reales** (Synth)
- Files: `/sdcard/vst/obxd.so`, presets/data in `/sdcard/vst/obxd/` (from this repo's `presets/obxd/`, which `tools/fetch-presets.py` fills; install.sh does that for you), screen in `/sdcard/Synths/reales - VST - OB-Xd/`
- 74 parameters (all automatable) on 5 pages

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAIN

<img src="screenshots/page_0.png" width="760" alt="OB-Xd, page MAIN">

Q-Link columns: **1** PATCH, VOLUME, TUNE, VOICES  ·  **2** SPREAD, UNISON, AS PLAYED, LEGATO  ·  **3** PORTAMENTO, BEND 12, BEND OSC2, BANK

### 2. OSCILLATORS

<img src="screenshots/page_1.png" width="760" alt="OB-Xd, page OSCILLATORS">

Q-Link columns: **1** OSC1 PITCH, OSC1 SAW, OSC1 PULSE, OSC2 PITCH  ·  **2** OSC2 DETUNE, OSC2 SAW, OSC2 PULSE, OSC2 SYNC  ·  **3** PULSE WIDTH, PW OFFSET, PW ENV, PW ENV BOTH  ·  **4** X-MOD, BRIGHTNESS, OSC2 STEP

### 3. FILTER

<img src="screenshots/page_2.png" width="760" alt="OB-Xd, page FILTER">

Q-Link columns: **1** OSC1 LEVEL, OSC2 LEVEL, NOISE, CUTOFF  ·  **2** RESONANCE, ENV AMOUNT, KEY TRACK, MULTIMODE  ·  **3** BANDPASS, 24 dB, SELF OSC, ENV INVERT  ·  **4** FILTER VAR, GLIDE VAR, ENV VAR, LEVEL VAR

### 4. ENVELOPES

<img src="screenshots/page_3.png" width="760" alt="OB-Xd, page ENVELOPES">

Q-Link columns: **1** FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  **2** FLT VEL, AMP ATTACK, AMP DECAY, AMP SUSTAIN  ·  **3** AMP RELEASE, AMP VEL

### 5. MODULATION

<img src="screenshots/page_4.png" width="760" alt="OB-Xd, page MODULATION">

Q-Link columns: **1** LFO RATE, LFO SYNC, LFO SINE, LFO SQUARE  ·  **2** LFO S&H, PITCH ENV, P.ENV BOTH, VIBRATO  ·  **3** MOD AMOUNT, MOD OSC1, MOD OSC2, MOD FILTER  ·  **4** PWM AMOUNT, PWM OSC1, PWM OSC2

## Install

From the top of this repo (see [INSTALL.md](../../../INSTALL.md)):

```
./install.sh <mpc-address> obxd
```

## Where it comes from

- Schwung module "OB-Xd" v0.4.9 by reales (port: charlesvestal)
- Licence: GPL-3.0, as declared in the module's `src/module.json` (text: [licenses/](../../../licenses/))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Sound fixed (2026-09-30): the knobs were declared 0-1 while the engine speaks 0-100, so any restore or knob move zeroed the patch; the fallback Init patch was also silent (engine patch, VENDORED.md).
- Presets work: factory bank shipped + MODULE_DIR, PATCH browser with a red dot-matrix display.
- BANK selector (2026-10-03), under PATCH on MAIN: every `.fxb` bank in `/sdcard/vst/obxd/presets/` (up to 32; Factory first, then by file name, which is the name shown), and PATCH browses the chosen one (up to 128 programs). The bank is saved with the project by name. An OB-Xd 1.x LV2 bank (`presets.ttl`) converts with `python3 tools/obxd-lv2-to-fxb.py <presets.ttl or its archive> "presets/obxd/presets/<Bank name>.fxb"`. The new parameters are appended (indices 70-73), so saved projects and Q-Link assignments keep working.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).

## Files

| Path | What |
|---|---|
| `screenshots/` | the pages as MPC draws them |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry (its presets/kits are in the repo's `presets/` folder, not here) |
| `vst.json` | build settings: name, maker, sources, compiler flags |
| `params.json` | the plugin's parameters as MPC sees them (VST index = order) |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `obxd.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `VENDORED.md` | notes on the vendored source and its patches |

To rebuild from source see [BUILDING.md](../../../BUILDING.md); to change the screen, [RESKINNING.md](../../../RESKINNING.md).
