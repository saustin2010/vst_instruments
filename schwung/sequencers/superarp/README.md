# Super Arp

**Arpeggiator** · A pattern and rhythm arpeggiator with 40 patterns, 40 rhythms and random modifiers. · maker in MPC: handcraftedcc · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Super Arp on the MPC touchscreen">

An arpeggiator that separates the note order from the rhythm: a MODE (up, down, as played, leaps, chord) or one of 40 PATTERNs sets which note comes next, and one of 40 RHYTHMs sets when, so the same chord can roll, skip and syncopate in many ways. Drops, octave and note randomisation, and velocity and gate randomisation all have seeds, so a random result repeats until you change it. Hold a chord on its track: the arpeggio follows MPC's tempo (or its own BPM) and goes out of its own MIDI port to the tracks you point at it.

## On the MPC

- In the plugin browser: **[SEQ] Super Arp** by **handcraftedcc** (Sequencer)
- Files: the release installs one folder, `/sdcard/Synths/handcraftedcc - VST - [SEQ] Super Arp/`, holding `superarp.so`, its screen. The vst_instruments installer puts `superarp.so` in `/sdcard/vst/` instead.
- 37 parameters (all automatable) on 3 pages

## Playing it

The walk-through, with what to check when nothing plays: [docs/sequencers.md](https://github.com/saustin2010/vst_instruments/blob/main/docs/sequencers.md).

MPC OS ignores a plugin's own MIDI output, so Super Arp opens its own MIDI port (MPC lists it as **[SEQ] Super Arp MIDI Out**), the way a USB MIDI device would appear. MPC picks the port up without a restart:

1. Put Super Arp on a plugin track and play or hold notes into it (pads, keys or a MIDI clip).
2. **Menu → Preferences → MIDI**: switch **Track** on for **[SEQ] Super Arp MIDI Out**.
3. On the track(s) that should play, set **MIDI Input Port** to that port and **Monitor** to **In** (not Auto, which only listens while that track is selected). Not on Super Arp's own track, or it hears itself.
4. Press play: it follows MPC's tempo and transport (MIDI clock from the plugin host).

Super Arp makes no sound of its own.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAIN

<img src="screenshots/page_0.png" width="760" alt="Super Arp, page MAIN">

Q-Link columns: **1** SYNC, RATE, TRIPLET, TEMPO (BPM)  ·  **2** LATCH STATE, OCTAVES  ·  **3** GATE, VELOCITY, SWING, VOICES

### 2. PATTERN

<img src="screenshots/page_1.png" width="760" alt="Super Arp, page PATTERN">

Q-Link columns: **1** MODE, PATTERN  ·  **2** MODE TRIGGER, MISSING NOTE, MODE SEED  ·  **3** RHYTHM, RHY TRIGGER  ·  **4** RND LENGTH, RND CHORDS, RND CH SEED

### 3. MODIFY

<img src="screenshots/page_2.png" width="760" alt="Super Arp, page MODIFY">

Q-Link columns: **1** MOD LOOP, MOD TRIGGER, DROP, DROP SEED  ·  **2** VEL RANDOM, VEL SEED, GATE RANDOM, GATE SEED  ·  **3** OCT RANDOM, OCT RANGE, OCT SEED  ·  **4** NOTE RANDOM, NOTE SEED

## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `SEQ-Super-Arp-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/mpc-vst-superarp/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.

**With the rest of the collection:** from [vst_instruments](https://github.com/saustin2010/vst_instruments) ([INSTALL.md](https://github.com/saustin2010/vst_instruments/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> superarp
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.

## Where it comes from

- Upstream: https://github.com/handcraftedcc/move-everything-superarp
- Vendored at commit 6eefd02af91823e330f7ce86883dc097b634ecc1 2026-03-23
- Licence: MIT ([`LICENSE`](LICENSE))
- MPC port and screen: [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/sequencers/superarp`), built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- `mpc/` = the MIDI FX adapter (`dev-tools/midifx/schwung_midi_fx.c`), Schwung's host headers and `wrapper/engine.h`. The adapter opens an ALSA MIDI port named after the plugin, sends the module's notes there, and turns MPC's transport (tempo, position, play/stop) into the 24-PPQN MIDI clock and Start/Stop the module expects.
- `mpc/host/plugin_api_v1.h`: its `offsetof(reserved) == 120` assert is a 64-bit (Move) layout check, so it is skipped on the MPC's 32-bit ARM (the module and adapter share the one header).
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
| `params.pre-stitch.json` | the parameter list before the Stitch screen renamed controls |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `superarp.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `mpc/` | MPC-side glue: the engine wrapper or MIDI/audio adapter, and headers |
| `UPSTREAM` | where the source came from, and at which commit |

## Building

With Docker (32-bit ARM emulation for the build), Python 3 and the tools from
[saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features) (sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)
plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`):

```
git clone -b steve-features https://github.com/saustin2010/mpc-vst-plugins
MPC_VST=$PWD/mpc-vst-plugins
bash "$MPC_VST/tools/build_port.sh" vst.json        # build/: superarp.so, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](https://github.com/saustin2010/vst_instruments/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](https://github.com/saustin2010/vst_instruments/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/sequencers/superarp`), next to the other plugins
and the tools that made its screen, and published to [mpc-vst-superarp](https://github.com/saustin2010/mpc-vst-superarp) for its
releases. Issues and pull requests are welcome in either.
