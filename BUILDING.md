# Building from source

Every plugin folder already contains its finished build (`deploy/`); you only need this to change a plugin or check
the build. The plugins are 32-bit ARM Linux libraries, cross-built in Docker, then tested on your computer under
AddressSanitizer before they go anywhere near an MPC.

## What you need

- macOS or Linux, `git`, Python 3, bash 4 or later (macOS: `brew install bash`).
- **Docker** with 32-bit ARM emulation. On macOS, [Colima](https://github.com/abiosoft/colima) works:
  ```
  brew install colima docker && colima start
  docker run --privileged --rm tonistiigi/binfmt --install arm     # again after every Colima restart
  ```
  You don't have to remember either step: `tools/build.sh` (via `tools/docker-ready.sh`) starts Colima if Docker isn't
  running and registers the ARM emulation when it's missing. Check by hand with `bash tools/docker-ready.sh`.
  The build pulls `arm32v7/gcc:12`, `gcc:12` and builds an image with headless Chromium for the artwork
  (`mpc-vst-html-art`) the first time. Installing plugins needs none of this, only SSH.

## 1. The framework

The wrapper, the Schwung adapter, the skin generator and the test are sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins). This repo
carries only our changes to it, as a patch against commit `39660f2` (see [framework/README.md](framework/README.md)):

```
framework/setup.sh      # clones it into framework/mpc-vst-plugins/ and applies framework/mpc-vst-plugins.patch
```

(Already have a checkout with the patch applied? Point to it with `export MPC_VST_FRAMEWORK=<path>`.)

## 2. Build a plugin

```
tools/build.sh hera              # or several: tools/build.sh hera grids rampage
```

For each plugin it:

1. builds the `.so` for the MPC's ARM CPU and its screen (`TUI.json`, `Q-Links.json`, artwork) from `vst.json`,
   `params.json`, `layout.conf` and `images/` (log: `build/build.log`),
2. refreshes `deploy/` with the new `.so`, screen and plugin-list entry (the plugin's presets stay in
   `presets/<plugin>/`; fetched first if missing, so the test runs with them),
3. runs the **offline test** (log: `build/test.log`): an x86 build of the same plugin under AddressSanitizer/UBSan
   that loads it like MPC does and checks two instances, every parameter's name and display, set/get round trips,
   options and pop-ups, notes to audio (or audio through, for effects), saving and restoring, slow Q-Link turns,
   and VST programs. It prints `PASSED` or `TEST FAILED`.

Then install the result as usual (`./install.sh <mpc> hera`). `build/` is ignored by git.

## Where things are

| In a plugin folder | What |
|---|---|
| `vst.json` | name, maker, plugin ID, the `.so` name, source files and compiler flags, preset folder (`MODULE_DIR`), category |
| `params.json` | the parameters MPC sees, in VST index order: ranges, options, names, steppers |
| `layout.conf` | the screen: pages, every control's position and kind, artwork, Q-Link order |
| `src/` | the engine as published upstream (plus our patches: `upstream-changes.diff`) |
| `mpc/` | glue for engines that aren't plain Schwung sound modules: MIDI FX / audio FX adapters, engine wrappers for Mutable's and Rack's code, headers |

Never change the order of `params.json` entries in a plugin people already use: MPC saves projects and Q-Link
assignments by parameter index. Add new parameters at the end.

## More checks

`dev-tools/fuzz/` stress-tests a plugin the way a user hammers it (two threads, state save/restore, re-inserts) and
`dev-tools/probe/` pokes an engine to learn its data folders and units; see [dev-tools/README.md](dev-tools/README.md).
The framework's `tools/bench.sh` measures CPU on the device.
