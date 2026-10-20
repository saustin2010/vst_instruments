# MPC VST Instruments

**43 plugins for Akai MPC OS standalone devices**: 24 synths, 4 drum machines, 11 MIDI sequencers and generators, and 4 audio effects. They run inside MPC's own plugin host like Akai's instruments do (pads, keys, clips, Q-Links, automation, saved with the project), and every one has its own native touchscreen page, designed in Google Stitch (or, for the Stevequencers, a browser prototype and a generator; for Percolator, a generator) and converted into MPC's skin format; the two made for MPC by other authors keep their own.

<img src="docs/images/gallery.png" alt="The first page of every plugin, as the MPC draws it">

## Thank you, sd88me

None of this would exist without **[sd88me](https://github.com/sd88me)** and his **[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)**. He worked out that MPC OS's built-in plugin host loads Linux VST2 plugins, and built everything these ports stand on: the hand-written VST2 wrapper, the adapter that runs Ableton Move's Schwung modules unchanged, the generator that turns a layout into MPC's native touchscreen pages, Skin Studio and the offline test. These ports started as experiments on top of his framework, and he'll probably bring them into his own repo. Thank you!

Thanks also to NoQuestion and dustyslices, who first described the route on MPC-Forums ("Proof of Concept: Custom Standalone Plugins", September 2026); to Charles Vestal and everyone writing Schwung modules for Ableton Move; to Emilie Gillet for open-sourcing the Mutable Instruments modules; to Befaco and the VCV Rack community; and to every author in [CREDITS.md](CREDITS.md), whose engines these are.

## The plugins

Click a picture for the plugin's page: what it is, every screen, how to play it, where it comes from.

Design QA: ☐ not checked on the MPC yet, ✅ every page checked on the device (names, values, touch controls,
Q-Links, switches, pop-ups and displays, nothing clipped). Edit this file and swap the box to tick one.

### Synths (24)

| | Plugin | What it is | Design QA |
|---|---|---|:---:|
| <a href="schwung/instruments/aphex/"><img src="schwung/instruments/aphex/screenshots/page_0.png" width="220" alt="Aphex"></a> | **[Aphex](schwung/instruments/aphex/)**<br><sub>Synth · Filliformes · MIT</sub> | Korg MS-10/MS-20 style mono synth with a patch bay, an ESP section and both MS filters. | ✅ |
| <a href="schwung/instruments/chordism/"><img src="schwung/instruments/chordism/screenshots/page_0.png" width="220" alt="Chordism"></a> | **[Chordism](schwung/instruments/chordism/)**<br><sub>Synth · Charles Vestal · MIT</sub> | One key in, a four-voice chord out: morphing oscillators, FM, filter, lo-fi, delay, reverb and an arpeggiator. | ✅ |
| <a href="schwung/instruments/denis/"><img src="schwung/instruments/denis/screenshots/page_0.png" width="220" alt="Denis"></a> | **[Denis](schwung/instruments/denis/)**<br><sub>Synth · Filliformes · MIT</sub> | West Coast / Serge-inspired mono synth: complex oscillators, a wavefolder and a modulation matrix. | ✅ |
| <a href="schwung/instruments/fizzik/"><img src="schwung/instruments/fizzik/screenshots/page_0.png" width="220" alt="Fizzik"></a> | **[Fizzik](schwung/instruments/fizzik/)**<br><sub>Synth · Filliformes · MIT</sub> | Physical modelling: an exciter and two coupled resonators (string, beam, plate, membrane). | ✅ |
| <a href="schwung/instruments/hank/"><img src="schwung/instruments/hank/screenshots/page_0.png" width="220" alt="Hank"></a> | **[Hank](schwung/instruments/hank/)**<br><sub>Synth · Charles Vestal · MIT</sub> | Two-operator FM on one page: ratio, brightness, bite, an envelope pair, noise and a tone sweep. | ✅ |
| <a href="schwung/instruments/helm/"><img src="schwung/instruments/helm/screenshots/page_0.png" width="220" alt="Helm"></a> | **[Helm](schwung/instruments/helm/)**<br><sub>Synth · Matt Tytel · GPL-3.0</sub> | Matt Tytel's Helm polysynth with its factory patches (275 with Move Organ). | ✅ |
| <a href="schwung/instruments/hera/"><img src="schwung/instruments/hera/screenshots/page_0.png" width="220" alt="Hera"></a> | **[Hera](schwung/instruments/hera/)**<br><sub>Synth · jpcima · GPL-3.0</sub> | Roland Juno-60: jpcima's Hera engine, with its 56 presets. | ✅ |
| <a href="schwung/instruments/hush1/"><img src="schwung/instruments/hush1/screenshots/page_0.png" width="220" alt="Hush One"></a> | **[Hush One](schwung/instruments/hush1/)**<br><sub>Synth · Move Everything · not stated upstream</sub> | Roland SH-101: a monophonic bass and lead synth with 11 presets, plus TAL-BassLine-101 presets you add. | ✅ |
| <a href="schwung/instruments/braids/"><img src="schwung/instruments/braids/screenshots/page_0.png" width="220" alt="Macro Osc"></a> | **[Macro Osc](schwung/instruments/braids/)**<br><sub>Synth · Émilie Gillet · MIT</sub> | Macro Osc: a macro oscillator with 47 synthesis models, from Émilie Gillet's open-source Braids. | ✅ |
| <a href="schwung/instruments/plaits/"><img src="schwung/instruments/plaits/screenshots/page_0.png" width="220" alt="Macro Osc 2"></a> | **[Macro Osc 2](schwung/instruments/plaits/)**<br><sub>Synth · Émilie Gillet · MIT</sub> | Macro Osc 2: a macro oscillator with 24 models, played like a synth, with 24 presets. | ✅ |
| <a href="mutable-instruments/elements/"><img src="mutable-instruments/elements/screenshots/page_0.png" width="220" alt="Modal Voice"></a> | **[Modal Voice](mutable-instruments/elements/)**<br><sub>Synth · Émilie Gillet · MIT</sub> | Modal Voice: a modal synthesis voice you bow, blow and strike, with 12 presets, from Émilie Gillet's open-source Elements. | ✅ |
| <a href="schwung/instruments/monksynth/"><img src="schwung/instruments/monksynth/screenshots/page_0.png" width="220" alt="MonkSynth"></a> | **[MonkSynth](schwung/instruments/monksynth/)**<br><sub>Synth · Jonathan Taylor · MIT</sub> | A formant (FOF) singing voice with 12 characters and a choir. | ✅ |
| <a href="schwung/instruments/monovoice/"><img src="schwung/instruments/monovoice/screenshots/page_0.png" width="220" alt="Mono Voice"></a> | **[Mono Voice](schwung/instruments/monovoice/)**<br><sub>Synth · timncox · MIT</sub> | Elektron Monomachine-style digital voice: SuperWave, SID, DigiPRO, FM and more machines; 12 factory patches. | ✅ |
| <a href="mpc-ports/mpcplaits/"><img src="mpc-ports/mpcplaits/screenshots/page_0.png" width="220" alt="MPC Plaits"></a> | **[MPC Plaits](mpc-ports/mpcplaits/)**<br><sub>Synth · poloq · MIT</sub> | poloq's polyphonic Plaits for MPC: all 24 models, up to 8 voices, four low-pass gate modes, LFOs, envelopes and two mod matrices, on the module's own panel art; 31 presets. | ☐ |
| <a href="schwung/instruments/mrhyde/"><img src="schwung/instruments/mrhyde/screenshots/page_0.png" width="220" alt="Mr Hyde"></a> | **[Mr Hyde](schwung/instruments/mrhyde/)**<br><sub>Synth · Move Everything · MIT</sub> | A MicroFreak-inspired voice: Plaits models with a low-pass gate, filter and a 6x6 mod matrix; 19 presets. | ✅ |
| <a href="mpc-ports/mutablevibe/"><img src="mpc-ports/mutablevibe/screenshots/page_0.png" width="220" alt="Mutable Vibe"></a> | **[Mutable Vibe](mpc-ports/mutablevibe/)**<br><sub>Synth · nachtaktiv303 · MIT</sub> | nachtaktiv303's Rings + Plaits instrument for MPC: 20 resonator and macro-oscillator engines with a filter, four LFOs, four envelopes, reverb, delay, chorus and drive; 24 presets. | ☐ |
| <a href="schwung/instruments/noisemaker/"><img src="schwung/instruments/noisemaker/screenshots/page_0.png" width="220" alt="Noisemaker"></a> | **[Noisemaker](schwung/instruments/noisemaker/)**<br><sub>Synth · TAL · GPL-2.0</sub> | TAL-NoiseMaker: a classic virtual-analog polysynth with 256 factory presets, plus preset banks you add. | ✅ |
| <a href="schwung/instruments/nusaw/"><img src="schwung/instruments/nusaw/screenshots/page_0.png" width="220" alt="NuSaw"></a> | **[NuSaw](schwung/instruments/nusaw/)**<br><sub>Synth · Charles Vestal · MIT</sub> | A detuned multi-saw (supersaw) polysynth with 27 presets. | ✅ |
| <a href="schwung/instruments/obxd/"><img src="schwung/instruments/obxd/screenshots/page_0.png" width="220" alt="OB-Xd"></a> | **[OB-Xd](schwung/instruments/obxd/)**<br><sub>Synth · reales · GPL-3.0</sub> | Oberheim OB-X: reales' OB-Xd with its 128 factory presets, plus `.fxb` banks you add. | ✅ |
| <a href="schwung/instruments/303/"><img src="schwung/instruments/303/screenshots/page_0.png" width="220" alt="Open303"></a> | **[Open303](schwung/instruments/303/)**<br><sub>Synth · Robin Schmidt · GPL-3.0</sub> | TB-303 bass line: Open303 with the Devilfish mods, a drive stage and 13 presets. | ✅ |
| <a href="schwung/instruments/moog/"><img src="schwung/instruments/moog/screenshots/page_0.png" width="220" alt="RaffoSynth"></a> | **[RaffoSynth](schwung/instruments/moog/)**<br><sub>Synth · Raffo · MIT</sub> | RaffoSynth: a Minimoog-style mono synth with four oscillators and a ladder filter. | ✅ |
| <a href="mutable-instruments/rings/"><img src="mutable-instruments/rings/screenshots/page_0.png" width="220" alt="Resonator"></a> | **[Resonator](mutable-instruments/rings/)**<br><sub>Synth · Émilie Gillet · MIT</sub> | Resonator: a resonator strummed by the notes you play, with 14 presets, from Émilie Gillet's open-source Rings. | ✅ |
| <a href="schwung/instruments/tablor/"><img src="schwung/instruments/tablor/screenshots/page_0.png" width="220" alt="Tablor"></a> | **[Tablor](schwung/instruments/tablor/)**<br><sub>Synth · athousanddetails · BSD-3-Clause</sub> | A two-oscillator wavetable synth that ships with its wavetables; 9 factory presets. | ✅ |
| <a href="schwung/instruments/wurl/"><img src="schwung/instruments/wurl/screenshots/page_0.png" width="220" alt="Wurl"></a> | **[Wurl](schwung/instruments/wurl/)**<br><sub>Synth · Filliformes · GPL-3.0</sub> | A physically modelled Wurlitzer 200A electric piano. | ✅ |

### Drum machines (4)

| | Plugin | What it is | Design QA |
|---|---|---|:---:|
| <a href="originals/incipience/"><img src="originals/incipience/screenshots/page_0.png" width="220" alt="Incipience"></a> | **[Incipience](originals/incipience/)**<br><sub>Drum synth · Steve A · written for this repo</sub> | Eleven instruments laid out like the Jomox AlphaBase Mk II: an analog-style kick, the MBrane's coupled resonators, hats, clap, rim and cymbals from samples and metal noise through self-oscillating filters, two sample players and a 4-operator FM synth; reverb and delay sends; plays the AlphaBase's free sample set. **In testing.** | ☐ |
| <a href="schwung/instruments/libpo32/"><img src="schwung/instruments/libpo32/screenshots/page_0.png" width="220" alt="Libpo32"></a> | **[Libpo32](schwung/instruments/libpo32/)**<br><sub>Drum synth · mestela · not stated upstream</sub> | PO-32-style drum synth: 16 synthesised drum sounds on pads 36-51. | ✅ |
| <a href="schwung/instruments/mrdrums/"><img src="schwung/instruments/mrdrums/screenshots/page_0.png" width="220" alt="Mr Drums"></a> | **[Mr Drums](schwung/instruments/mrdrums/)**<br><sub>Drum sampler · Move Everything · MIT</sub> | A 16-pad drum sampler: kits of samples on pads 36-51. | ✅ |
| <a href="originals/percolator/"><img src="originals/percolator/screenshots/page_1.png" width="220" alt="Percolator"></a> | **[Percolator](originals/percolator/)**<br><sub>Drum synth · Steve A · written for this repo</sub> | Four voices laid out like the Erica Synths Pērkons HD-01: three algorithms and three modes each, a BBD delay, an LFO to every knob, envelope displays; plays the Pērkons' free kit packs. 2.1.0 (in testing): your own WAV files on any voice, and RANDOMISE per voice. | ✅ |

### MIDI sequencers and generators (11)

| | Plugin | What it is | Design QA |
|---|---|---|:---:|
| <a href="mutable-instruments/grids/"><img src="mutable-instruments/grids/screenshots/page_0.png" width="220" alt="Drum Map"></a> | **[Drum Map](mutable-instruments/grids/)**<br><sub>MIDI sequencer · Émilie Gillet · GPL-3.0</sub> | Drum Map: a topographic drum sequencer that plays a drum track from MPC's clock, from Émilie Gillet's open-source Grids. | ☐ |
| <a href="vcv-rack/rampage/"><img src="vcv-rack/rampage/screenshots/page_0.png" width="220" alt="Dual Slope"></a> | **[Dual Slope](vcv-rack/rampage/)**<br><sub>Modulator · Andrew Belt · GPL-3.0</sub> | Dual Slope: two rise-and-fall generators (envelopes, LFOs, slew) that modulate other tracks, based on Befaco's Rampage VCV Rack module. | ☐ |
| <a href="schwung/sequencers/eucalypso/"><img src="schwung/sequencers/eucalypso/screenshots/page_0.png" width="220" alt="Eucalypso"></a> | **[Eucalypso](schwung/sequencers/eucalypso/)**<br><sub>MIDI sequencer · handcraftedcc · MIT</sub> | Four-lane Euclidean sequencer: turns held notes into interlocking rhythms for other tracks. | ☐ |
| <a href="schwung/sequencers/groovebank/"><img src="schwung/sequencers/groovebank/screenshots/page_0.png" width="220" alt="Groove Bank"></a> | **[Groove Bank](schwung/sequencers/groovebank/)**<br><sub>MIDI sequencer · Mission Minnow · MIT</sub> | Retriggers the chord you hold in a rhythm from a library of genre grooves. | ☐ |
| <a href="schwung/sequencers/mazelite/"><img src="schwung/sequencers/mazelite/screenshots/page_0.png" width="220" alt="Maze Lite"></a> | **[Maze Lite](schwung/sequencers/mazelite/)**<br><sub>MIDI sequencer · sd88me · MIT</sub> | A dual generative sequencer in the style of the Moog Labyrinth. | ☐ |
| <a href="schwung/sequencers/midiplayer/"><img src="schwung/sequencers/midiplayer/screenshots/page_0.png" width="220" alt="MIDI Player"></a> | **[MIDI Player](schwung/sequencers/midiplayer/)**<br><sub>MIDI sequencer · Charles Vestal · MIT</sub> | Plays Standard MIDI Files in time with MPC's transport. | ☐ |
| <a href="schwung/sequencers/pixelwalkers/"><img src="schwung/sequencers/pixelwalkers/screenshots/page_0.png" width="220" alt="Pixel Walkers"></a> | **[Pixel Walkers](schwung/sequencers/pixelwalkers/)**<br><sub>MIDI sequencer · mestela · MIT</sub> | Generative: the notes you play become walkers that bounce and retrigger when they land. | ☐ |
| <a href="mutable-instruments/marbles/"><img src="mutable-instruments/marbles/screenshots/page_0.png" width="220" alt="Random Sampler"></a> | **[Random Sampler](mutable-instruments/marbles/)**<br><sub>MIDI sequencer · Émilie Gillet · MIT</sub> | Random Sampler: a random sampler that plays up to three tracks. | ☐ |
| <a href="originals/stevequencer/"><img src="originals/stevequencer/screenshots/page_0.png" width="220" alt="Stevequencer"></a> | **[Stevequencer](originals/stevequencer/)**<br><sub>MIDI sequencer · Steve A · written for this repo</sub> | A 64-step melodic step sequencer on a 4x4 grid, edited from the Q-Links: pitch, length, on/off, velocity, chance and ratchets per step. | ☐ |
| <a href="originals/stevequencer16/"><img src="originals/stevequencer16/screenshots/page_0.png" width="220" alt="Stevequencer 16"></a> | **[Stevequencer 16](originals/stevequencer16/)**<br><sub>MIDI sequencer · Steve A · written for this repo</sub> | 16 steps and eight modulation lanes to any parameter of the instrument, Elektron-style: hold, return, slide across steps, LFO, follow another lane; per-lane rate and length. | ☐ |
| <a href="schwung/sequencers/superarp/"><img src="schwung/sequencers/superarp/screenshots/page_0.png" width="220" alt="Super Arp"></a> | **[Super Arp](schwung/sequencers/superarp/)**<br><sub>Arpeggiator · handcraftedcc · MIT</sub> | A pattern and rhythm arpeggiator with 40 patterns, 40 rhythms and random modifiers. | ☐ |

### Audio effects (4)

| | Plugin | What it is | Design QA |
|---|---|---|:---:|
| <a href="mutable-instruments/warps/"><img src="mutable-instruments/warps/screenshots/page_0.png" width="220" alt="Meta Mod"></a> | **[Meta Mod](mutable-instruments/warps/)**<br><sub>Audio effect · Émilie Gillet · MIT</sub> | Meta Mod: a meta-modulator (ring mod, fold, vocoder...) as an audio effect, with 12 presets, from Émilie Gillet's open-source Warps. | ✅ |
| <a href="originals/percolator/percolatorout/"><img src="originals/percolator/percolatorout/screenshots/page_0.png" width="220" alt="Percolator OUT"></a> | **[Percolator OUT](originals/percolator/percolatorout/)**<br><sub>Audio effect · Steve A · written for this repo</sub> | Plays one of Percolator's OUT 1-4 on its own track (an insert; an audio track will do), so a voice gets that track's fader, inserts and outputs. | ☐ |
| <a href="mutable-instruments/ringsfx/"><img src="mutable-instruments/ringsfx/screenshots/page_0.png" width="220" alt="Resonator FX"></a> | **[Resonator FX](mutable-instruments/ringsfx/)**<br><sub>Audio effect · Émilie Gillet · MIT</sub> | Resonator FX: Resonator as an audio effect, the track's sound excites the resonator, with 12 presets. | ✅ |
| <a href="schwung/effects/verglas/"><img src="schwung/effects/verglas/screenshots/page_0.png" width="220" alt="Verglas"></a> | **[Verglas](schwung/effects/verglas/)**<br><sub>Audio effect · Émilie Gillet · MIT</sub> | Verglas: a granular texture processor as an audio effect, with 12 presets, from Émilie Gillet's open-source Clouds. | ✅ |

## Installing

You need a **Gen1 MPC OS device** (32-bit ARM: e.g. MPC Live, Live II, One, X, Force) with **root SSH access**, and a Mac or Linux computer (Windows: WSL) on the same network. Then:

```
git clone https://github.com/saustin2010/vst_instruments.git
cd vst_instruments
./install.sh <mpc-address> --dry-run all      # check first: changes nothing
./install.sh <mpc-address> hera grids verglas  # or a group: instruments, sequencers, effects, all
```

The first install of a plugin restarts MPC once (it asks first; `MPC.settings` is backed up). **[INSTALL.md](INSTALL.md)** walks through every step: getting SSH access, what the installer changes, adding a plugin to a track, routing the sequencers, updating, uninstalling, installing by hand and troubleshooting. Presets, kits and wavetables are fetched from their original projects into `presets/` and installed with each plugin; [docs/presets-and-libraries.md](docs/presets-and-libraries.md) lists them, where they come from and how to add your own.

**One plugin at a time, as a release zip:** 34 of the plugins are in
[sd88me's plugin catalogue](https://sd88me.github.io/mpc-vst-plugins/), each from its own repo with its own releases
(`saustin2010/mpc-vst-<plugin>`, for example [mpc-vst-303](https://github.com/saustin2010/mpc-vst-303); Percolator OUT ships in Percolator's). The other 9 stay here: 6 have a version in the catalogue already, 2 have no licence, and Incipience is in testing (not offered to the catalogue yet).
This repo stays where they're developed; the plan and where each plugin stands: [docs/catalogue-migration.md](docs/catalogue-migration.md).

## Status (2026-10-05)

- Full-height screens (2026-10-18): the Stitch screens fill the whole plugin area. The conversion used to centre each design at the height it had under its own header and footer, leaving an empty band above and below (about 110 px of 628 on most). 30 screens rebuilt, same controls and parameters; installed and checked on the Live II (2026-10-19). Noisemaker's screen also lost its "TAL" text.
- Renamed (2026-10-12): no other company's name in a plugin's name, maker or screen. Moog is now **RaffoSynth**; the Mutable Instruments ports are **Macro Osc** (Braids), **Macro Osc 2** (Plaits), **Modal Voice** (Elements), **Resonator** (Rings), **Resonator FX**, **Meta Mod** (Warps), **Drum Map** (Grids) and **Random Sampler** (Marbles), with Émilie Gillet, who wrote them, as their maker. Same sound, presets and parameters; [what changes](docs/catalogue-migration.md#other-companies-names-2026-10-12).
- All 40 build and pass the offline test (all rebuilt 2026-10-05): an x86 build under AddressSanitizer/UBSan that checks every parameter, presets, saving and restoring, Q-Link behaviour, notes to audio, and a stress test that hammers the plugin from two threads as MPC does.
- All 40 are installed on an MPC Live II (MPC OS 3.9.1); these builds since 2026-10-05 (the first set 2026-10-03).
- Design QA on the device: **27 of 40 passed** (2026-10-04): the first 22 synths, both drum machines and the three audio effects, every page checked on the Live II (names, values, touch controls, Q-Links, pop-ups, displays). Every screen has one Q-Link column per panel, with Moog as the model, and each plugin's `DESIGN-QA.md` says what changed. Still to do: the 11 sequencers and generators and the two newest synths (Mutable Vibe, MPC Plaits); their screens have had the same pass offline.
- Presets: every instrument and audio effect now has presets in MPC's own **PRESET menu** in the plugin header (also on the arrangement screen); checked on the Live II. Where upstream had none, the port brings its own, levels evened out (303, Elements, Rings, Plaits, Mr Hyde, Rings FX, Verglas, Warps; 2026-10-03/04); Tablor's and Mono Voice's factory sets are reachable now too. OB-Xd and Noisemaker get a BANK selector for banks you add, Hush One reads TAL-BassLine-101 presets you add, and the menu follows what's loaded ([docs/presets-and-libraries.md](docs/presets-and-libraries.md)).
- Every screen is checked offline before it ships: each control's binding, the Q-Link layout, the touch areas (`dev-tools/stitch/check_skin.py`) and the Q-Link column outlines MPC highlights (`dev-tools/stitch/qlink_overlay.py`).
- Sequencers on a stock MPC: on the Live II, MPC finds a sequencer's own MIDI port and connects to it by itself, no restart (2026-10-04); a second track plays from it (Monitor In on that track, Remote off for the port; confirmed 2026-10-04, as on a Force). [docs/sequencers.md](docs/sequencers.md) walks through the routing.
- Plugin browser: every name starts with its kind, **[SYN]**, **[DRUM]**, **[SEQ]** or **[FX]** (2026-10-04). MPC's plugin menu sorted by type puts every VST in one VST folder (it ignores the category a plugin reports, Akai's own folder names included: tested on the Live II), so the tags group them there; sorted by manufacturer it makes a folder per maker. All plugins keep their real makers.
- Audio effects: Verglas, Warps and Rings FX are in a track's insert effect slots, under VST, and work there (Live II, 2026-10-04; their screens passed the design QA in the slot).
- New: **[Stevequencer](originals/stevequencer/)**, a 64-step sequencer edited from the Q-Links, written for this repo (2026-10-04): built, offline test passed, installed and working on the Live II. Its browser prototype (`design/prototype.html`) plays in Chrome. Its two MOD lanes (a CC value per step that moves the instrument's controls) work on the Live II (2026-10-05).
- MIDI CC 20-35 move every plugin's first-page Q-Links (column 1 = CC 20-23, top to bottom, and so on), with no MIDI learn: a sequencer's per-step CCs or a controller drive the instrument through the track's MIDI input (2026-10-04; working on the Live II 2026-10-05). For any parameter on any page, NRPN: see Stevequencer 16 below.
- New: **[Mutable Vibe](mpc-ports/mutablevibe/)** (nachtaktiv303) and **[MPC Plaits](mpc-ports/mpcplaits/)** (poloq), two instruments their authors wrote for MPC OS on sd88me's framework, built here from their source with this repo's copy of it (2026-10-05): offline test passed, Q-Links one column per panel on every page (their screens are otherwise the authors'). Installed and working on the Live II (2026-10-05). Presets made for both, levels evened out offline (24 and 31; Mutable Vibe gained a VOLUME control for it).
- New: **[Stevequencer 16](originals/stevequencer16/)** (2026-10-05; offline tests passed, installed on the Live II, first device test next): 16 steps and eight modulation lanes that can move any parameter of the instrument, on any page (NRPN, which every plugin here now takes as its parameter number: [docs/parameter-numbers.md](docs/parameter-numbers.md)), with slides across steps, LFOs, per-lane rate and length, and lanes that follow each other (modulation groups). Stevequencer's MOD lanes are confirmed working on the Live II (2026-10-05): MPC passes a track's MIDI CCs through to the instrument.
- New: **[Percolator](originals/percolator/)** (2026-10-10), a four-voice drum synth written for this repo, laid out like the Erica Synths Pērkons HD-01 (each voice has its own note, MIDI channel and output; notes 36-39 to start with). It reads the Pērkons' kit packs as they unzip; Erica gives them away but they aren't shipped here (its README has the links). Built and offline test passed; its first version measured on the Live II (`bench.sh`: PASS, playing 1.9 %); its screen checked there (Design QA ✅). Released v1.2.0 from its own repo, in sd88me's catalogue since 2026-10-10; v2.0.0 released 2026-10-12 after the owner checked its release zip on the Live II. Its kit-pack steps rewritten 2026-10-13 (zips unzipped on the MPC, what goes wrong, a prompt for Claude Code), after users had trouble installing them; 2.0.1 (released 2026-10-13, checked on the Live II) also finds a pack one folder down, as Windows' "Extract All" leaves it. 2.1.0 (2026-10-19/20, offline tests passed; on the Live II, the owner: "this is working well"; not released yet): your own WAV files on any voice, from the MPC's drives (SOURCE: its algorithm or its sample; a SAMPLES page to browse them, as Incipience's), and a RANDOMISE button per voice; and every voice now stops once its hit has died away (up to 2.0.1 only the snare did, so silence after playing cost 1.7x a fresh instance's). 2026-10-20, offline so far: the SAMPLES page is Incipience 1.1.0's browser (a folder and its subfolders in one list, the data wheel on FILE, a second tap or AUTO LOAD to load), and SOURCE and SAMPLE tidied on the voice pages.
- New: **[Percolator OUT](originals/percolator/percolatorout/)** (2026-10-12), an insert effect that plays one of Percolator 2.0.0's OUT 1-4 on its own track, so a voice gets a track (an audio track will do) without a second instrument plugin. Built, offline tests passed (with Percolator as two separately loaded libraries); working on an audio track on the Live II (2026-10-12). Released 2026-10-12 as a second zip from Percolator's repo (tag `percolator-out-v2.0.0`), and in sd88me's catalogue as `percolator-out`.
- New: **[Incipience](originals/incipience/)** (2026-10-13), an eleven-instrument drum synth written for this repo, laid out like the Jomox AlphaBase Mk II (its controls and 0-127 ranges; no sequencer), with a screen designed in Google Stitch with the AlphaBase's look: a tab per instrument, each with the hardware's PAGE A and B and a MIX page as Q-Link sub-pages. It plays the AlphaBase's own sample set from Jomox's free download (not shipped). Built, offline test PASSED, `check_skin.py` OK, procwatch no flags; installed on the Live II; in testing (the owner's first look: "looks and sounds great"; the changes asked for are in).
- Developed on a Live II. Other Gen1 devices run the same MPC software and should behave the same; Gen2 devices (e.g. Live III) are reported to be more locked down. Reports welcome.

What's next is in [ROADMAP.md](ROADMAP.md). Found a problem? Open an issue with the plugin, your MPC model and firmware, and what you did.

## What's in this repo

```
schwung/instruments/   22 instruments ported from Ableton Move "Schwung" modules
schwung/sequencers/     6 MIDI sequencers from Schwung MIDI FX modules
schwung/effects/        1 audio effect from a Schwung audio FX module
mutable-instruments/    6 ported from Mutable Instruments' own firmware source
vcv-rack/               1 ported from a VCV Rack module
originals/              2 written for this repo (Stevequencer, Stevequencer 16)
mpc-ports/              2 written for MPC by other authors (Mutable Vibe, MPC Plaits), built here
  <plugin>/            README.md, screenshots/, deploy/ (ready to install), source, screen design
presets/               the plugins' presets, kits, wavetables (not in git: tools/fetch-presets.py)
install.sh, uninstall.sh, tools/   the installer (and tools/build.sh to build from source, tools/publish.sh to publish a plugin to its own repo)
framework/             our changes to sd88me's mpc-vst-plugins, as a patch, and setup.sh (setup-release.sh: the tools the plugins' own repos release with)
dev-tools/             the tools the ports and screens were made with
docs/                  extra guides (presets and libraries, parameter numbers, root SSH on a stock MPC, the catalogue migration plan) and images
licenses/              licence texts the plugins refer to
```

Each plugin's `deploy/` folder is the finished build, so installing needs only this repo; the presets come from their original projects (the installer fetches them). To build from source see [BUILDING.md](BUILDING.md); to change a screen, [RESKINNING.md](RESKINNING.md); for what the plugins do behind the scenes and what they cost while silent (and the fixes still to make), [TESTS.md](TESTS.md).

## Licences

Each plugin keeps its upstream licence (GPL-2.0, GPL-3.0, MIT or BSD-3-Clause; two state none; MPC Plaits' panel artwork is CC-BY-SA 3.0): see the plugin's README and [CREDITS.md](CREDITS.md). The GPL plugins' complete source is in their folders. The scripts and documents written for this repo don't have a licence of their own yet.

## Disclaimer

Not affiliated with or endorsed by Akai Professional / inMusic, or by the makers of the instruments these plugins emulate or are named after (Roland, Korg, Oberheim, Moog, Elektron, Wurlitzer, Teenage Engineering, Mutable Instruments, Befaco and others); their names are used only to say what a plugin is modelled on. VST is a trademark of Steinberg; these plugins use a hand-written VST2 interface, not Steinberg's SDK. No Akai content is included. Installing edits MPC's settings file: the installer backs it up first, and you use all of this at your own risk, on hardware you own.
