# Groove Bank

**MIDI sequencer** · Retriggers the chord you hold in a rhythm from a library of genre grooves. · maker in MPC: Mission Minnow · licence: MIT

<img src="screenshots/page_0.png" width="760" alt="Groove Bank on the MPC touchscreen">

Hold a chord on Groove Bank's track and it replays it in a rhythm template from its library of genre-first grooves (each with variants), with swing, gate length, strum and accent; LATCH keeps it going after you let go. It follows MPC's tempo. The groove library ships with it. The notes go out of its own MIDI port to the tracks you point at it; it makes no sound itself.

## On the MPC

- In the plugin browser: **[SEQ] Groove Bank** by **Mission Minnow** (Sequencer)
- Files: `/sdcard/vst/groovebank.so`, presets/data in `/sdcard/vst/groovebank/` (from this repo's `presets/groovebank/`, which `tools/fetch-presets.py` fills; install.sh does that for you), screen in `/sdcard/Synths/Mission Minnow - VST - [SEQ] Groove Bank/`
- 10 parameters (all automatable) on 1 page

## Playing it

MPC OS ignores a plugin's own MIDI output, so Groove Bank opens its own MIDI port (named **Groove Bank**, port **MIDI Out**), the way a USB MIDI device would appear. MPC picks the port up without a restart:

1. Put Groove Bank on a plugin track and play or hold notes into it (pads, keys or a MIDI clip).
2. **Menu → Preferences → MIDI**: switch **Track** on for the Groove Bank port.
3. On the track(s) that should play, set **MIDI input** to that port (not *All*, and not on Groove Bank's own track, or it hears itself).
4. Press play: it follows MPC's tempo and transport (MIDI clock from the plugin host).

Groove Bank makes no sound of its own.

## Pages

Screenshots are rendered from the built skin with the engine's real values right after it's inserted. On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a 4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.

### 1. MAIN

<img src="screenshots/page_0.png" width="760" alt="Groove Bank, page MAIN">

Q-Link columns: **1** VARIANT, SWING, GATE  ·  **2** STRUM, ACCENT, LATCH  ·  **3** GROOVE

## Install

From the top of this repo (see [INSTALL.md](../../../INSTALL.md)):

```
./install.sh <mpc-address> groovebank
```

## Where it comes from

- Upstream: https://github.com/mission-minnow/groovebank
- Vendored at commit 4edfa2f0a65f953f685e60b5753e725b0e5df7ce 2026-07-24
- Schwung module "Groove Bank" v0.1.16 by Mission Minnow
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
| `groovebank.css` | the artwork stylesheet |
| `images/` | artwork: backgrounds, knobs, displays |
| `design/` | the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it) |
| `src/` | the engine's source, vendored from upstream |
| `mpc/` | MPC-side glue: the engine wrapper or MIDI/audio adapter, and headers |
| `UPSTREAM` | where the source came from, and at which commit |

To rebuild from source see [BUILDING.md](../../../BUILDING.md); to change the screen, [RESKINNING.md](../../../RESKINNING.md).
