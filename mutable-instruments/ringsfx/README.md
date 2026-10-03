# Rings FX

**Audio effect** · Rings as an audio effect: the track's sound excites the resonator, with 12 presets. · maker in MPC: Mutable Instruments · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Rings FX on the MPC touchscreen">

The same Rings resonator as an insert effect: the track's audio becomes the exciter and Rings' own onset detector strums it, tuned to NOTE. Drums turn into tuned metallic hits, voices and loops grow sympathetic string halos. INPUT gain and a dry/wet MIX sit alongside the module's controls.

Upstream has no presets, so the port brings 12 (Init, Resonant Body, Metal Plate, Bright Bell, Low Drone, Sympathetic Strings, Sitar Drone, Chord Resonator, Plucked String, FM Ring, String Reverb, Shimmer Wash), in MPC's PRESET menu, each setting every control, with levels evened out (measured with a test signal through it).

## On the MPC

- In the plugin browser: **Rings FX** by **Mutable Instruments** (Effect)
- Files: `/sdcard/vst/ringsfx.so`, screen in `/sdcard/Synths/Mutable Instruments - VST - Rings FX/`
- 12 parameters (all automatable) on 1 page

## Playing it

An audio effect: insert it in a track's or program's insert effects (it reports two inputs and outputs and the Effect category). It processes 128-sample blocks, about 3 ms of latency at 44.1 kHz.

**Not yet tried on a device:** whether MPC OS offers third-party VST effects in its insert list. The plugin builds and passes the offline test with audio in; please report what you find.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. RINGS FX

<img src="screenshots/page_0.png" width="760" alt="Rings FX, page RINGS FX">

Q-Link columns: **1** MODEL, POLYPHONY  ·  **2** STRUCTURE, BRIGHTNESS, DAMPING, POSITION  ·  **3** NOTE, FINE  ·  **4** INPUT, MIX, WIDTH, VOLUME

## Install

From the top of this repo (see [INSTALL.md](../../INSTALL.md)):

```
./install.sh <mpc-address> ringsfx
```

## Where it comes from

- Upstream: https://github.com/pichenettes/eurorack (rings/)
- Vendored at commit 08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4 2023-08-16
- Licence: MIT ([`LICENSE`](LICENSE))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Not a Move module: `mpc/ringsfx_engine.cc` drives `rings::Part` with the track's audio as its exciter and Rings' own onset detector as the strummer, at NOTE (MIDI note-ons too, if MPC sends any to an effect). Runs at 44.1 kHz with its tuning corrected; decays ~9% long.
- Same `src/` (with the velocity patch) as `../rings`.
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
| `ringsfx.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `mpc/` | MPC-side glue: the engine wrapper or MIDI/audio adapter, and headers |
| `upstream-changes.diff` | every local change to the upstream source |
| `UPSTREAM` | where the source came from, and at which commit |

To rebuild from source see [BUILDING.md](../../BUILDING.md); to change the screen, [RESKINNING.md](../../RESKINNING.md).
