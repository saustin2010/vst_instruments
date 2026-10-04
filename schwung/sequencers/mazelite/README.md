# Maze Lite

**MIDI sequencer** · A dual generative sequencer in the style of the Moog Labyrinth. · maker in MPC: sd88me · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Maze Lite on the MPC touchscreen">

Two clock-synced 8-step generative sequencers in the style of the Moog Labyrinth: each loops a short pattern of bits that turns into notes in the chosen scale around a root, and CORRUPT mutates it (past noon it starts flipping bits), from locked repetition to constant change. Per sequencer: range (pitch spread around the root), length (1-8 steps), BIT FLIP and ADVANCE buttons and a reset every 1-8 bars; TRIG MIX, note rate and note length shape the output. It runs with MPC's transport, a note you play on its track sets the key, and it plays out of its own MIDI port for other tracks. The engine is sd88me's own Schwung module (the sequencer half of his Maze Voice).

## On the MPC

- In the plugin browser: **[SEQ] Maze Lite** by **sd88me** (Sequencer)
- Files: `/sdcard/vst/mazelite.so`, screen in `/sdcard/Synths/sd88me - VST - [SEQ] Maze Lite/`
- 23 parameters (all automatable) on 1 page

## Playing it

MPC OS ignores a plugin's own MIDI output, so Maze Lite opens its own MIDI port (named **Maze Lite**, port **MIDI Out**), the way a USB MIDI device would appear. MPC picks the port up without a restart:

1. Put Maze Lite on a plugin track. It needs no notes: it plays when MPC's transport runs.
2. **Menu → Preferences → MIDI**: switch **Track** on for the Maze Lite port.
3. On the track(s) that should play, set **MIDI input** to that port (not *All*, and not on Maze Lite's own track, or it hears itself).
4. Press play: it follows MPC's tempo and transport (MIDI clock from the plugin host).

A note you play on its track sets the key (the root its patterns spread around).

Maze Lite makes no sound of its own.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAZE

<img src="screenshots/page_0.png" width="760" alt="Maze Lite, page MAZE">

Q-Link columns: **1** SCALE, NOTE RATE, NOTE LENGTH, RESET BOTH  ·  **2** S1 CORRUPT, S1 RANGE, S1 LENGTH, S1 TRIG MIX  ·  **3** S2 CORRUPT, S2 RANGE, S2 LENGTH, S2 TRIG MIX  ·  **4** S1 RESET, S2 RESET

## Install

From the top of this repo (see [INSTALL.md](../../../INSTALL.md)):

```
./install.sh <mpc-address> mazelite
```

## Where it comes from

- Upstream: https://github.com/sd88me/schwung-maze
- Vendored at commit e774a674eef281248282ab47f69a9456bf6631fc 2026-09-20
- Schwung module "Maze Lite" v1.3.4 by sd88me
- Licence: MIT ([`LICENSE`](LICENSE))
- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (wrapper, Schwung adapter, skin tools).

## Changes for the MPC

- `mpc/` = the MIDI FX adapter (`dev-tools/midifx/schwung_midi_fx.c`), Schwung's host headers and `wrapper/engine.h`. The adapter opens an ALSA MIDI port named after the plugin, sends the module's notes there, and turns MPC's transport (tempo, position, play/stop) into the 24-PPQN MIDI clock and Start/Stop the module expects.
- `mpc/host/plugin_api_v1.h`: its `offsetof(reserved) == 120` assert is a 64-bit (Move) layout check, so it is skipped on the MPC's 32-bit ARM (the module and adapter share the one header).
- New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, its own knob art, live names and values, and controls the design left out added in its style (see RESKINNING.md).

## Files

| Path | What |
|---|---|
| `screenshots/` | the pages as MPC draws them |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry (its presets/kits are in the repo's `presets/` folder, not here) |
| `vst.json` | build settings: name, maker, sources, compiler flags |
| `params.json` | the plugin's parameters as MPC sees them (VST index = order) |
| `params.base.json` | the engine's own parameter list it was derived from |
| `layout.conf` | the screen: control positions, art, Q-Links (generated from the Stitch design) |
| `layout.grid.conf` | the plan of pages and controls the Stitch conversion fills in |
| `mazelite.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `mpc/` | MPC-side glue: the engine wrapper or MIDI/audio adapter, and headers |
| `UPSTREAM` | where the source came from, and at which commit |

To rebuild from source see [BUILDING.md](../../../BUILDING.md); to change the screen, [RESKINNING.md](../../../RESKINNING.md).
