# Mr Hyde

**Synth** · A MicroFreak-inspired voice: Plaits models with a low-pass gate, filter and a 6x6 mod matrix, with 19 presets. · maker in MPC: Move Everything · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Mr Hyde on the MPC touchscreen">

Built around Mutable Instruments' Plaits engine (17 models: virtual analog, wavetables, FM, grains, chords, speech, strings, modal and percussion models), Mr Hyde adds what a MicroFreak-style instrument needs: a low-pass gate, a filter, an LFO, envelopes, a cycling envelope, a random source, and a 6x6 modulation matrix whose rows are spread over the ASSIGN, PITCH HARM and TIMB CUT pages.

Upstream has no presets, so the port brings 19 (Init, Freak Bass, Wobble Bass, Sync Lead, Terrain Pad, String Machine, Supersaw Stack, Folded Pluck, FM Keys, Formant Choir, Drawbar Organ, Wavetable Sweep, Chord Memory, Speech Synth, Swarm, Noise Sweep, Particle Rain, Glass String, Modal Bells), in MPC's PRESET menu. Each sets every control; levels are evened out as far as VOLUME allows (it drives the low-pass gate, which saturates), so a few models (organ, string machine, inharmonic string, particles) stay quieter by nature.

## On the MPC

- In the plugin browser: **Mr Hyde** by **Move Everything** (Synth)
- Files: `/sdcard/vst/mrhyde.so`, screen in `/sdcard/Synths/Move Everything - VST - Mr Hyde/`
- 81 parameters (all automatable) on 7 pages

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAIN

<img src="screenshots/page_0.png" width="760" alt="Mr Hyde, page MAIN">

Q-Link columns: **1** MODEL, FM AMOUNT, AUX MIX  ·  **2** PITCH, HARMONICS, TIMBRE, MORPH  ·  **3** FILTER MODE, CUTOFF FREQ, RESONANCE  ·  **4** LPG DECAY, LPG COLOR, VOLUME, PAN

### 2. LFO ENV

<img src="screenshots/page_1.png" width="760" alt="Mr Hyde, page LFO ENV">

Q-Link columns: **1** LFO SHAPE, LFO RATE, LFO PHASE, LFO SYNC  ·  **2** VEL CURVE, AT CURVE  ·  **3** ENV ATTACK, ENV DECAY, ENV SUSTAIN, ENV RELEASE  ·  **4** LFO RETRIG, ENV RETRIG

### 3. CYC RAND

<img src="screenshots/page_2.png" width="760" alt="Mr Hyde, page CYC RAND">

Q-Link columns: **1** CYC ATTACK, CYC DECAY, CYC SHAPE  ·  **2** CYC SYNC, CYC RETRIG, CYC BIPOLAR  ·  **3** RND MODE, RND RATE, RND SLEW  ·  **4** RND SYNC, RND RETRIG

### 4. ASSIGN

<img src="screenshots/page_3.png" width="760" alt="Mr Hyde, page ASSIGN">

Q-Link columns: **1** A1 TARGET, A1 LFO, A1 ENV, A1 CYCLE  ·  **2** A1 RANDOM, A1 VEL, A1 AT  ·  **3** A2 TARGET, A2 LFO, A2 ENV, A2 CYCLE  ·  **4** A2 RANDOM, A2 VEL, A2 AT

### 5. PITCH HARM

<img src="screenshots/page_4.png" width="760" alt="Mr Hyde, page PITCH HARM">

Q-Link columns: **1** PITCH LFO, PITCH ENV, PITCH CYCLE  ·  **2** PITCH RANDOM, PITCH VEL, PITCH AT  ·  **3** HARM LFO, HARM ENV, HARM CYCLE  ·  **4** HARM RANDOM, HARM VEL, HARM AT

### 6. TIMB CUT

<img src="screenshots/page_5.png" width="760" alt="Mr Hyde, page TIMB CUT">

Q-Link columns: **1** TIMB LFO, TIMB ENV, TIMB CYCLE  ·  **2** TIMB RANDOM, TIMB VEL, TIMB AT  ·  **3** CUT LFO, CUT ENV, CUT CYCLE  ·  **4** CUT RANDOM, CUT VEL, CUT AT

### 7. VOICE

<img src="screenshots/page_6.png" width="760" alt="Mr Hyde, page VOICE">

Q-Link columns: **1** VOICE MODE, POLYPHONY, GLIDE  ·  **2** UNISON, DETUNE, SPREAD

## Install

From the top of this repo (see [INSTALL.md](../../../INSTALL.md)):

```
./install.sh <mpc-address> mrhyde
```

## Where it comes from

- Schwung module "MrHyde" v0.0.1 by move-anything contributors
- Licence: MIT, as declared in the module's `src/module.json` (text: [licenses/](../../../licenses/))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Seven compact pages (MAIN, LFO ENV, CYC RAND, ASSIGN, PITCH HARM, TIMB CUT, VOICE) instead of an auto layout; the mod matrix as rows of six.
- Readable unique names (e.g. A1 LFO, PITCH ENV, CYC BIPOLAR); the 17 models in a two-column pop-up.
- The engine reports option values as text: option labels only changed case; pop-ups draw friendlier labels.
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
| `mrhyde.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |

To rebuild from source see [BUILDING.md](../../../BUILDING.md); to change the screen, [RESKINNING.md](../../../RESKINNING.md).
