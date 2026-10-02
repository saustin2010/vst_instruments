# Developer tools

The tools these ports and screens were made with, as they were used. You don't need any of them to install or even
to rebuild a plugin (`tools/build.sh` does that); they're here so the work can be reproduced and extended.

They expect the working layout they were written in: a checkout of the framework with the plugins under
`steve/schwung-ports/<plugin>`. `dev-tools/workspace.sh` builds exactly that out of symlinks to this repo:

```
framework/setup.sh          # once: the framework + our patch
dev-tools/workspace.sh      # once: framework/mpc-vst-plugins/steve/ -> this repo
cd framework/mpc-vst-plugins
python3 steve/tools/stitch/convert.py hera          # redo Hera's screen from its Stitch design
steve/tools/skin-redesign/build.sh hera src/presets:presets   # build, test, preview, deploy folder
python3 steve/tools/stitch/check_skin.py hera       # bindings, Q-Links, touch boxes
```

| Folder | What it is |
|---|---|
| `stitch/` | The Google Stitch → MPC screen pipeline: `pull.py` (download designs), `survey.py` (what a port needs), `extract.py` (render a design in headless Chromium and measure its controls), `convert.py` (write `layout.conf`, with per-port settings in its `MAPS`), `waveforms.py` / `envelope.py` (real waveform pictures and envelope displays), `check_skin.py` (checks a built skin), `dump_state.sh` + `showcase.py` (the screenshots in each plugin's README). See its README. |
| `skin-redesign/` | The generator used before the Stitch designs: one spec per port (`specs/<port>.py`) writing `params.json` and the page plan (`layout.grid.conf`), plus `build.sh` / `build_all.sh` (build → offline test → preview → deploy folder). |
| `midifx/` | The adapter that runs a Schwung MIDI FX module (sequencer, arpeggiator) as a plugin with its own ALSA MIDI port, clocked from MPC's transport. Copied into each sequencer's `mpc/`. |
| `midiout/` | The plugin's own MIDI output port (header), used by Rampage. |
| `audiofx/` | The adapter for Schwung audio FX modules (Verglas). |
| `mi/` | Helpers for engines written around Mutable Instruments' DSP classes (parameters, saved state, resampling). |
| `rack/` | A small stand-in for the VCV Rack API and a module extractor, so a Rack module's DSP builds unchanged (Rampage). |
| `fuzz/` | Stress-tests a port offline under ASan/UBSan the way a user hammers it (two threads, state save/restore, re-inserts). |
| `probe/` | Builds a port's engine for Linux and pokes it (get/set/notes) to learn its data folders and units. |
| `screengrab/` | Screenshots and CPU readings from an MPC over SSH (read-only). |
| `fake-mpc/` | A pretend MPC in a container, to test `install.sh` / `uninstall.sh` without a device. |
| `package/` | The script that assembled this repo from the working folder. |
