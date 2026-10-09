# Rings

**Synth** · Mutable Instruments Rings: a resonator, strummed by the notes you play, with 14 presets. · maker in MPC: Mutable Instruments · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Rings on the MPC touchscreen">

Rings is a resonator with seven models (a modal resonator, sympathetic strings, a string, an FM voice, sympathetic chords, a string with reverb, and the module's hidden "Disastrous Peace" string synth), shaped by STRUCTURE, BRIGHTNESS, DAMPING and POSITION, with one, two or four voices ringing at once. Here every note strums it with Rings' internal exciter (velocity sets how hard) and the voices rotate as on the module. The string synth's effect (formant, chorus, reverb, ensemble) is SYNTH FX. Runs Mutable's own DSP.

The module has no presets, so the port brings 14 (Init plus Glass Marimba, Tubular Bell, Wood Block, Sympathetic Sitar, Chord Harp, Nylon String, Steel String, Dulcimer, FM Tines, FM Gong, Verb String, Synth Strings and Choir Pad), in MPC's PRESET menu; each sets every control, with levels evened out.

## On the MPC

- In the plugin browser: **[SYN] Rings** by **Mutable Instruments** (Synth)
- Files: the release installs one folder, `/sdcard/Synths/Mutable Instruments - VST - [SYN] Rings/`, holding `rings.so`, its screen. The vst_instruments installer puts `rings.so` in `/sdcard/vst/` instead.
- 12 parameters (all automatable) on 1 page

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. RINGS

<img src="screenshots/page_0.png" width="760" alt="Rings, page RINGS">

Q-Link columns: **1** MODEL, POLYPHONY  ·  **2** STRUCTURE, BRIGHTNESS, DAMPING, POSITION  ·  **3** VELOCITY, OCTAVE, BEND RANGE, SYNTH FX  ·  **4** WIDTH, VOLUME

## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `SYN-Rings-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/mpc-vst-rings/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.

**With the rest of the collection:** from [vst_instruments](https://github.com/saustin2010/vst_instruments) ([INSTALL.md](https://github.com/saustin2010/vst_instruments/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> rings
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.

## Where it comes from

- Upstream: https://github.com/pichenettes/eurorack (rings/)
- Vendored at commit 08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4 2023-08-16
- Licence: MIT ([`LICENSE`](LICENSE))
- MPC port and screen: [vst_instruments](https://github.com/saustin2010/vst_instruments) (`mutable-instruments/rings`), built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Not a Move module: `mpc/rings_engine.cc` drives Mutable's `rings::Part` (and its string synth) directly. MIDI note-on = strum at that note (internal exciter), voices rotate as on the module; pitch bend = FM. Runs at Rings' own 48 kHz, resampled to 44.1 kHz (`mpc/mi_engine.h`).
- `src/rings/dsp/part.*`, `performance_state.h` (`-DMPC_PORT`): velocity scales the internal exciter's strike/pluck (the module has no velocity). Diff: `upstream-changes.diff`.
- `src/stmlib/dsp/dsp.h` (`-DMPC_PORT`, 2026-10-02): `Interpolate` clamps its index; a control at exactly 100% read one past a lookup table (found with dev-tools/fuzz). Diff: `upstream-changes.diff`.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).

## Files

| Path | What |
|---|---|
| `screenshots/` | the pages as MPC draws them |
| `.github/workflows/release.yml` | the release build (GitHub Actions, a draft release) |
| `FRAMEWORK.md` | the changes to sd88me's tools this plugin is built with, and why |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry (not used by the release) |
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

## Building

With Docker (32-bit ARM emulation for the build), Python 3 and the tools from
[saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features) (sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)
plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`, and [FRAMEWORK.md](FRAMEWORK.md) says which changes this plugin uses and why):

```
git clone -b steve-features https://github.com/saustin2010/mpc-vst-plugins
MPC_VST=$PWD/mpc-vst-plugins
bash "$MPC_VST/tools/build_port.sh" vst.json        # build/: rings.so, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](https://github.com/saustin2010/vst_instruments/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](https://github.com/saustin2010/vst_instruments/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](https://github.com/saustin2010/vst_instruments) (`mutable-instruments/rings`), next to the other plugins
and the tools that made its screen, and published to [mpc-vst-rings](https://github.com/saustin2010/mpc-vst-rings) for its
releases. Issues and pull requests are welcome in either.
