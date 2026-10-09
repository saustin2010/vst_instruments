# Noisemaker

**Synth** · TAL-NoiseMaker: a classic virtual-analog polysynth with 256 factory presets. · maker in MPC: TAL · licence: GPL-2.0

<img src="screenshots/page_0.png" width="760" alt="Noisemaker on the MPC touchscreen">

TAL-NoiseMaker by Patrick Kunz: two oscillators plus sub, 12 multimode filters with their own envelope and velocity response, two LFOs, a third envelope you can draw, chorus, reverb and delay. Six voices. All 256 factory presets are built in, from pads and leads to basses and effects, and folders of your own TAL-NoiseMaker presets (`.noisemakerpreset`) show up as banks.

## On the MPC

- In the plugin browser: **[SYN] Noisemaker** by **TAL** (Synth)
- Files: the release installs one folder, `/sdcard/Synths/TAL - VST - [SYN] Noisemaker/`, holding `noisemaker.so`, its screen. The vst_instruments installer puts `noisemaker.so` in `/sdcard/vst/` instead.
- 91 parameters (all automatable) on 6 pages

## Playing it

Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be turned with the Q-Links and automated, and the settings are saved with your project.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAIN

<img src="screenshots/page_0.png" width="760" alt="Noisemaker, page MAIN">

Q-Link columns: **1** FILTER TYPE, CUTOFF, RESONANCE, FILTER ENV  ·  **2** AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  **3** VOLUME, VOICES, PORTAMENTO, PORTA MODE  ·  **4** PATCH, BANK

### 2. OSC

<img src="screenshots/page_1.png" width="760" alt="Noisemaker, page OSC">

Q-Link columns: **1** OSC1 WAVE, OSC1 TUNE, OSC1 FINE, OSC1 PW  ·  **2** OSC2 WAVE, OSC2 TUNE, OSC2 FINE, OSC2 FM  ·  **3** OSC1 LEVEL, OSC2 LEVEL, SUB LEVEL, RING MOD  ·  **4** OSC SYNC, MASTER TUNE, OSC1 PHASE, OSC2 PHASE

### 3. FILTER

<img src="screenshots/page_2.png" width="760" alt="Noisemaker, page FILTER">

Q-Link columns: **1** KEY TRACK, DRIVE, HIGH PASS, VEL CUTOFF  ·  **2** DETUNE, VINTAGE, BITCRUSH  ·  **3** FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  **4** FLT TIME, AMP TIME, VEL VOLUME, VEL ENV

### 4. LFO

<img src="screenshots/page_3.png" width="760" alt="Noisemaker, page LFO">

Q-Link columns: **1** LFO1 WAVE, LFO1 RATE, LFO1 AMOUNT, LFO1 DEST  ·  **2** LFO1 SYNC, LFO1 KEYTRIG, LFO1 PHASE  ·  **3** LFO2 WAVE, LFO2 RATE, LFO2 AMOUNT, LFO2 DEST  ·  **4** LFO2 SYNC, LFO2 KEYTRIG, LFO2 PHASE

### 5. MOD

<img src="screenshots/page_4.png" width="760" alt="Noisemaker, page MOD">

Q-Link columns: **1** ENV3 ATTACK, ENV3 DECAY, ENV3 AMOUNT, ENV3 DEST  ·  **2** DRAW AMOUNT, DRAW SPEED, DRAW DEST  ·  **3** WHEEL CUTOFF, BEND RANGE  ·  **4** CHORUS I, CHORUS II

### 6. FX

<img src="screenshots/page_5.png" width="760" alt="Noisemaker, page FX">

Q-Link columns: **1** REVERB WET, REV DECAY, REV PREDELAY  ·  **2** REV HI CUT, REV LO CUT  ·  **3** DELAY WET, DLY FEEDBACK, DLY HI CUT, DLY LO CUT  ·  **4** DELAY TIME, DELAY SYNC, DELAY 2X L, DELAY 2X R

## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `SYN-Noisemaker-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/mpc-vst-noisemaker/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.
Your own files in `noisemaker/presets/` are kept when you update or uninstall.

**With the rest of the collection:** from [vst_instruments](https://github.com/saustin2010/vst_instruments) ([INSTALL.md](https://github.com/saustin2010/vst_instruments/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> noisemaker
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.

## Where it comes from

- Schwung module "Noisemaker" v0.2.2 by legsmechanical (engine: Patrick Kunz / TAL)
- Licence: GPL-2.0, as declared in the module's `src/module.json` (text: [LICENSE](LICENSE))
- MPC port and screen: [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/noisemaker`), built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- Presets work: 256 factory presets were never exposed; now a PATCH browser.
- Preset banks (2026-10-03): each folder in `/sdcard/vst/noisemaker/presets/` is a bank of `.noisemakerpreset` files (its subfolders included; up to 64 banks of 512), picked with the new BANK selector under PATCH; PATCH then browses that bank (its first 256), named and sorted by file name. vst.json now sets `MODULE_DIR`, which the engine needs to look there at all. The bank is saved with the project by name; the new parameters are appended (indices 87-90), so saved projects and Q-Link assignments keep working. The scope under them is shorter (`layout.conf` edited by hand, see its header).
- Ten real on/off switches (osc sync, LFO sync/keytrig, Chorus I/II, delay sync/2x) were 0-100 knobs; LFO waves and destinations are now choices.
- Osc 2 tuning no longer drifts when a project is restored: two Move-UI macro parameters wrote through to other settings; their slots are kept (same indices) but the engine no longer sees them.
- **Crash on changing VOICES fixed (2026-10-02).** Changing VOICES (e.g. from 1) while notes were sounding, then playing more notes than voices, took MPC down: upstream's `VoiceManager::setNumberOfVoices` emptied its list of playing notes while the voices kept sounding (and mono mode never lists its note), so the next note's "steal the oldest voice" read past the end of an empty list (`playingNotes.at(-1)`, an uncaught `std::out_of_range`). Local patch in `src/dsp/Engine/VoiceManager.h`, under `#ifdef MPC_PORT` (vst.json defines it): a real change of the voice count releases every voice and starts both note lists afresh, and a steal with an empty list takes voice 1. The original file is in `upstream-changes.diff`. Also `-fwrapv` (the noise generators rely on integer wrap-around). Found and checked with `dev-tools/fuzz/` (`fuzz_port.sh noisemaker 1 2000 0 voices` crashed before, survives after, on one thread and on two).
- The wrapper now serialises every engine call per instance (2026-10-02, all ports): MPC changes parameters on its screen thread while audio runs on another, and this engine (like most) isn't written for that.
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).
- Presets in MPC's PRESET menu (2026-10-03): its presets are VST programs, listed live from the engine (vst.json
  `programs` with `count` and `name_at`; the engine answers a preset's name by number, marked MPC port), so it lists the bank that's loaded and follows a BANK switch.
- **Memory leaks fixed (2026-10-10).** Upstream never freed each voice's Moog 24 dB filter (with its noise source)
  or the Envelope Editor's points: about 1 KB lost per instance. The release workflow's host test runs with
  LeakSanitizer on and refused it. Local patches under `#ifdef MPC_PORT` in `src/dsp/Engine/FilterHandler.h`,
  `FilterMoog24.h` and `src/dsp/EnvelopeEditor/EnvelopeEditor.h`; the originals are in `upstream-changes.diff`.

## Files

| Path | What |
|---|---|
| `screenshots/` | the pages as MPC draws them |
| `.github/workflows/release.yml` | the release build (GitHub Actions, a draft release) |
| `FRAMEWORK.md` | the changes to sd88me's tools this plugin is built with, and why |
| `LICENSE` | the licence |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry (not used by the release) |
| `vst.json` | build settings: name, maker, sources, compiler flags |
| `params.json` | the plugin's parameters as MPC sees them (VST index = order) |
| `params.base.json` | the engine's own parameter list it was derived from |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `noisemaker.css` | the artwork stylesheet |
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
bash "$MPC_VST/tools/build_port.sh" vst.json        # build/: noisemaker.so, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](https://github.com/saustin2010/vst_instruments/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](https://github.com/saustin2010/vst_instruments/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](https://github.com/saustin2010/vst_instruments) (`schwung/instruments/noisemaker`), next to the other plugins
and the tools that made its screen, and published to [mpc-vst-noisemaker](https://github.com/saustin2010/mpc-vst-noisemaker) for its
releases. Issues and pull requests are welcome in either.
