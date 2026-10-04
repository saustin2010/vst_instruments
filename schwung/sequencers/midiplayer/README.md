# MIDI Player

**MIDI sequencer** · Plays Standard MIDI Files in time with MPC's transport. · maker in MPC: Charles Vestal · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="MIDI Player on the MPC touchscreen">

Drop .mid files into /sdcard/vst/midiplayer/MIDI on the MPC and MIDI Player plays them in sync with MPC's tempo and transport, out of its own MIDI port to the tracks you point at it. Step through files with the arrows (the file's name shows on screen), pick one track of the file or ALL (every track, sent on channel 1), and switch LOOP on or off. A short demo file ships with it.

## On the MPC

- In the plugin browser: **[SEQ] MIDI Player** by **Charles Vestal** (Sequencer)
- Files: `/sdcard/vst/midiplayer.so`, presets/data in `/sdcard/vst/midiplayer/` (from this repo's `presets/midiplayer/`, which `tools/fetch-presets.py` fills; install.sh does that for you), screen in `/sdcard/Synths/Charles Vestal - VST - [SEQ] MIDI Player/`
- 6 parameters (all automatable) on 1 page

## Playing it

MPC OS ignores a plugin's own MIDI output, so MIDI Player opens its own MIDI port (named **MIDI Player**, port **MIDI Out**), the way a USB MIDI device would appear. MPC picks the port up without a restart:

1. Put MIDI Player on a plugin track. It needs no notes: it plays when MPC's transport runs.
2. **Menu → Preferences → MIDI**: switch **Track** on for the MIDI Player port.
3. On the track(s) that should play, set **MIDI input** to that port (not *All*, and not on MIDI Player's own track, or it hears itself).
4. Press play: it follows MPC's tempo and transport (MIDI clock from the plugin host).

MIDI Player makes no sound of its own.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. PLAYER

<img src="screenshots/page_0.png" width="760" alt="MIDI Player, page PLAYER">

Q-Link columns: **1** TRACK, LOOP  ·  **2** FILE

## Install

From the top of this repo (see [INSTALL.md](../../../INSTALL.md)):

```
./install.sh <mpc-address> midiplayer
```

## Where it comes from

- Upstream: https://github.com/charlesvestal/schwung-midi-player
- Vendored at commit a655383f224c58d506d7a2b14b10977523afe71e 2026-09-12
- Schwung module "MIDI Player" v0.1.4 by Charles Vestal
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
| `midiplayer.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `mpc/` | MPC-side glue: the engine wrapper or MIDI/audio adapter, and headers |
| `UPSTREAM` | where the source came from, and at which commit |

To rebuild from source see [BUILDING.md](../../../BUILDING.md); to change the screen, [RESKINNING.md](../../../RESKINNING.md).
