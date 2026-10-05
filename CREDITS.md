# Credits

These plugins are other people's instruments, ported to MPC OS. Thank you to all of them.

## The framework

- **sd88me**: [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins), the VST2 wrapper, Schwung adapter, skin generator, Skin Studio and offline test every plugin here is built with. These ports exist because of it.
- **NoQuestion** and **dustyslices**: first described running custom plugins in MPC's standalone host (MPC-Forums, "Proof of Concept: Custom Standalone Plugins", September 2026).
- **Charles Vestal** and the Schwung / Move Everything community: the Schwung host for Ableton Move, and many of the modules ported here.
- Google Stitch: the screen designs were generated with it, then converted and finished by hand.

## The plugins

| Plugin | Original work | Source ported | Licence |
|---|---|---|---|
| [303](schwung/instruments/303/) | Robin Schmidt / midilab / davemollen / Charles Vestal | Schwung module "303" v0.3.3 | GPL-3.0 |
| [Aphex](schwung/instruments/aphex/) | Filliformes | https://github.com/filliformes/aphex-move | MIT |
| [Braids](schwung/instruments/braids/) | Emilie Gillet (port: charlesvestal) | Schwung module "Braids" v0.2.8 | MIT |
| [Chordism](schwung/instruments/chordism/) | Charles Vestal | https://github.com/charlesvestal/schwung-chordism | MIT |
| [Denis](schwung/instruments/denis/) | Vincent Fillion | https://github.com/filliformes/denis-move | MIT |
| [Elements](mutable-instruments/elements/) | Emilie Gillet | https://github.com/pichenettes/eurorack (elements/) | MIT |
| [Eucalypso](schwung/sequencers/eucalypso/) | Handcrafted Media | https://github.com/handcraftedcc/move-everything-eucalypso | MIT |
| [Fizzik](schwung/instruments/fizzik/) | Filliformes | https://github.com/filliformes/fizzik-move | MIT |
| [Grids](mutable-instruments/grids/) | Mutable Instruments | https://github.com/pichenettes/eurorack (grids/) | GPL-3.0 |
| [Groove Bank](schwung/sequencers/groovebank/) | mission-minnow | https://github.com/mission-minnow/groovebank | MIT |
| [Hank](schwung/instruments/hank/) | charlesvestal | Schwung module "Hank" v0.6.0 | MIT |
| [Helm](schwung/instruments/helm/) | Matt Tytel | https://github.com/andree182/schwung-helm | GPL-3.0 |
| [Hera](schwung/instruments/hera/) | jpcima (port: charlesvestal) | Schwung module "Hera" v0.1.7 | GPL-3.0 |
| [Hush One](schwung/instruments/hush1/) | Move Everything Community | Schwung module "HUSH ONE" v0.2.8 | not stated upstream |
| [Libpo32](schwung/instruments/libpo32/) | mestela | Schwung module "Libpo32" v1.1.0 | not stated upstream |
| [Marbles](mutable-instruments/marbles/) | Emilie Gillet | https://github.com/pichenettes/eurorack (marbles/) | MIT |
| [Maze Lite](schwung/sequencers/mazelite/) | sd88me | https://github.com/sd88me/schwung-maze | MIT |
| [MIDI Player](schwung/sequencers/midiplayer/) | Charles Vestal | https://github.com/charlesvestal/schwung-midi-player | MIT |
| [MonkSynth](schwung/instruments/monksynth/) | Jonathan Taylor | https://github.com/charlesvestal/schwung-monksynth | MIT |
| [Mono Voice](schwung/instruments/monovoice/) | Tim Cox | https://github.com/timncox/schwung-mono | MIT |
| [Moog](schwung/instruments/moog/) | Nicolas Roulet, Julian Palladino (port: charlesvestal) | Schwung module "RaffoSynth" v0.2.5 | MIT |
| [MPC Plaits](mpc-ports/mpcplaits/) | poloq (bridge from schwung-mrhyde, move-anything contributors; DSP: Emilie Gillet) | https://github.com/poloq-instruments/mpc-vst-plaits | MIT (panel artwork CC-BY-SA 3.0) |
| [Mr Hyde](schwung/instruments/mrhyde/) | move-anything contributors | Schwung module "MrHyde" v0.0.1 | MIT |
| [Mr Drums](schwung/instruments/mrdrums/) | move-anything contributors | Schwung module "MrDrums" v0.0.4 | MIT |
| [Mutable Vibe](mpc-ports/mutablevibe/) | nachtaktiv303 (DSP: Emilie Gillet) | https://github.com/nachtaktiv303/mpc-vst-mutable-vibe | MIT |
| [Noisemaker](schwung/instruments/noisemaker/) | legsmechanical (engine: Patrick Kunz / TAL) | Schwung module "Noisemaker" v0.2.2 | GPL-2.0 |
| [NuSaw](schwung/instruments/nusaw/) | Charles Vestal | https://github.com/charlesvestal/schwung-nusaw | MIT |
| [OB-Xd](schwung/instruments/obxd/) | reales (port: charlesvestal) | Schwung module "OB-Xd" v0.4.9 | GPL-3.0 |
| [Pixel Walkers](schwung/sequencers/pixelwalkers/) | Matt Estela | https://github.com/mestela/schwung-pixel-walkers | MIT |
| [Plaits](schwung/instruments/plaits/) | Emilie Gillet (original Plaits DSP) | https://github.com/j3threejay/move-anything-plaits | MIT |
| [Rampage](vcv-rack/rampage/) | Befaco | https://github.com/VCVRack/Befaco (src/Rampage.cpp, src/plugin.hpp) | GPL-3.0 |
| [Rings](mutable-instruments/rings/) | Emilie Gillet | https://github.com/pichenettes/eurorack (rings/) | MIT |
| [Rings FX](mutable-instruments/ringsfx/) | Emilie Gillet | https://github.com/pichenettes/eurorack (rings/) | MIT |
| [Super Arp](schwung/sequencers/superarp/) | Handcrafted Media | https://github.com/handcraftedcc/move-everything-superarp | MIT |
| [Tablor](schwung/instruments/tablor/) | athousanddetails | https://github.com/athousanddetails/schwung-tablor | BSD-3-Clause |
| [Verglas](schwung/effects/verglas/) | Emilie Gillet (emilie | https://github.com/filliformes/verglas-move | MIT |
| [Warps](mutable-instruments/warps/) | Emilie Gillet | https://github.com/pichenettes/eurorack (warps/) | MIT |
| [Wurl](schwung/instruments/wurl/) | Filliformes | https://github.com/filliformes/wurl-move | GPL-3.0 |

Each plugin's README has the details: the exact upstream commit or module version, its licence files, and every change made for the MPC (also as `upstream-changes.diff` where upstream files were patched). Helm's factory patches are CC BY 4.0 (Matt Tytel and contributors). Fonts in the screens: Titillium Web (SIL OFL), as MPC uses.
