# Rampage

**Modulator** · Befaco Rampage: a dual slope generator (envelopes, LFOs, slew) that modulates other tracks. · maker in MPC: Befaco · licence: GPL-3.0

<img src="screenshots/page_0.png" width="760" alt="Rampage on the MPC touchscreen">

Rampage is Befaco's dual function generator: two channels that each make a rise and a fall, with adjustable times, shapes and range. Triggered they're envelopes, gated they hold (attack-sustain-release), cycling they're LFOs, and BALANCE and the MIN/MAX outputs combine both channels. On the MPC, notes on its track trigger or gate each channel, and OUT A/B, MIN and MAX go out of its own MIDI port as CCs you can MIDI-learn on another track; end-of-cycle events go out as notes. Turn AUDIO on to hear the outputs. Ported unchanged from Befaco's VCV Rack code through a small Rack API stand-in.

## On the MPC

- In the plugin browser: **[SEQ] Rampage** by **Befaco** (Sequencer)
- Files: the release installs one folder, `/sdcard/Synths/Befaco - VST - [SEQ] Rampage/`, holding `rampage.so`, its screen. The vst_instruments installer puts `rampage.so` in `/sdcard/vst/` instead.
- 27 parameters (all automatable) on 2 pages

## Playing it

The walk-through, with what to check when nothing plays: [docs/sequencers.md](https://github.com/saustin2010/vst_instruments/blob/main/docs/sequencers.md).

MPC OS ignores a plugin's own MIDI output, so Rampage opens its own MIDI port (MPC lists it as **[SEQ] Rampage MIDI Out**), the way a USB MIDI device would appear. MPC picks the port up without a restart:

1. Put Rampage on a plugin track and play or hold notes into it (pads, keys or a MIDI clip).
2. **Menu → Preferences → MIDI**: switch **Track** on for **[SEQ] Rampage MIDI Out**.
3. On the track to modulate, set **MIDI Input Port** to that port (with **Monitor** on **In**). On this repo's instruments, CC 20-35 move the first page's Q-Links (CC 20-23 = the first column, top to bottom, and so on), so CC OUT A and CC OUT B (MIDI page; 20 and 21 by default) move the first two knobs straight away, no MIDI learn. MIN and MAX send too when given a CC number. Learning Rampage's CCs in MPC's MIDI Learn isn't recommended: with the port's Remote switched on, its stream of CCs froze MPC once (2026-10-04).
4. EOC NOTES sends a short note at the end of each cycle, e.g. to fire a drum pad on another track.

Rampage makes no sound of its own unless AUDIO is on.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. RAMPAGE

<img src="screenshots/page_0.png" width="760" alt="Rampage, page RAMPAGE">

Q-Link columns: **1** A RANGE, A RISE, A FALL, A SHAPE  ·  **2** B RANGE, B RISE, B FALL, B SHAPE  ·  **3** CYCLE A, BALANCE, CYCLE B  ·  **4** AUDIO, VOLUME

### 2. MIDI

<img src="screenshots/page_1.png" width="760" alt="Rampage, page MIDI">

Q-Link columns: **1** A NOTES, B NOTES, A KEY TRACK, B KEY TRACK  ·  **2** CC CHANNEL, CC OUT A, CC OUT B  ·  **3** CC MIN, CC MAX  ·  **4** EOC NOTES, EOC NOTE A, EOC NOTE B

## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `SEQ-Rampage-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/mpc-vst-rampage/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.

**With the rest of the collection:** from [vst_instruments](https://github.com/saustin2010/vst_instruments) ([INSTALL.md](https://github.com/saustin2010/vst_instruments/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> rampage
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.

## Where it comes from

- Upstream: https://github.com/VCVRack/Befaco (src/Rampage.cpp, src/plugin.hpp)
- Vendored at commit bf03788cf11668ba4d371291d6b6dc8377393a24 2026-09-17
- Licence: GPL-3.0 ([`LICENSE`](LICENSE), [`LICENSE.md`](LICENSE.md), [`LICENSE-GPLv3.txt`](LICENSE-GPLv3.txt))
- MPC port and screen: [vst_instruments](https://github.com/saustin2010/vst_instruments) (`vcv-rack/rampage`), built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Not a Move module: Befaco's Rampage from VCVRack/Befaco. `src/Rampage_dsp.hpp` is its module code without the panel widget, unchanged (`dev-tools/rack/extract_module.py`), compiled against `dev-tools/rack/rack_shim.hpp`, a small portable stand-in for the Rack API (Module, ports, float_4).
- `mpc/rampage_engine.cc`: MIDI notes trigger (AR) or gate (ASR) each channel, KEY TRACK drives EXP CV; OUT A/B, MIN and MAX go out as MIDI CC through the plugin's own port (`dev-tools/midiout`), end-of-cycle as notes; AUDIO plays OUT A/B (DC-blocked). Turning CYCLE on kicks the loop once (Rampage only re-fires at a cycle's end).
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
| `params.pre-stitch.json` | the parameter list before the Stitch screen renamed controls |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `rampage.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `mpc/` | MPC-side glue: the engine wrapper or MIDI/audio adapter, and headers |
| `UPSTREAM` | where the source came from, and at which commit |

## Building

With Docker (32-bit ARM emulation for the build), Python 3 and the tools from
[saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features) (sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)
plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`, and [FRAMEWORK.md](FRAMEWORK.md) says which changes this plugin uses and why):

```
git clone -b steve-features https://github.com/saustin2010/mpc-vst-plugins
MPC_VST=$PWD/mpc-vst-plugins
bash "$MPC_VST/tools/build_port.sh" vst.json        # build/: rampage.so, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](https://github.com/saustin2010/vst_instruments/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](https://github.com/saustin2010/vst_instruments/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](https://github.com/saustin2010/vst_instruments) (`vcv-rack/rampage`), next to the other plugins
and the tools that made its screen, and published to [mpc-vst-rampage](https://github.com/saustin2010/mpc-vst-rampage) for its
releases. Issues and pull requests are welcome in either.
