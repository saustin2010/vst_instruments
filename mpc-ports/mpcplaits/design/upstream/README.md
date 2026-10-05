# MPC Plaits

**Mutable Instruments Plaits as a native instrument for Akai MPC standalone devices.**

All 24 Plaits models, up to 8 voices, LFOs, envelopes and two modulation matrices, synced to the MPC's tempo,
with a touchscreen skin and Q-Link control. It loads from the MPC's own plugin browser like any other instrument.

![The PLAITS page on an MPC One](docs/screenshots/plaits.png)

## Highlights

- **All 24 Plaits models** (firmware 1.2, in the module's order), from virtual analog and FM to chords, speech,
  strings and drums.
- **Polyphonic:** mono, poly or legato, up to 8 voices, with unison, detune and stereo spread.
- **Four ways to play the low-pass gate:** GATE, PING (struck), ENV (driven by an ADSR) and DRONE.
- **Modulation:** two LFOs, two ADSRs, a cycling envelope and a random source, routed through a ready-made 4 x 4 matrix
  plus an assignable one (velocity, aftertouch, mod wheel into FM, LPG, filter, volume, pan and more).
- **Tempo sync:** LFOs, random and retriggers follow the MPC's tempo and restart when it starts playing.
- **A skin built for the MPC screen:** six pages, a live ADSR drawing, and Q-Links that follow the page you're on.

## Requirements

- A **first-generation MPC OS standalone device** (32-bit ARM): MPC One, Live, Live II, X, Key 61, or Force.
- **Root SSH access** to it. Stock MPC OS doesn't offer this, so you need a modded unit.
- Tested on an **MPC One running MPC OS 3.9.1**.

Installing plugins this way is unofficial: back up first, and use it at your own risk.

## Install

1. Download `MPC-Plaits-<version>-mpc-armv7.zip` from [Releases](../../releases).
2. Unzip it and copy the folder to the device:
   ```
   scp -r MPC-Plaits-1.0.0 root@<device-ip>:/tmp/
   ```
3. **Save your project**, then run the installer. It stops MPC, backs up `MPC.settings`, registers the plugin and
   starts MPC again:
   ```
   ssh root@<device-ip> sh /tmp/MPC-Plaits-1.0.0/install.sh
   ```
4. Add **MPC Plaits** to a track from the plugin browser (Instrument plugins).

> **MPC One and Live:** the SD card is mounted `noexec`, so a plugin can't load from it. Install into a folder on the
> internal storage instead, and list that folder under `SynthContentLocations` in `MPC.settings`:
> `sh install.sh -t /media/az01-internal/Synths`

`INSTALL.md` inside the zip has the full steps, including installing by hand.

## The pages

### PLAITS

The module panel. Pick a model with the LEDs, the arrows or the drop-down list, and shape it with FREQUENCY,
HARMONICS, TIMBRE and MORPH. The three attenuverters (TIMBRE, FM, MORPH) set how far the per-note decay envelope moves
each one, as on the hardware. OUT/AUX blends the model's two outputs.

### VOICE

![VOICE page](docs/screenshots/voice.png)

| Section | Controls |
|---|---|
| **AMP** | GATE (the gate follows the key), PING (struck, rings out with DECAY), ENV (ENV 1 drives the gate), DRONE (free-running). LPG COLOR, DECAY, and TRIG RATE: synced retriggers while a key is held, so Chiptune arpeggiates and drums ratchet. |
| **VOICES** | MONO, POLY or LEGATO. NOTES (voice count), UNISON, DETUNE, SPREAD. |
| **PLAY** | GLIDE, pitch BEND range, velocity and aftertouch curves. |
| **OUTPUT** | Low-, band- or high-pass filter with CUTOFF and RESO, VOLUME and PAN. |

### ENV

![ENV page](docs/screenshots/env.png)

Two per-voice ADSRs, each drawn live as you turn its knobs. ENV 1 is also the amplitude envelope in the ENV and DRONE
amp modes. ENV 2 is a modulation source.

### LFO

![LFO page](docs/screenshots/lfo.png)

Two LFOs (six shapes, free or synced to tempo, retrigger, per-voice, phase), a cycling envelope (linear, exponential or
logarithmic, unipolar or bipolar) and a random source (sample-and-hold, smooth or drift).

### MOD

![MOD page](docs/screenshots/mod.png)

A fixed 4 x 4 matrix: LFO 1, ENV 2, CYCLE and RANDOM into PITCH, HARMONICS, TIMBRE and MORPH. Nothing to set up: turn a
knob and it's routed. Each cell has an on/off switch, so you can mute a route without losing its amount.

### ASSIGN

![ASSIGN page](docs/screenshots/assign.png)

A second 4 x 4 matrix where you choose the sources (including velocity, aftertouch and mod wheel) and destinations
(including FM, LPG decay, filter, volume, pan and OUT/AUX). It starts routed as LFO 2, velocity, aftertouch and mod wheel
into FM, LPG decay, cutoff and volume.

Every matrix amount is bipolar and uses Plaits' own attenuverter curve, which is fine-grained around the centre.

**MIDI:** notes, velocity, sustain pedal, pitch bend, mod wheel, poly and channel aftertouch.

## CPU

Measured on an MPC One with `tools/bench.sh`, as a share of one 128-sample audio block:

| Load | p99 |
|---|---|
| 1 voice | ~3% |
| 4 to 16 voices | ~8% |
| Release tail | ~11% |
| Fast Q-Link sweep | ~30% |

The Q-Link sweep peak comes from Plaits recomputing the model's parameters on every step.

## Building from source

<details>
<summary>Build, install from source, screenshots and tests</summary>

### Build

Needs Docker and a sibling checkout of [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) on its
**`poloq-dev`** branch, which has transport sync, popup `wheel=1`, `settle()` and per-column Q-Link outlines that
aren't upstream yet. Set `MPC_VST` if it isn't at `../mpc-vst-plugins`.

```
./build.sh
```

Outputs `vst/build/plaits.so`, `vst/build/skin/poloq - VST - MPC Plaits/` and `vst/build/pluginlist-entry.xml`.

### Install from source (MPC One)

```
./install_mpc_one.sh [--bench bench.json]
```

Packages the build as a release zip (`tools/release.py`, catalog manifest included) and installs it on the MPC reachable
as `ssh mpc`, into `/media/az01-internal/Synths`. That folder must be listed in `MPC.settings`'
`SynthContentLocations`. The installer stops MPC, backs up `MPC.settings`, replaces any entry with the same uid and
restarts MPC. `--bench` adds `tools/bench.sh -j` results to the release notes.

MPC never cleans its skin image cache (`/var/tmp/filmstrips`, on the same partition). If space runs low, clear it with
MPC stopped.

### Screenshots

```
tools/screenshot.sh <name>
```

Saves what the MPC's screen shows right now to `docs/screenshots/<name>.png` (1280 x 800). MPC draws through the GPU,
so this reads the display with ffmpeg's `kmsgrab` rather than the (empty) framebuffer.

### Tests

- `TEST=tests/<name>.c tests/run_c.sh` builds one host test (`controls`, `notes`, `sustain`, `voices`, `voicing`,
  `modulation`, `deadvoice`, `dump_defaults`) with ASan/UBSan in Docker and runs it. After editing a header,
  `rm -rf tests/build` first. `notes` takes a mode argument (`-1` = all): `TEST=tests/notes.c tests/run_c.sh -1`.
- `tests/run_fuzz.sh [blocks] [seed]` runs the random-input fuzzer (`tests/fuzz.c`).

Offline host tests pass under ASan/UBSan, and fuzzing on x86 and 32-bit ARM is clean. The armhf build exports a single
symbol (`VSTPluginMain`) and needs glibc 2.27 or later.

### Versioning

The uid (`MiPl`), `plaits.so` and the catalog id `mpc-plaits` never change. The major version goes up only when
parameter positions change, because MPC stores a project's plugin values by position.

</details>

## Skin artwork

Modelled on VCV Rack's "Macro Oscillator 2" panel. The icons, chevron band and OUT/AUX lettering come from Mutable
Instruments' own panel artwork (`plaits_v50.ai`, © Emilie Gillet, CC-BY-SA 3.0), kept as
`vst/art/src/plaits_v50_panel.svg`. `vst/art/make_art.py` (needs `rsvg-convert` and ImageMagick) builds `vst/images/`
from it: the background, knob images and one LED-row picture per model. The derived artwork stays under CC-BY-SA 3.0.
Nothing is taken from the VCV panel files themselves.

## Credits

Port, skin and MPC integration by **poloq** ([poloq-instruments](https://github.com/poloq-instruments)).

Built on:

- **Emilie Gillet / Mutable Instruments**: [Plaits](https://github.com/pichenettes/eurorack/tree/master/plaits), the
  original module and its DSP (MIT), and the panel artwork the skin is built from (CC-BY-SA 3.0).
- **[handcraftedcc](https://github.com/handcraftedcc)**: [schwung-mrhyde](https://github.com/handcraftedcc/schwung-mrhyde),
  the Plaits build for Ableton Move whose bridge is vendored here (see `src/VENDORED.md` for what was reused, dropped
  and changed).
- **[charlesvestal](https://github.com/charlesvestal)**: [Schwung](https://github.com/charlesvestal/schwung) (formerly
  Move Anything), the plugin API that bridge targets.
- **[sd88me](https://github.com/sd88me)**: [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins), the MPC OS
  VST2 wrapper, Schwung adapter and build and skin tooling this port is built with.
- **VCV**: [Audible Instruments](https://github.com/VCVRack/AudibleInstruments)' "Macro Oscillator 2", the layout
  reference for the skin (no files taken from it).

## License

MIT, see [`LICENSE`](LICENSE), which also lists the third-party components. The skin artwork derived from Mutable
Instruments' panel is CC-BY-SA 3.0.

*Not affiliated with or endorsed by Akai Professional or Mutable Instruments.*
