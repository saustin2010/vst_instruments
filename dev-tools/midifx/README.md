# midifx: Schwung MIDI FX modules as MPC plugins that play other tracks

MPC OS ignores a VST's MIDI output, so a sequencer plugin can't hand its notes to MPC the normal way. This adapter
runs a Schwung `midi_fx_api_v1` module (arpeggiator, Euclidean or generative sequencer, MIDI file player) inside a
silent instrument plugin and sends everything the module plays out of an **ALSA MIDI port of its own** (client = the
plugin's name, port "MIDI Out"; a second instance is "<name> 2"). MPC picks new ports up by itself (checked on the
Live II 2026-10-01 with `../probe/midiport/seqprobe.c`), so another track can take it as its MIDI input.

| File | What |
|---|---|
| `schwung_midi_fx.c` | the adapter (`mpc_engine_t` for the repo's wrapper). Clock: the wrapper's `mpc_engine_transport` hook (tempo, position, play/stop from the host each block) becomes 24-PPQN MIDI clock + Start/Stop for the module, and answers its `get_bpm`/`get_clock_status`/`get_beat_position`. Notes played on the plugin's own track go to the module. libasound is `dlopen`ed on the device. |
| `host/plugin_api_v1.h`, `host/midi_fx_api_v1.h` | Schwung's host headers (charlesvestal/schwung). Local change: the 64-bit-only `offsetof(reserved) == 120` assert is skipped on 32-bit ARM. |
| `sync.sh [port ...]` | copies the adapter, headers and `wrapper/engine.h` into each sequencer port's `mpc/` |
| `make_demo_mid.py` | writes MIDI Player's demo file (`schwung-ports/midiplayer/data/MIDI/`, original material) |

Per-port settings live in the port's `vst.json` `"defines"`:
- `MIDIFX_INIT` = `"key=val;key=val"`, set right after the module starts (Eucalypso and Super Arp: `"sync=clock"`
  so they follow MPC's transport instead of their own tempo);
- `MODULE_DIR` for modules that read files (Groove Bank's `patterns/`, MIDI Player's `MIDI/`);
- `MIDIFX_DEBUG` prints every MIDI message sent (offline probing).

Ports using it: eucalypso, superarp, groovebank, pixelwalkers, mazelite, midiplayer (`steve/schwung-ports/`). Each
README there has the routing steps for the MPC. Offline check without the MPC: `../probe/probe.sh <port dir>
<module dir> note:60 play:96 stop` with `MIDIFX_DEBUG` set (see `../probe/`).
