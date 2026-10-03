# MPC VST Instruments

**36 plugins for Akai MPC OS standalone devices**: 22 synths, 2 drum machines, 9 MIDI sequencers and generators, and 3 audio effects. They run inside MPC's own plugin host like Akai's instruments do (pads, keys, clips, Q-Links, automation, saved with the project), and every one has its own native touchscreen page, designed in Google Stitch and converted into MPC's skin format.

<img src="docs/images/gallery.png" alt="The first page of every plugin, as the MPC draws it">

## Thank you, sd88me

None of this would exist without **[sd88me](https://github.com/sd88me)** and his **[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)**. He worked out that MPC OS's built-in plugin host loads Linux VST2 plugins, and built everything these ports stand on: the hand-written VST2 wrapper, the adapter that runs Ableton Move's Schwung modules unchanged, the generator that turns a layout into MPC's native touchscreen pages, Skin Studio and the offline test. These ports started as experiments on top of his framework, and he'll probably bring them into his own repo. Thank you!

Thanks also to NoQuestion and dustyslices, who first described the route on MPC-Forums ("Proof of Concept: Custom Standalone Plugins", September 2026); to Charles Vestal and everyone writing Schwung modules for Ableton Move; to Emilie Gillet for open-sourcing the Mutable Instruments modules; to Befaco and the VCV Rack community; and to every author in [CREDITS.md](CREDITS.md), whose engines these are.

## The plugins

Click a picture for the plugin's page: what it is, every screen, how to play it, where it comes from.

Design QA: ☐ not checked on the MPC yet, ✅ every page checked on the device (names, values, touch controls,
Q-Links, switches, pop-ups and displays, nothing clipped). Edit this file and swap the box to tick one.

### Synths (22)

| | Plugin | What it is | Design QA |
|---|---|---|:---:|
| <a href="schwung/instruments/303/"><img src="schwung/instruments/303/screenshots/page_0.png" width="220" alt="303"></a> | **[303](schwung/instruments/303/)**<br><sub>Synth · Robin Schmidt · GPL-3.0</sub> | TB-303 bass line: Open303 with the Devilfish mods, a drive stage and 13 presets. | ✅ |
| <a href="schwung/instruments/aphex/"><img src="schwung/instruments/aphex/screenshots/page_0.png" width="220" alt="Aphex"></a> | **[Aphex](schwung/instruments/aphex/)**<br><sub>Synth · Filliformes · MIT</sub> | Korg MS-10/MS-20 style mono synth with a patch bay, an ESP section and both MS filters. | ☐ |
| <a href="schwung/instruments/braids/"><img src="schwung/instruments/braids/screenshots/page_0.png" width="220" alt="Braids"></a> | **[Braids](schwung/instruments/braids/)**<br><sub>Synth · Mutable Instruments · MIT</sub> | Mutable Instruments Braids: a macro oscillator with 47 synthesis models. | ☐ |
| <a href="schwung/instruments/chordism/"><img src="schwung/instruments/chordism/screenshots/page_0.png" width="220" alt="Chordism"></a> | **[Chordism](schwung/instruments/chordism/)**<br><sub>Synth · Charles Vestal · MIT</sub> | One key in, a four-voice chord out: morphing oscillators, FM, filter, lo-fi, delay, reverb and an arpeggiator. | ☐ |
| <a href="schwung/instruments/denis/"><img src="schwung/instruments/denis/screenshots/page_0.png" width="220" alt="Denis"></a> | **[Denis](schwung/instruments/denis/)**<br><sub>Synth · Filliformes · MIT</sub> | West Coast / Serge-inspired mono synth: complex oscillators, a wavefolder and a modulation matrix. | ☐ |
| <a href="mutable-instruments/elements/"><img src="mutable-instruments/elements/screenshots/page_0.png" width="220" alt="Elements"></a> | **[Elements](mutable-instruments/elements/)**<br><sub>Synth · Mutable Instruments · MIT</sub> | Mutable Instruments Elements: a modal synthesis voice you bow, blow and strike, with 12 presets. | ☐ |
| <a href="schwung/instruments/fizzik/"><img src="schwung/instruments/fizzik/screenshots/page_0.png" width="220" alt="Fizzik"></a> | **[Fizzik](schwung/instruments/fizzik/)**<br><sub>Synth · Filliformes · MIT</sub> | Physical modelling: an exciter and two coupled resonators (string, beam, plate, membrane). | ☐ |
| <a href="schwung/instruments/hank/"><img src="schwung/instruments/hank/screenshots/page_0.png" width="220" alt="Hank"></a> | **[Hank](schwung/instruments/hank/)**<br><sub>Synth · Charles Vestal · MIT</sub> | Two-operator FM on one page: ratio, brightness, bite, an envelope pair, noise and a tone sweep. | ☐ |
| <a href="schwung/instruments/helm/"><img src="schwung/instruments/helm/screenshots/page_0.png" width="220" alt="Helm"></a> | **[Helm](schwung/instruments/helm/)**<br><sub>Synth · Matt Tytel · GPL-3.0</sub> | Matt Tytel's Helm polysynth with its factory patches (275 with Move Organ). | ☐ |
| <a href="schwung/instruments/hera/"><img src="schwung/instruments/hera/screenshots/page_0.png" width="220" alt="Hera"></a> | **[Hera](schwung/instruments/hera/)**<br><sub>Synth · jpcima · GPL-3.0</sub> | Roland Juno-60: jpcima's Hera engine, with its 56 presets. | ☐ |
| <a href="schwung/instruments/hush1/"><img src="schwung/instruments/hush1/screenshots/page_0.png" width="220" alt="Hush One"></a> | **[Hush One](schwung/instruments/hush1/)**<br><sub>Synth · Move Everything · not stated upstream</sub> | Roland SH-101: a monophonic bass and lead synth with 11 presets, plus TAL-BassLine-101 presets you add. | ☐ |
| <a href="schwung/instruments/monksynth/"><img src="schwung/instruments/monksynth/screenshots/page_0.png" width="220" alt="MonkSynth"></a> | **[MonkSynth](schwung/instruments/monksynth/)**<br><sub>Synth · Jonathan Taylor · MIT</sub> | A formant (FOF) singing voice with 12 characters and a choir. | ☐ |
| <a href="schwung/instruments/monovoice/"><img src="schwung/instruments/monovoice/screenshots/page_0.png" width="220" alt="Mono Voice"></a> | **[Mono Voice](schwung/instruments/monovoice/)**<br><sub>Synth · timncox · MIT</sub> | Elektron Monomachine-style digital voice: SuperWave, SID, DigiPRO, FM and more machines. | ☐ |
| <a href="schwung/instruments/moog/"><img src="schwung/instruments/moog/screenshots/page_0.png" width="220" alt="Moog"></a> | **[Moog](schwung/instruments/moog/)**<br><sub>Synth · Raffo · MIT</sub> | RaffoSynth: a Minimoog-style mono synth with four oscillators and a ladder filter. | ☐ |
| <a href="schwung/instruments/mrhyde/"><img src="schwung/instruments/mrhyde/screenshots/page_0.png" width="220" alt="Mr Hyde"></a> | **[Mr Hyde](schwung/instruments/mrhyde/)**<br><sub>Synth · Move Everything · MIT</sub> | A MicroFreak-inspired voice: Plaits models with a low-pass gate, filter and a 6x6 mod matrix. | ☐ |
| <a href="schwung/instruments/noisemaker/"><img src="schwung/instruments/noisemaker/screenshots/page_0.png" width="220" alt="Noisemaker"></a> | **[Noisemaker](schwung/instruments/noisemaker/)**<br><sub>Synth · TAL · GPL-2.0</sub> | TAL-NoiseMaker: a classic virtual-analog polysynth with 256 factory presets, plus preset banks you add. | ☐ |
| <a href="schwung/instruments/nusaw/"><img src="schwung/instruments/nusaw/screenshots/page_0.png" width="220" alt="NuSaw"></a> | **[NuSaw](schwung/instruments/nusaw/)**<br><sub>Synth · Charles Vestal · MIT</sub> | A detuned multi-saw (supersaw) polysynth with 27 presets. | ☐ |
| <a href="schwung/instruments/obxd/"><img src="schwung/instruments/obxd/screenshots/page_0.png" width="220" alt="OB-Xd"></a> | **[OB-Xd](schwung/instruments/obxd/)**<br><sub>Synth · reales · GPL-3.0</sub> | Oberheim OB-X: reales' OB-Xd with its 128 factory presets, plus `.fxb` banks you add. | ☐ |
| <a href="schwung/instruments/plaits/"><img src="schwung/instruments/plaits/screenshots/page_0.png" width="220" alt="Plaits"></a> | **[Plaits](schwung/instruments/plaits/)**<br><sub>Synth · Mutable Instruments · MIT</sub> | Mutable Instruments Plaits: a macro oscillator with 24 models, played like a synth. | ☐ |
| <a href="mutable-instruments/rings/"><img src="mutable-instruments/rings/screenshots/page_0.png" width="220" alt="Rings"></a> | **[Rings](mutable-instruments/rings/)**<br><sub>Synth · Mutable Instruments · MIT</sub> | Mutable Instruments Rings: a resonator, strummed by the notes you play. | ☐ |
| <a href="schwung/instruments/tablor/"><img src="schwung/instruments/tablor/screenshots/page_0.png" width="220" alt="Tablor"></a> | **[Tablor](schwung/instruments/tablor/)**<br><sub>Synth · athousanddetails · BSD-3-Clause</sub> | A two-oscillator wavetable synth that ships with its wavetables. | ☐ |
| <a href="schwung/instruments/wurl/"><img src="schwung/instruments/wurl/screenshots/page_0.png" width="220" alt="Wurl"></a> | **[Wurl](schwung/instruments/wurl/)**<br><sub>Synth · Filliformes · GPL-3.0</sub> | A physically modelled Wurlitzer 200A electric piano. | ☐ |

### Drum machines (2)

| | Plugin | What it is | Design QA |
|---|---|---|:---:|
| <a href="schwung/instruments/libpo32/"><img src="schwung/instruments/libpo32/screenshots/page_0.png" width="220" alt="Libpo32"></a> | **[Libpo32](schwung/instruments/libpo32/)**<br><sub>Drum synth · mestela · not stated upstream</sub> | PO-32-style drum synth: 16 synthesised drum sounds on pads 36-51. | ☐ |
| <a href="schwung/instruments/mrdrums/"><img src="schwung/instruments/mrdrums/screenshots/page_0.png" width="220" alt="Mrdrums"></a> | **[Mrdrums](schwung/instruments/mrdrums/)**<br><sub>Drum sampler · Move Everything · MIT</sub> | A 16-pad drum sampler: kits of samples on pads 36-51. | ☐ |

### MIDI sequencers and generators (9)

| | Plugin | What it is | Design QA |
|---|---|---|:---:|
| <a href="schwung/sequencers/eucalypso/"><img src="schwung/sequencers/eucalypso/screenshots/page_0.png" width="220" alt="Eucalypso"></a> | **[Eucalypso](schwung/sequencers/eucalypso/)**<br><sub>MIDI sequencer · handcraftedcc · MIT</sub> | Four-lane Euclidean sequencer: turns held notes into interlocking rhythms for other tracks. | ☐ |
| <a href="mutable-instruments/grids/"><img src="mutable-instruments/grids/screenshots/page_0.png" width="220" alt="Grids"></a> | **[Grids](mutable-instruments/grids/)**<br><sub>MIDI sequencer · Mutable Instruments · GPL-3.0</sub> | Mutable Instruments Grids: a topographic drum sequencer that plays a drum track from MPC's clock. | ☐ |
| <a href="schwung/sequencers/groovebank/"><img src="schwung/sequencers/groovebank/screenshots/page_0.png" width="220" alt="Groove Bank"></a> | **[Groove Bank](schwung/sequencers/groovebank/)**<br><sub>MIDI sequencer · Mission Minnow · MIT</sub> | Retriggers the chord you hold in a rhythm from a library of genre grooves. | ☐ |
| <a href="mutable-instruments/marbles/"><img src="mutable-instruments/marbles/screenshots/page_0.png" width="220" alt="Marbles"></a> | **[Marbles](mutable-instruments/marbles/)**<br><sub>MIDI sequencer · Mutable Instruments · MIT</sub> | Mutable Instruments Marbles: a random sampler that plays up to three tracks. | ☐ |
| <a href="schwung/sequencers/mazelite/"><img src="schwung/sequencers/mazelite/screenshots/page_0.png" width="220" alt="Maze Lite"></a> | **[Maze Lite](schwung/sequencers/mazelite/)**<br><sub>MIDI sequencer · sd88me · MIT</sub> | A dual generative sequencer in the style of the Moog Labyrinth. | ☐ |
| <a href="schwung/sequencers/midiplayer/"><img src="schwung/sequencers/midiplayer/screenshots/page_0.png" width="220" alt="MIDI Player"></a> | **[MIDI Player](schwung/sequencers/midiplayer/)**<br><sub>MIDI sequencer · Charles Vestal · MIT</sub> | Plays Standard MIDI Files in time with MPC's transport. | ☐ |
| <a href="schwung/sequencers/pixelwalkers/"><img src="schwung/sequencers/pixelwalkers/screenshots/page_0.png" width="220" alt="Pixel Walkers"></a> | **[Pixel Walkers](schwung/sequencers/pixelwalkers/)**<br><sub>MIDI sequencer · mestela · MIT</sub> | Generative: the notes you play become walkers that bounce and retrigger when they land. | ☐ |
| <a href="vcv-rack/rampage/"><img src="vcv-rack/rampage/screenshots/page_0.png" width="220" alt="Rampage"></a> | **[Rampage](vcv-rack/rampage/)**<br><sub>Modulator · Befaco · GPL-3.0</sub> | Befaco Rampage: a dual slope generator (envelopes, LFOs, slew) that modulates other tracks. | ☐ |
| <a href="schwung/sequencers/superarp/"><img src="schwung/sequencers/superarp/screenshots/page_0.png" width="220" alt="Super Arp"></a> | **[Super Arp](schwung/sequencers/superarp/)**<br><sub>Arpeggiator · handcraftedcc · MIT</sub> | A pattern and rhythm arpeggiator with 40 patterns, 40 rhythms and random modifiers. | ☐ |

### Audio effects (3)

| | Plugin | What it is | Design QA |
|---|---|---|:---:|
| <a href="mutable-instruments/ringsfx/"><img src="mutable-instruments/ringsfx/screenshots/page_0.png" width="220" alt="Rings FX"></a> | **[Rings FX](mutable-instruments/ringsfx/)**<br><sub>Audio effect · Mutable Instruments · MIT</sub> | Rings as an audio effect: the track's sound excites the resonator. | ☐ |
| <a href="schwung/effects/verglas/"><img src="schwung/effects/verglas/screenshots/page_0.png" width="220" alt="Verglas"></a> | **[Verglas](schwung/effects/verglas/)**<br><sub>Audio effect · Mutable Instruments · MIT</sub> | Mutable Instruments Clouds: a granular texture processor as an audio effect. | ☐ |
| <a href="mutable-instruments/warps/"><img src="mutable-instruments/warps/screenshots/page_0.png" width="220" alt="Warps"></a> | **[Warps](mutable-instruments/warps/)**<br><sub>Audio effect · Mutable Instruments · MIT</sub> | Mutable Instruments Warps: a meta-modulator (ring mod, fold, vocoder...) as an audio effect. | ☐ |

## Installing

You need a **Gen1 MPC OS device** (32-bit ARM: e.g. MPC Live, Live II, One, X, Force) with **root SSH access**, and a Mac or Linux computer (Windows: WSL) on the same network. Then:

```
git clone https://github.com/saustin2010/vst_instruments.git
cd vst_instruments
./install.sh <mpc-address> --dry-run all      # check first: changes nothing
./install.sh <mpc-address> hera grids verglas  # or a group: instruments, sequencers, effects, all
```

The first install of a plugin restarts MPC once (it asks first; `MPC.settings` is backed up). **[INSTALL.md](INSTALL.md)** walks through every step: getting SSH access, what the installer changes, adding a plugin to a track, routing the sequencers, updating, uninstalling, installing by hand and troubleshooting. Presets, kits and wavetables are fetched from their original projects into `presets/` and installed with each plugin; [docs/presets-and-libraries.md](docs/presets-and-libraries.md) lists them, where they come from and how to add your own.

## Status (2026-10-03)

- All 36 build and pass the offline test: an x86 build under AddressSanitizer/UBSan that checks every parameter, presets, saving and restoring, Q-Link behaviour, notes to audio, and a stress test that hammers the plugin from two threads as MPC does.
- All 36 run on an MPC Live II (MPC OS 3.9.1) with their Stitch screens (installed 2026-10-03). Moog's screen has been checked on the device; the others are going through an on-device design QA now: see the **Design QA** column above.
- Design QA, first pass (offline, waiting for the device check): 303 (✅), Aphex, Braids, Chordism, Denis, Elements, Fizzik, Hank, Hera, Hush One, MonkSynth, Mono Voice, Mr Hyde, Noisemaker, NuSaw, OB-Xd, Plaits, Rings, Tablor, Wurl, Libpo32, Mr Drums, Rings FX, Verglas, Warps: one Q-Link column per panel, as Moog. Each has a `DESIGN-QA.md` with what changed.
- Presets: every instrument that has presets lists them in MPC's own **PRESET menu** in the plugin header (also on the arrangement screen); checked on the Live II. OB-Xd and Noisemaker get a BANK selector for banks you add, Hush One reads TAL-BassLine-101 presets you add, and the menu follows what's loaded ([docs/presets-and-libraries.md](docs/presets-and-libraries.md)).
- Every screen is checked offline before it ships: each control's binding, the Q-Link layout, the touch areas (`dev-tools/stitch/check_skin.py`) and the Q-Link column outlines MPC highlights (`dev-tools/stitch/qlink_overlay.py`).
- Not yet tried on a device: the sequencers driving other tracks through their own MIDI port on a stock MPC (the mechanism works on a Force), and the three audio effects (whether MPC lists third-party effects at all).
- Plugin browser groups: MPC's plugin menu sorted by type shows only VST Instruments and VST Effects (it ignores the category plugins report); sorted by manufacturer it makes a folder per maker. All plugins keep their real makers.
- Developed on a Live II. Other Gen1 devices run the same MPC software and should behave the same; Gen2 devices (e.g. Live III) are reported to be more locked down. Reports welcome.

What's next is in [ROADMAP.md](ROADMAP.md). Found a problem? Open an issue with the plugin, your MPC model and firmware, and what you did.

## What's in this repo

```
schwung/instruments/   22 instruments ported from Ableton Move "Schwung" modules
schwung/sequencers/     6 MIDI sequencers from Schwung MIDI FX modules
schwung/effects/        1 audio effect from a Schwung audio FX module
mutable-instruments/    6 ported from Mutable Instruments' own firmware source
vcv-rack/               1 ported from a VCV Rack module
  <plugin>/            README.md, screenshots/, deploy/ (ready to install), source, screen design
presets/               the plugins' presets, kits, wavetables (not in git: tools/fetch-presets.py)
install.sh, uninstall.sh, tools/   the installer (and tools/build.sh to build from source)
framework/             our changes to sd88me's mpc-vst-plugins, as a patch, and setup.sh
dev-tools/             the tools the ports and screens were made with
docs/                  extra guides (presets and libraries, root SSH on a stock MPC) and images
licenses/              licence texts the plugins refer to
```

Each plugin's `deploy/` folder is the finished build, so installing needs only this repo; the presets come from their original projects (the installer fetches them). To build from source see [BUILDING.md](BUILDING.md); to change a screen, [RESKINNING.md](RESKINNING.md).

## Licences

Each plugin keeps its upstream licence (GPL-2.0, GPL-3.0, MIT or BSD-3-Clause; two state none): see the plugin's README and [CREDITS.md](CREDITS.md). The GPL plugins' complete source is in their folders. The scripts and documents written for this repo don't have a licence of their own yet.

## Disclaimer

Not affiliated with or endorsed by Akai Professional / inMusic, or by the makers of the instruments these plugins emulate or are named after (Roland, Korg, Oberheim, Moog, Elektron, Wurlitzer, Teenage Engineering, Mutable Instruments, Befaco and others); their names are used only to say what a plugin is modelled on. VST is a trademark of Steinberg; these plugins use a hand-written VST2 interface, not Steinberg's SDK. No Akai content is included. Installing edits MPC's settings file: the installer backs it up first, and you use all of this at your own risk, on hardware you own.
