# Hank

**Synth** · Two-operator FM on one page: ratio, brightness, bite, an envelope pair, noise and a tone sweep. · maker in MPC: Charles Vestal · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Hank on the MPC touchscreen">

A small, immediate FM synth, as its author puts it: "two-operator FM on eight knobs, one page: ratio, brightness, bite, one envelope pair, noise and a tone sweep". Add voices, glide and transpose, and that's it. 32 built-in presets cover basses, keys, bells and plucks.

## On the MPC

- In the plugin browser: **[SYN] Hank** by **Charles Vestal** (Synth)
- Files: the release installs one folder, `/sdcard/Synths/Charles Vestal - VST - [SYN] Hank/`, holding `hank.so`, its screen. The vst_instruments installer puts `hank.so` in `/sdcard/vst/` instead.
- 16 parameters (all automatable) on 1 page

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. HANK

<img src="screenshots/page_0.png" width="760" alt="Hank, page HANK">

Q-Link columns: **1** RATIO, BRIGHT, BITE (MOD), TONE  ·  **2** ATTACK, DECAY, SUSTAIN  ·  **3** NOISE, GLIDE, VOICES  ·  **4** TRANSPOSE, VOLUME

## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `SYN-Hank-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/mpc-vst-hank/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.

**With the rest of the collection:** from [vst_instruments](https://github.com/saustin2010/vst_instruments) ([INSTALL.md](https://github.com/saustin2010/vst_instruments/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> hank
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.

## Where it comes from

- Schwung module "Hank" v0.6.0 by charlesvestal
- Licence: MIT, as its author declares in the upstream README ("Licence: MIT.") and in `src/module.json`. Upstream has no LICENSE file, so [`LICENSE`](LICENSE) is the standard MIT text naming its author.
- MPC port and screen: [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/hank`), built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Presets work: the preset control only reached presets 0 and 1 (its range comes from the engine at run time, which the build ignored); now a PATCH browser over all 32.
- **Sine lookup fixed (2026-10-02)**: a float rounding edge (a phase of -0.000001 wrapped to exactly 1.0) read one past Hank's sine table. `src/dsp/hank_engine.cpp` under `MPC_PORT` (vst.json defines it); diff in `upstream-changes.diff`. Found with `dev-tools/fuzz`.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).
- Presets in MPC's PRESET menu (2026-10-03): its 32 presets are VST programs (vst.json `programs`), so the PRESET
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
| `hank.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `upstream-changes.diff` | every local change to the upstream source |

## Building

With Docker (32-bit ARM emulation for the build), Python 3 and the tools from
[saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features) (sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)
plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`, and [FRAMEWORK.md](FRAMEWORK.md) says which changes this plugin uses and why):

```
git clone -b steve-features https://github.com/saustin2010/mpc-vst-plugins
MPC_VST=$PWD/mpc-vst-plugins
bash "$MPC_VST/tools/build_port.sh" vst.json        # build/: hank.so, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](https://github.com/saustin2010/vst_instruments/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](https://github.com/saustin2010/vst_instruments/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/hank`), next to the other plugins
and the tools that made its screen, and published to [mpc-vst-hank](https://github.com/saustin2010/mpc-vst-hank) for its
releases. Issues and pull requests are welcome in either.
