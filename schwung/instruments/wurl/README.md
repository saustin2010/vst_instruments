# Wurl

**Synth** · A physically modelled Wurlitzer 200A electric piano. · maker in MPC: Filliformes · licence: GPL-3.0

<img src="screenshots/page_0.png" width="760" alt="Wurl on the MPC touchscreen">

A model of the Wurlitzer 200A electric piano (the OpenWurli engine) rather than samples, so it responds to velocity the way the real thing does, from mellow to barking. TREMOLO, BRIGHT, DARKEN, BARK, SPEAKER and REVERB shape it, and ten presets set up different instruments (classic 200A, dreamy keys, barky soul, surf spring, dark ballad...).

## On the MPC

- In the plugin browser: **[SYN] Wurl** by **Filliformes** (Synth)
- Files: the release installs one folder, `/sdcard/Synths/Filliformes - VST - [SYN] Wurl/`, holding `wurl.so`, its screen. The vst_instruments installer puts `wurl.so` in `/sdcard/vst/` instead.
- 11 parameters (all automatable) on 1 page

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. WURL

<img src="screenshots/page_0.png" width="760" alt="Wurl, page WURL">

Q-Link columns: **1** BRIGHT, DARKEN, BARK, TUNE  ·  **2** ATTACK, DECAY, VOLUME  ·  **3** TREMOLO, SPEAKER, REVERB  ·  **4** PRESET

## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `SYN-Wurl-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/mpc-vst-wurl/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.

**With the rest of the collection:** from [vst_instruments](https://github.com/saustin2010/vst_instruments) ([INSTALL.md](https://github.com/saustin2010/vst_instruments/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> wurl
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.

## Where it comes from

- Upstream: https://github.com/filliformes/wurl-move
- Vendored at commit 4061aceed7ca9d8ebac4b8828275cf92e89a0bfc 2026-05-13
- Schwung module "Wurl" v0.1.1 by fillioning (port of OpenWurli by hal0zer0)
- Licence: GPL-3.0 ([`LICENSE`](LICENSE))
- MPC port and screen: [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/wurl`), built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Setting its preset reapplies it, even to the same value: the wrapper skips a set that would not change what the engine reports (generic wrapper fix), so restoring a project keeps your edits.
- `params.base.json` comes from the module's `chain_params` (what the engine actually takes, saved as `chain_params.engine.json`), not its menu tree.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).
- Presets in MPC's PRESET menu (2026-10-03): its 10 presets are VST programs (vst.json `programs`), so the PRESET
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
| `chain_params.engine.json` | what the engine reports it takes (its `chain_params`) |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `wurl.css` | the artwork stylesheet |
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
bash "$MPC_VST/tools/build_port.sh" vst.json        # build/: wurl.so, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](https://github.com/saustin2010/vst_instruments/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](https://github.com/saustin2010/vst_instruments/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/wurl`), next to the other plugins
and the tools that made its screen, and published to [mpc-vst-wurl](https://github.com/saustin2010/mpc-vst-wurl) for its
releases. Issues and pull requests are welcome in either.
