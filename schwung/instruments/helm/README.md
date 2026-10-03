# Helm

**Synth** · Matt Tytel's Helm polysynth with its factory patches (275 with Move Organ). · maker in MPC: Matt Tytel · licence: GPL-3.0

<img src="screenshots/page_0.png" width="760" alt="Helm on the MPC touchscreen">

Helm is a full polyphonic subtractive synth: two oscillators with unison and cross-modulation, a sub oscillator and noise, a multimode filter with a formant section, filter and modulation envelopes, mono and poly LFOs, a 32-step sequencer, an arpeggiator, distortion, delay, reverb and a stutter effect. This port runs Helm's own DSP engine headless and comes with its 274 factory patches (CC BY 4.0, by Matt Tytel and contributors) plus the Schwung port's Move Organ. Twelve pages; steps 17-32 of the step sequencer are automatable but not on a page.

## On the MPC

- In the plugin browser: **Helm** by **Matt Tytel** (Synth)
- Files: `/sdcard/vst/helm.so`, presets/data in `/sdcard/vst/helm/` (from this repo's `presets/helm/`, which `tools/fetch-presets.py` fills; install.sh does that for you), screen in `/sdcard/Synths/Matt Tytel - VST - Helm/`
- 164 parameters (all automatable) on 12 pages

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAIN

<img src="screenshots/page_0.png" width="760" alt="Helm, page MAIN">

Q-Link columns: **1** VOLUME, POLYPHONY, OCTAVE, LEGATO  ·  **2** CUTOFF, RESONANCE, FILTER TYPE  ·  **3** AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  **4** PATCH

### 2. OSC

<img src="screenshots/page_1.png" width="760" alt="Helm, page OSC">

Q-Link columns: **1** OSC1 WAVE, OSC1 TRANSP, OSC1 TUNE, OSC1 VOLUME  ·  **2** OSC1 VOICES, OSC1 DETUNE, OSC1 HARMON  ·  **3** OSC2 WAVE, OSC2 TRANSP, OSC2 TUNE, OSC2 VOLUME  ·  **4** OSC2 VOICES, OSC2 DETUNE, OSC2 HARMON

### 3. OSC MIX

<img src="screenshots/page_2.png" width="760" alt="Helm, page OSC MIX">

Q-Link columns: **1** CROSS MOD, FBK AMOUNT, FBK TRANSP, FBK TUNE  ·  **2** OSC MIX  ·  **3** NOISE VOL  ·  **4** SUB OCT, SUB SHUF, SUB VOL, SUB OSC WAVE

### 4. FILTER

<img src="screenshots/page_3.png" width="760" alt="Helm, page FILTER">

Q-Link columns: **1** FILTER ON, FILTER STYLE, FILTER SHELF, FILTER BLEND  ·  **2** FILTER DRIVE, SATURATION, FLT ENV AMT, FLT KEYTRACK  ·  **3** FORMANT ON, FORMANT X, FORMANT Y  ·  **4** FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE

### 5. MOD ENV

<img src="screenshots/page_4.png" width="760" alt="Helm, page MOD ENV">

Q-Link columns: **1** MOD ATTACK, MOD DECAY, MOD SUSTAIN, MOD RELEASE  ·  **2** LFO1 WAVE, LFO1 AMOUNT, LFO1 RETRIG  ·  **3** LFO1 SYNC, LFO1 FREQ, LFO1 TEMPO

### 6. MONO LFO

<img src="screenshots/page_5.png" width="760" alt="Helm, page MONO LFO">

Q-Link columns: **1** LFO2 WAVE, LFO2 AMOUNT, LFO2 RETRIG  ·  **2** LFO2 SYNC, LFO2 FREQ, LFO2 TEMPO  ·  **3** PLFO WAVE, PLFO AMOUNT  ·  **4** PLFO SYNC, PLFO FREQ, PLFO TEMPO

### 7. STEP SEQ

<img src="screenshots/page_6.png" width="760" alt="Helm, page STEP SEQ">

Q-Link columns: **1** NUM STEPS, STEP SMOOTH, STEP RETRIG  ·  **2** STEP SYNC, STEP FREQ, STEP TEMPO  ·  **3** STEP 1, STEP 2, STEP 3, STEP 4  ·  **4** STEP 5, STEP 6, STEP 7, STEP 8

### 8. STEPS

<img src="screenshots/page_7.png" width="760" alt="Helm, page STEPS">

Q-Link columns: **1** STEP 9, STEP 10, STEP 11, STEP 12  ·  **2** STEP 13, STEP 14, STEP 15, STEP 16  ·  **3** ARP ON, ARP PATTERN, ARP OCTAVES, ARP GATE  ·  **4** ARP SYNC, ARP FREQ, ARP TEMPO

### 9. DISTORTION

<img src="screenshots/page_8.png" width="760" alt="Helm, page DISTORTION">

Q-Link columns: **1** DIST ON, DIST TYPE, DIST DRIVE, DIST MIX  ·  **2** DELAY ON, DELAY MIX, DELAY FBK  ·  **3** DELAY SYNC, DELAY FREQ, DELAY TEMPO

### 10. REVERB

<img src="screenshots/page_9.png" width="760" alt="Helm, page REVERB">

Q-Link columns: **1** REVERB ON, REVERB MIX, REVERB FBK, REV DAMPING  ·  **2** STUTTER ON, STUT SOFT  ·  **3** STUT SYNC, STUT FREQ, STUT TEMPO  ·  **4** RESAMP SYNC, RESAMP FREQ, RESAMP TEMPO

### 11. PLAYING

<img src="screenshots/page_10.png" width="760" alt="Helm, page PLAYING">

Q-Link columns: **1** AUTO BPM, BPM, VEL TRACK, BEND RANGE  ·  **2** PORTA TYPE, PORTAMENTO  ·  **3** MOD 1 AMT, MOD 2 AMT, MOD 3 AMT, MOD 4 AMT  ·  **4** MOD 5 AMT, MOD 6 AMT, MOD 7 AMT, MOD 8 AMT

### 12. MOD AMOUNTS

<img src="screenshots/page_11.png" width="760" alt="Helm, page MOD AMOUNTS">

Q-Link columns: **1** MOD 9 AMT, MOD 10 AMT, MOD 11 AMT, MOD 12 AMT  ·  **2** MOD 13 AMT, MOD 14 AMT, MOD 15 AMT, MOD 16 AMT

## Install

From the top of this repo (see [INSTALL.md](../../../INSTALL.md)):

```
./install.sh <mpc-address> helm
```

## Where it comes from

- Upstream: https://github.com/andree182/schwung-helm
- Vendored at commit 10823a8ebc3a0e1a0cd43d0c459e8bae63996c9d 2026-06-11
- Schwung module "Helm" v1.5 by Matt Tytel (port: andree182 & antigravity)
- Licence: GPL-3.0 ([`LICENSE`](LICENSE))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Adds `src/data/Move Organ.helm` (from upstream's `data/`) to the factory patch folder it ships.
- mopo (Helm's DSP library), found by the offline test: `Memory` frees a `new[]` buffer with `delete` (now `delete[]`), and `TriggerWait` copies an uninitialised flag (now initialised). Its oscillators wrap their phase by signed overflow, so the build adds `-fwrapv`. Diff: `upstream-changes.diff`.
- Vendored tree trimmed from 139 MB to 57 MB (2026-10-01): removed the unused second JUCE copy, JUCE's examples/extras/docs, concurrentqueue's benchmarks/tests, the GUI/standalone/builds folders and the bundled VST3_SDK (Steinberg's SDK is never kept here); the build and test pass without them.
- `params.base.json` comes from the module's `chain_params` (what the engine actually takes, saved as `chain_params.engine.json`), not its menu tree.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).
- Presets in MPC's PRESET menu (2026-10-03): its 275 patches are VST programs, listed live from the engine (vst.json
  `programs` with `count` and `name_at`; the engine answers a preset's name by number, marked MPC port), named from their files without loading each one (stepping through them at every insert would take seconds).

## Files

| Path | What |
|---|---|
| `screenshots/` | the pages as MPC draws them |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry (its presets/kits are in the repo's `presets/` folder, not here) |
| `vst.json` | build settings: name, maker, sources, compiler flags |
| `params.json` | the plugin's parameters as MPC sees them (VST index = order) |
| `params.base.json` | the engine's own parameter list it was derived from |
| `params.pre-stitch.json` | the parameter list before the Stitch screen renamed controls |
| `chain_params.engine.json` | what the engine reports it takes (its `chain_params`) |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `helm.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `upstream-changes.diff` | every local change to the upstream source |
| `UPSTREAM` | where the source came from, and at which commit |

To rebuild from source see [BUILDING.md](../../../BUILDING.md); to change the screen, [RESKINNING.md](../../../RESKINNING.md).
