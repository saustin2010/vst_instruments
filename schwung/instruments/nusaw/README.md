# NuSaw

**Synth** · A detuned multi-saw (supersaw) polysynth with 27 presets. · maker in MPC: Charles Vestal · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="NuSaw on the MPC touchscreen">

The supersaw sound of trance and big-room synths: a stack of detuned saws (SAWS, DETUNE, SPREAD) with a sub oscillator, a resonant low-pass filter with its own envelope, an amp envelope, chorus and delay. 27 built-in presets, shown on a cyan LED patch display. Huge pads, stabs and leads with little effort.

## On the MPC

- In the plugin browser: **[SYN] NuSaw** by **Charles Vestal** (Synth)
- Files: the release installs one folder, `/sdcard/Synths/Charles Vestal - VST - [SYN] NuSaw/`, holding `nusaw.so`, its screen. The vst_instruments installer puts `nusaw.so` in `/sdcard/vst/` instead.
- 29 parameters (all automatable) on 2 pages

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAIN

<img src="screenshots/page_0.png" width="760" alt="NuSaw, page MAIN">

Q-Link columns: **1** SAWS, DETUNE, SPREAD, SUB LEVEL  ·  **2** CUTOFF, RESONANCE, ENV MOD  ·  **3** ATTACK, DECAY, SUSTAIN, RELEASE  ·  **4** PATCH

### 2. MORE

<img src="screenshots/page_1.png" width="760" alt="NuSaw, page MORE">

Q-Link columns: **1** FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  **2** SUB OCTAVE, VELOCITY, BEND RANGE, VOLUME  ·  **3** CHORUS, CHORUS DEPTH  ·  **4** DELAY, DELAY TIME, DELAY FBK, DELAY TONE

## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `SYN-NuSaw-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/mpc-vst-nusaw/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.

**With the rest of the collection:** from [vst_instruments](https://github.com/saustin2010/vst_instruments) ([INSTALL.md](https://github.com/saustin2010/vst_instruments/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> nusaw
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.

## Where it comes from

- Upstream: https://github.com/charlesvestal/schwung-nusaw
- Vendored at commit d4d824de1a5679d2d5c6f3667df1176cdba92efa 2026-08-29
- Schwung module "NuSaw" v0.2.2 by charlesvestal
- Licence: MIT ([`LICENSE`](LICENSE))
- MPC port and screen: [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/nusaw`), built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).
- Presets in MPC's PRESET menu (2026-10-03): its 27 presets are VST programs (vst.json `programs`), so the PRESET
  dropdown in the plugin header, also on the arrangement screen, lists and loads them.

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
| `params.pre-stitch.json` | the parameter list before the Stitch screen renamed controls |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `nusaw.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `UPSTREAM` | where the source came from, and at which commit |

## Building

With Docker (32-bit ARM emulation for the build), Python 3 and the tools from
[saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features) (sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)
plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`, and [FRAMEWORK.md](FRAMEWORK.md) says which changes this plugin uses and why):

```
git clone -b steve-features https://github.com/saustin2010/mpc-vst-plugins
MPC_VST=$PWD/mpc-vst-plugins
bash "$MPC_VST/tools/build_port.sh" vst.json        # build/: nusaw.so, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](https://github.com/saustin2010/vst_instruments/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](https://github.com/saustin2010/vst_instruments/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/nusaw`), next to the other plugins
and the tools that made its screen, and published to [mpc-vst-nusaw](https://github.com/saustin2010/mpc-vst-nusaw) for its
releases. Issues and pull requests are welcome in either.
