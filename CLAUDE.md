# vst_instruments: agent guide

40 native VST2 plugins for Akai MPC OS standalone devices (Gen1, 32-bit ARM: Live, Live II, One, X, Force), loaded
by MPC's built-in JUCE plugin host, each with a native touchscreen skin converted from a Google Stitch design. The
owner develops on an MPC Live II (MPC OS 3.9.1, root SSH via a rebuilt Akai update).

**This repo is the source of truth now.** It was first assembled (2026-10-03) from a private working folder by
`dev-tools/package/package.py`; don't run that script against this repo (it rewrites everything except `.git`).

## Start here

1. `README.md` (catalogue, status), `INSTALL.md` (users), `BUILDING.md`, `RESKINNING.md`.
2. `ROADMAP.md`: the open items. Update it as things get done, with the date.
3. `docs/presets-and-libraries.md`: what every plugin loads from files, what ships, what's missing.
4. The framework's `docs/NOTES.md` (after `framework/setup.sh`: `framework/mpc-vst-plugins/docs/NOTES.md`): every
   fact verified on hardware. Read the relevant section before changing the wrapper, skins or MIDI/effects behaviour.

## Layout

- `schwung/{instruments,sequencers,effects}/<plugin>/`, `mutable-instruments/<plugin>/`, `vcv-rack/<plugin>/`,
  `originals/<plugin>/` (written for this repo), `mpc-ports/<plugin>/` (written for MPC by other authors, built here from
  their source): one self-contained folder per plugin. `vst.json` (build),
  `params.json` (what MPC sees), `layout.conf` (the screen),
  `images/`, `<plugin>.css`, `src/` (upstream source; local patches under `MPC_PORT`, summarised in
  `upstream-changes.diff`), `mpc/` (adapters/glue), `design/stitch.html`, `screenshots/`, `README.md`, and `deploy/`:
  the finished build, laid out like the MPC (`vst/` → `/sdcard/vst`, `Synths/` → `/sdcard/Synths`,
  `pluginlist-entry.xml`), without its presets (those are in `presets/`).
- `presets/<plugin>/`: the plugins' libraries (presets, kits, wavetables, patches) laid out as `/sdcard/vst/<plugin>/`.
  **Not in git** (only `presets/README.md` is): `tools/fetch-presets.py` downloads them from their projects at pinned
  commits (git-hash checked); install.sh and tools/build.sh fetch what's missing. Never commit them.
- `install.sh`, `uninstall.sh`, `tools/` (`mpc-side.sh` runs on the device; `plugin_list.awk` edits MPC.settings;
  `common.sh`; `build.sh`).
- `framework/`: our changes to sd88me's mpc-vst-plugins as `mpc-vst-plugins.patch` against `39660f2`, and `setup.sh`
  (clones it into the git-ignored `framework/mpc-vst-plugins/` and applies the patch).
- `dev-tools/`: the Stitch pipeline (`stitch/`), the older spec generator (`skin-redesign/`), adapters' sources
  (`midifx/`, `audiofx/`, `mi/`, `rack/`, `midiout/`), `fuzz/`, `probe/`, `screengrab/`, `fake-mpc/`, `package/`.
  They run inside a workspace: `dev-tools/workspace.sh` links this repo into `framework/mpc-vst-plugins/steve/`.

## Workflow for a change to a plugin

1. `framework/setup.sh` once. Building needs Docker with 32-bit ARM emulation: `tools/docker-ready.sh` (run by
   `tools/build.sh` and `dev-tools/fake-mpc/start.sh`) starts Colima if Docker is down and registers the emulation
   (lost on every Colima restart). Installing needs only SSH, no Docker.
2. Edit, then `tools/build.sh <plugin>`: builds the ARM `.so` and skin, refreshes `deploy/` (with its data folder),
   runs the offline x86 test under ASan/UBSan. It must print PASSED.
3. Screen changes: `dev-tools/workspace.sh` once, then from `framework/mpc-vst-plugins`:
   `python3 steve/tools/stitch/check_skin.py <plugin>` (bindings, Q-Links, touch boxes, options: must say OK), and
   refresh the README screenshots: `steve/tools/stitch/dump_state.sh <plugin>` (the engine's real state), then from the
   repo root `docker run --rm -v "$PWD:$PWD" -w "$PWD/framework/mpc-vst-plugins" mpc-vst-html-art python3
   steve/tools/stitch/showcase.py <plugin> "$PWD/<group>/<plugin>/screenshots"`. Docker must mount the repo root
   (the workspace links point into it); the dev tools do that themselves when they see they're in a workspace.
4. Update the plugin's README (and ROADMAP / presets doc if relevant). One commit per concern.
5. Installer changes: test with `dev-tools/fake-mpc/` before any device.

## Rules

- **Parameters are append-only.** MPC saves projects and Q-Link assignments by VST parameter index: never reorder or
  remove `params.json` entries of a plugin people use; add new ones at the end.
- **The device is the owner's live setup.** Ask before anything that restarts MPC (registering new plugins does), back
  up `MPC.settings` first (the installer does), stage files as `x.new` then `mv`. Never edit `MPC.settings` while
  MPC runs. A plugin crash takes MPC down.
- Never commit: anything in `presets/` but its README (add a library by adding its source to
  `tools/fetch-presets.py`, or for a plugin with its own repo to its `release/library.json`), Akai's stock skins or
  artwork, the Steinberg VST SDK (the VST2 ABI here is hand-written), ROMs or other copyrighted sample content,
  `framework/mpc-vst-plugins/`, `framework/release-tools/`, build output, SSH keys, IPs or device serials.
- **Plugins with their own repo** (`docs/catalogue-migration.md`): this repo stays the master. `tools/publish.sh <plugin>`
  pushes the committed folder to `saustin2010/mpc-vst-<plugin>`; never commit there directly (a PR merged there comes
  back with `git subtree pull`, see the script). Its releases build with `framework/setup-release.sh`'s tools;
  `dev-tools/catalogue/check.sh <plugin>` must say PASSED and "screen: same" before a release.
- No maker badges in screen art (no Akai/Roland/Oberheim logos implying origin); naming what a plugin emulates is fine.
- sd88me's framework files stay in his repo: change them through `framework/mpc-vst-plugins.patch`
  (`git diff` in the framework clone, plus new files), never by copying them here. The release tools are his current
  `main` plus commits on `steve-features` of `saustin2010/mpc-vst-plugins` (each offered to him as a PR); a change
  there means a new pin in `framework/setup-release.sh` and the plugins' `release.yml`.
- Device facts that matter: plugins go in `/sdcard/vst` (the SD card and SSD are `noexec`), skins in `/sdcard/Synths`,
  the plugin list is `pluginList-arm` in `/media/az01-internal/Settings/MPC/MPC.settings`, read only at MPC start-up.
  MPC shows a control's **parameter name**, not the layout label. MPC ignores plugin MIDI out: sequencers open their own
  ALSA MIDI port.
- Commits and pushes go to `github.com/saustin2010/vst_instruments` (the owner's), its plugins' own repos
  (`tools/publish.sh`) and `saustin2010/mpc-vst-plugins` (`steve-features`). Push only when asked.
