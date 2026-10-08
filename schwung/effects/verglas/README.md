# Verglas

**Audio effect** · Mutable Instruments Clouds: a granular texture processor as an audio effect, with 12 presets. · maker in MPC: Mutable Instruments · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Verglas on the MPC touchscreen">

Clouds (here Verglas) records the track's audio into a buffer and plays it back as grains: POSITION, SIZE, PITCH, DENSITY and TEXTURE shape the cloud, FREEZE holds the buffer, and MODE switches between granular, stretch, looper and spectral processing. DRY/WET, SPREAD, FEEDBACK and REVERB blend it, a lo-fi QUALITY switch colours it, and the TONE page adds high- and low-pass filters, a low boost and a limiter. Ambient washes, frozen pads and glitchy textures from any sound.

Upstream has no presets, so the port brings 12 (Init, Grain Cloud, Shimmer, Octave Down Haze, Ambient Wash, Stretch Time, Looper Delay, Spectral Smear, Lo-Fi Grains, Stutter, Dark Tail, Warm Tape), in MPC's PRESET menu, each setting every control. Levels are evened out with a test signal through it; where a preset came out quiet, its TONE page limiter is on with a little make-up gain.

## On the MPC

- In the plugin browser: **[FX] Verglas** by **Mutable Instruments** (Audio effect)
- Files: the release installs one folder, `/sdcard/Synths/Mutable Instruments - VST - [FX] Verglas/`, holding `verglas.so`, its screen. The vst_instruments installer puts `verglas.so` in `/sdcard/vst/` instead.
- 20 parameters (all automatable) on 2 pages

## Playing it

An audio effect: insert it in a track's or program's insert effects (it reports two inputs and outputs and the Effect category). It processes 128-sample blocks, about 3 ms of latency at 44.1 kHz.

**Not yet tried on a device:** whether MPC OS offers third-party VST effects in its insert list. The plugin builds and passes the offline test with audio in; please report what you find.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. VERGLAS

<img src="screenshots/page_0.png" width="760" alt="Verglas, page VERGLAS">

Q-Link columns: **1** POSITION, SIZE, PITCH  ·  **2** DENSITY, TEXTURE  ·  **3** MODE, FREEZE, QUALITY  ·  **4** DRY/WET, FEEDBACK, REVERB, SPREAD

### 2. TONE

<img src="screenshots/page_1.png" width="760" alt="Verglas, page TONE">

Q-Link columns: **1** LOW BOOST, LOW FREQ, LOW Q  ·  **2** LIMITER, LIM DRIVE, LIM OUTPUT  ·  **3** HIGH PASS, LOW PASS

## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `FX-Verglas-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/mpc-vst-verglas/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.

**With the rest of the collection:** from [vst_instruments](https://github.com/saustin2010/vst_instruments) ([INSTALL.md](https://github.com/saustin2010/vst_instruments/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> verglas
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.

## Where it comes from

- Upstream: https://github.com/filliformes/verglas-move
- Vendored at commit 581d671da6a754a015e60773067210ec19cd20d2 2026-09-15
- Schwung module "Verglas" v1.2.3 by Emilie Gillet (DSP) / fillioning (port)
- Licence: MIT ([`LICENSE`](LICENSE))
- MPC port and screen: [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/effects/verglas`), built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- `src/clouds_move.cpp`: the port drives Clouds fully wet with `dry_wet = 1.0`, which makes the crossfade lookup read one past its table; now 0.99999 (ASan, 2026-10-01).
- `src/clouds/dsp/grain.h` (Mutable's code): a grain at exactly its peak reads `lut_window[4097]`, one past the table; the index is clamped. Harmless on the module (reads the next flash word), still fixed. Diff: `upstream-changes.diff`.
- `mpc/schwung_audio_fx.c` = `dev-tools/audiofx` (Schwung audio FX -> the MPC engine interface).
- `src/stmlib/dsp/dsp.h` (`-DMPC_PORT`, 2026-10-02): `Interpolate` clamps its index; a control at exactly 100% read one past a lookup table (found with dev-tools/fuzz). Diff: `upstream-changes.diff`.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).

## Files

| Path | What |
|---|---|
| `screenshots/` | the pages as MPC draws them |
| `.github/workflows/release.yml` | the release build (GitHub Actions, a draft release) |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry (not used by the release) |
| `vst.json` | build settings: name, maker, sources, compiler flags |
| `params.json` | the plugin's parameters as MPC sees them (VST index = order) |
| `params.base.json` | the engine's own parameter list it was derived from |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `verglas.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `mpc/` | MPC-side glue: the engine wrapper or MIDI/audio adapter, and headers |
| `upstream-changes.diff` | every local change to the upstream source |
| `UPSTREAM` | where the source came from, and at which commit |

## Building

With Docker (32-bit ARM emulation for the build), Python 3 and the tools from
[saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features) (sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)
plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`):

```
git clone -b steve-features https://github.com/saustin2010/mpc-vst-plugins
MPC_VST=$PWD/mpc-vst-plugins
bash "$MPC_VST/tools/build_port.sh" vst.json        # build/: verglas.so, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](https://github.com/saustin2010/vst_instruments/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](https://github.com/saustin2010/vst_instruments/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/effects/verglas`), next to the other plugins
and the tools that made its screen, and published to [mpc-vst-verglas](https://github.com/saustin2010/mpc-vst-verglas) for its
releases. Issues and pull requests are welcome in either.
