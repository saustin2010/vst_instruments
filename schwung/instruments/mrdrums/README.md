# Mr Drums

**Drum sampler** · A 16-pad drum sampler: kits of samples on pads 36-51. · maker in MPC: Move Everything · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Mr Drums on the MPC touchscreen">

A drum sample player: each kit is a folder of up to 16 samples on MIDI notes 36-51, so MPC's pads play it. Per pad: volume, pan, tune, start, attack, decay, choke group, gate or one-shot, and random pan, volume and decay plus a play chance for humanised hits. Master volume, polyphony, velocity curve and humanize apply to the kit. Kits live in /sdcard/vst/mrdrums/kits on the MPC (add your own folders there); a starter kit of synthesised hits ships with it.

## On the MPC

- In the plugin browser: **[DRUM] Mr Drums** by **Move Everything** (Drum machine)
- Files: the release installs one folder, `/sdcard/Synths/Move Everything - VST - [DRUM] Mr Drums/`, holding `mrdrums.so`, its screen and its files (`mrdrums/`). The vst_instruments installer puts `mrdrums.so` in `/sdcard/vst/` and its data in `/sdcard/vst/mrdrums/` instead; the plugin finds them either way (its data folder is `mrdrums/` next to the `.so`).
- 26 parameters (all automatable) on 2 pages

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

Its 16 sounds sit on MIDI notes 36-51, one per pad.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. KIT

<img src="screenshots/page_0.png" width="760" alt="Mr Drums, page KIT">

Q-Link columns: **1** MASTER VOL, POLYPHONY, VEL CURVE, HUMANIZE  ·  **2** EDIT PAD, AUTO SELECT  ·  **3** RAND LOOP  ·  **4** KIT

### 2. PAD

<img src="screenshots/page_1.png" width="760" alt="Mr Drums, page PAD">

Q-Link columns: **1** EDIT PAD  ·  **2** PAD VOLUME, PAD PAN, PAD TUNE, PAD START  ·  **3** PAD MODE, CHOKE GROUP, PAD ATTACK, PAD DECAY  ·  **4** RAND VOLUME, RAND PAN, RAND DECAY, CHANCE

## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `DRUM-Mr-Drums-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/mpc-vst-mrdrums/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.
Your own files in `mrdrums/kits/` are kept when you update or uninstall.

**With the rest of the collection:** from [vst_instruments](https://github.com/saustin2010/vst_instruments) ([INSTALL.md](https://github.com/saustin2010/vst_instruments/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> mrdrums
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.

## Where it comes from

- Schwung module "MrDrums" v0.0.4 by move-anything contributors
- Licence: MIT, as declared in the module's `src/module.json` (text: [LICENSE](LICENSE))
- MPC port and screen: [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/mrdrums`), built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- It makes sound now: on the Move, samples are picked in a file browser, which the MPC doesn't have, so it was always silent. A small engine patch (VENDORED.md) adds kit folders: every folder in /sdcard/vst/mrdrums/kits is a kit, its WAV/AIF files in name order go to pads 1-16. The first kit loads automatically.
- Parameter ranges fixed: the pad controls were declared 0-1 while the engine uses real units (volume 0-2, tune ±24, attack/decay in ms, chance in %), so a project restore zeroed decay and chance.
- The pad sample-path slot is retired (a host restoring it as a number would have cleared the pad).
- KIT page (kit browser, master volume, polyphony, velocity curve, humanize, pad select + AUTO SELECT) and PAD page (the selected pad's sound, envelope, randomisation).
- **Crash fix (2026-10-02)**: a project load or a pad sample change no longer frees a sample a voice is still playing (upstream bug; see `VENDORED.md`).
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).
- Presets in MPC's PRESET menu (2026-10-03): its kits are VST programs, listed live from the engine (vst.json
  `programs` with `count` and `name_at`; the engine answers a preset's name by number, marked MPC port), so kit folders you add show up.

## Files

| Path | What |
|---|---|
| `screenshots/` | the pages as MPC draws them |
| `release/` | `make-library.py` (makes its starter files) |
| `.github/workflows/release.yml` | the release build (GitHub Actions, a draft release) |
| `LICENSE` | the licence |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry (not used by the release) |
| `vst.json` | build settings: name, maker, sources, compiler flags |
| `params.json` | the plugin's parameters as MPC sees them (VST index = order) |
| `params.base.json` | the engine's own parameter list it was derived from |
| `params.pre-stitch.json` | the parameter list before the Stitch screen renamed controls |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `mrdrums.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `VENDORED.md` | notes on the vendored source and its patches |

## Building

With Docker (32-bit ARM emulation for the build), Python 3 and the tools from
[saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features) (sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)
plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`):

```
git clone -b steve-features https://github.com/saustin2010/mpc-vst-plugins
MPC_VST=$PWD/mpc-vst-plugins
python3 release/make-library.py library         # its starter files, made by a script here
bash "$MPC_VST/tools/build_port.sh" vst.json        # build/: mrdrums.so, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](https://github.com/saustin2010/vst_instruments/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](https://github.com/saustin2010/vst_instruments/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/mrdrums`), next to the other plugins
and the tools that made its screen, and published to [mpc-vst-mrdrums](https://github.com/saustin2010/mpc-vst-mrdrums) for its
releases. Issues and pull requests are welcome in either.
