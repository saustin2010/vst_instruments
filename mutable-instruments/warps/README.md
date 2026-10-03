# Warps

**Audio effect** · Mutable Instruments Warps: a meta-modulator (ring mod, fold, vocoder...) as an audio effect. · maker in MPC: Mutable Instruments · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Warps on the MPC touchscreen">

Warps crosses two signals: its internal carrier oscillator (sine, triangle or saw at NOTE) and the track's audio, or, with EXTERNAL, the input's left and right channels. ALGORITHM morphs through crossfading, folding, analog and digital ring modulation, XOR, comparison, spectral morphing and a vocoder; TIMBRE adds the character of each. MODE's second entry is the module's hidden frequency shifter. MIX blends the dry sound back.

## On the MPC

- In the plugin browser: **Warps** by **Mutable Instruments** (Effect)
- Files: `/sdcard/vst/warps.so`, screen in `/sdcard/Synths/Mutable Instruments - VST - Warps/`
- 12 parameters (all automatable) on 1 page

## Playing it

An audio effect: insert it in a track's or program's insert effects (it reports two inputs and outputs and the Effect category). It processes 128-sample blocks, about 3 ms of latency at 44.1 kHz.

**Not yet tried on a device:** whether MPC OS offers third-party VST effects in its insert list. The plugin builds and passes the offline test with audio in; please report what you find.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. WARPS

<img src="screenshots/page_0.png" width="760" alt="Warps, page WARPS">

Q-Link columns: **1** MODE, ALGORITHM, TIMBRE, SHIFT  ·  **2** CARRIER, NOTE, FINE, CARRIER LVL  ·  **3** MOD LEVEL  ·  **4** OUTPUT, MIX, VOLUME

## Install

From the top of this repo (see [INSTALL.md](../../INSTALL.md)):

```
./install.sh <mpc-address> warps
```

## Where it comes from

- Upstream: https://github.com/pichenettes/eurorack (warps/)
- Vendored at commit 08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4 2023-08-16
- Licence: MIT ([`LICENSE`](LICENSE))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Not a Move module: `mpc/warps_engine.cc` drives Mutable's `warps::Modulator` at 44.1 kHz (its Init takes the rate). Internal carrier (sine/triangle/saw at NOTE) x the track's audio, or EXTERNAL (left = carrier, right = modulator); MODE 2 = the hidden frequency shifter; MIX blends dry back.
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
| `warps.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `mpc/` | MPC-side glue: the engine wrapper or MIDI/audio adapter, and headers |
| `upstream-changes.diff` | every local change to the upstream source |
| `UPSTREAM` | where the source came from, and at which commit |

To rebuild from source see [BUILDING.md](../../BUILDING.md); to change the screen, [RESKINNING.md](../../RESKINNING.md).
