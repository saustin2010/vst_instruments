# Rings

**Synth** · Mutable Instruments Rings: a resonator, strummed by the notes you play. · maker in MPC: Mutable Instruments · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Rings on the MPC touchscreen">

Rings is a resonator with seven models (a modal resonator, sympathetic strings, a string, an FM voice, sympathetic chords, a string with reverb, and the module's hidden "Disastrous Peace" string synth), shaped by STRUCTURE, BRIGHTNESS, DAMPING and POSITION, with one, two or four voices ringing at once. Here every note strums it with Rings' internal exciter (velocity sets how hard) and the voices rotate as on the module. The string synth's effect (formant, chorus, reverb, ensemble) is SYNTH FX. Runs Mutable's own DSP.

## On the MPC

- In the plugin browser: **Rings** by **Mutable Instruments** (Synth)
- Files: `/sdcard/vst/rings.so`, screen in `/sdcard/Synths/Mutable Instruments - VST - Rings/`
- 12 parameters (all automatable) on 1 page

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. RINGS

<img src="screenshots/page_0.png" width="760" alt="Rings, page RINGS">

Q-Link columns: **1** MODEL, POLYPHONY, STRUCTURE, BRIGHTNESS  ·  **2** DAMPING, POSITION, VELOCITY, OCTAVE  ·  **3** BEND RANGE, SYNTH FX, WIDTH, VOLUME

## Install

From the top of this repo (see [INSTALL.md](../../INSTALL.md)):

```
./install.sh <mpc-address> rings
```

## Where it comes from

- Upstream: https://github.com/pichenettes/eurorack (rings/)
- Vendored at commit 08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4 2023-08-16
- Licence: MIT ([`LICENSE`](LICENSE))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Not a Move module: `mpc/rings_engine.cc` drives Mutable's `rings::Part` (and its string synth) directly. MIDI note-on = strum at that note (internal exciter), voices rotate as on the module; pitch bend = FM. Runs at Rings' own 48 kHz, resampled to 44.1 kHz (`mpc/mi_engine.h`).
- `src/rings/dsp/part.*`, `performance_state.h` (`-DMPC_PORT`): velocity scales the internal exciter's strike/pluck (the module has no velocity). Diff: `upstream-changes.diff`.
- `src/stmlib/dsp/dsp.h` (`-DMPC_PORT`, 2026-10-02): `Interpolate` clamps its index; a control at exactly 100% read one past a lookup table (found with dev-tools/fuzz). Diff: `upstream-changes.diff`.
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
| `rings.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `mpc/` | MPC-side glue: the engine wrapper or MIDI/audio adapter, and headers |
| `upstream-changes.diff` | every local change to the upstream source |
| `UPSTREAM` | where the source came from, and at which commit |

To rebuild from source see [BUILDING.md](../../BUILDING.md); to change the screen, [RESKINNING.md](../../RESKINNING.md).
