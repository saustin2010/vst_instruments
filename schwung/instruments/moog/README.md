# Moog

**Synth** · RaffoSynth: a Minimoog-style mono synth with four oscillators and a ladder filter. · maker in MPC: Raffo · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Moog on the MPC touchscreen">

RaffoSynth, a Minimoog-inspired monosynth: four oscillators (waveform and footage switches, 32' to 2', detune), noise, the 24 dB resonant ladder filter with its contour, a loudness contour, an LFO, mod wheel routing and glide. Fat basses and leads. 14 presets, also listed in MPC's own PRESET menu in the plugin header. The oscilloscope on the MAIN page shows oscillator 1's waveform, and the two envelope displays follow their knobs.

## On the MPC

- In the plugin browser: **[SYN] Moog** by **Raffo** (Synth)
- Files: `/sdcard/vst/moog.so`, screen in `/sdcard/Synths/Raffo - VST - [SYN] Moog/`
- 41 parameters (all automatable) on 3 pages

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

Its presets also appear in MPC's own **PRESET** menu in the plugin header.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAIN

<img src="screenshots/page_0.png" width="760" alt="Moog, page MAIN">

Q-Link columns: **1** CUTOFF, EMPHASIS, CONTOUR, KEY TRACK  ·  **2** FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  **3** AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  **4** PATCH

### 2. OSCILLATORS

<img src="screenshots/page_1.png" width="760" alt="Moog, page OSCILLATORS">

Q-Link columns: **1** NOISE, OSC1 WAVE, OSC1 RANGE, OSC1 LEVEL  ·  **2** OSC2 WAVE, OSC2 RANGE, OSC2 DETUNE, OSC2 LEVEL  ·  **3** OSC3 WAVE, OSC3 RANGE, OSC3 DETUNE, OSC3 LEVEL  ·  **4** OSC4 WAVE, OSC4 RANGE, OSC4 DETUNE, OSC4 LEVEL

### 3. MODULATION

<img src="screenshots/page_2.png" width="760" alt="Moog, page MODULATION">

Q-Link columns: **1** LFO RATE, LFO PITCH, LFO FILTER  ·  **2** WHEEL FILTER, WHEEL PITCH  ·  **3** GLIDE, BEND RANGE, VELOCITY  ·  **4** VOLUME

## Install

From the top of this repo (see [INSTALL.md](../../../INSTALL.md)):

```
./install.sh <mpc-address> moog
```

## Where it comes from

- Schwung module "RaffoSynth" v0.2.5 by Nicolas Roulet, Julian Palladino (port: charlesvestal)
- Licence: MIT, as declared in the module's `src/module.json` (text: [licenses/](../../../licenses/))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Presets work: the engine has 14 built in that were never exposed; now a PATCH browser.
- Oscillator waveforms are labelled switches (TRIANGLE/SAW/SQUARE/PULSE) and the ranges are footage switches (32'/16'/8'/4'/2', sending the engine's -2..2); three pages (MAIN, OSCILLATORS, MODULATION).
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).

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
| `moog.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |

To rebuild from source see [BUILDING.md](../../../BUILDING.md); to change the screen, [RESKINNING.md](../../../RESKINNING.md).
