# The build framework

These plugins are built with **[sd88me/mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)**: a hand-written
VST2 wrapper for MPC OS, the adapter that runs Ableton Move "Schwung" modules unchanged, the skin generator that makes
MPC's native touchscreen pages, and the offline test. That code is sd88me's and lives in his repo. **Our changes to it
are commits on the branch `steve-features` of the fork [saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features)**
(below); the plugins build and release with it at one pinned commit, `REF` in `setup-release.sh`.

```
framework/setup.sh          # clones it into framework/mpc-vst-plugins/ at the pin (tools/build.sh uses it)
framework/setup-release.sh  # the same commit into framework/release-tools/ (dev-tools/catalogue/check.sh uses it)
```

You only need this to build from source ([BUILDING.md](../BUILDING.md)). Installing the ready-made plugins doesn't.

## Our changes: `saustin2010/mpc-vst-plugins`, branch `steve-features`

The plugins that have their own repos (docs/catalogue-migration.md) are released with sd88me's **current** tools plus 17
changes, kept as commits on the branch `steve-features` of the fork
[saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features) (made 2026-10-08, on his
main at #229). `framework/setup-release.sh` fetches them at the pinned commit; `dev-tools/catalogue/check.sh` builds with
them. Each plugin's `FRAMEWORK.md` lists the ones it uses, what they do there and what to change once sd88me has them
(written by `dev-tools/catalogue/prepare.py`). Each commit is meant to be offered to him as a pull request.

| Commit | Change | Why | Used by |
|---|---|---|---|
| [`b1c39bc`](https://github.com/saustin2010/mpc-vst-plugins/commit/b1c39bc) | A hand-made `params.json` beside a Schwung `module` in `vst.json` | Without it sd88me's tools take the params file alone and don't link the Schwung adapter: the build fails. | 18 of the Schwung instruments (those with their own `params.json`) |
| [`50f459d`](https://github.com/saustin2010/mpc-vst-plugins/commit/50f459d) | The host test runs in Docker on macOS; `TEST_DOCKER_ARGS` | Apple's AddressSanitizer hangs on macOS 26 before the test starts. | building on a Mac |
| [`81f473b`](https://github.com/saustin2010/mpc-vst-plugins/commit/81f473b) | `focus_ring=1`: the touched control highlighted | His default draws no focus ring since 26 September; our screens were checked with it. | all 30 |
| [`01da4b3`](https://github.com/saustin2010/mpc-vst-plugins/commit/01da4b3) | `qlink_box=slot`: Q-Link column outlines by whole slots | His outlines are tighter (knob rings) and count buttons; ours were checked on the Live II. | all 30 |
| [`81a2edd`](https://github.com/saustin2010/mpc-vst-plugins/commit/81a2edd) | `when=<param>:<i>/<N>`: a picture per band of a continuous parameter | Envelope displays drawn per sustain level; without it the build stops. | 8: Aphex, Braids, Denis, Hera, MonkSynth, Moog, Noisemaker, Hank |
| [`6c87b6d`](https://github.com/saustin2010/mpc-vst-plugins/commit/6c87b6d) | `tools_repo` input of the release workflow | Lets a plugin repo build with the fork's tools. | every plugin repo |
| [`8a6ed80`](https://github.com/saustin2010/mpc-vst-plugins/commit/8a6ed80) | Option `values` / `send`; labels matched before numbers; no read past an option list | Engines that parse their own words or values; the overflow crashed Aphex under ASan in his wrapper. | values/send: Eucalypso, MIDI Player, Moog, Super Arp; the fixes: all |
| [`2da5066`](https://github.com/saustin2010/mpc-vst-plugins/commit/2da5066) | `mpc_engine_transport()`: tempo, song position and play state each buffer | Without it the sequencers pass offline but never play in time on the MPC. | 6: Eucalypso, Grids, Groove Bank, MIDI Player, Pixel Walkers, Super Arp |
| [`6303f4e`](https://github.com/saustin2010/mpc-vst-plugins/commit/6303f4e) | Host test: a wheel click on a long whole-number range moves 1/100 of it | The test expected one step per click on a 0-5000 ms range. | the host test |
| [`ea27eda`](https://github.com/saustin2010/mpc-vst-plugins/commit/ea27eda) | `"programs"` `count`, `name_at`, `name` | MPC's PRESET menu without empty slots, and with names. | 12: Braids, Chordism, Hera, MonkSynth, Mono Voice, Moog, Mr Drums, Noisemaker, NuSaw, OB-Xd, Hank, Tablor |
| [`55e60a0`](https://github.com/saustin2010/mpc-vst-plugins/commit/55e60a0) | `"clamped": true` params | Values the engine holds to what it has loaded; the host test skips them. | Groove Bank, MIDI Player |
| [`9595071`](https://github.com/saustin2010/mpc-vst-plugins/commit/9595071) | The release `install.sh` matches the plugin's path as text (`grep -F`) | Our `[SYN]`-style names made it refuse to install (found on the Live II). | all 30 |
| [`10d8f5f`](https://github.com/saustin2010/mpc-vst-plugins/commit/10d8f5f) | `QLINK_TRAVEL` and `SET_IF_CHANGED`, opt-in | Q-Link ticks add up like a detented knob, as our wrapper does; his stepped an option per tick and switches flipped on the Live II. | all 30 |
| [`f210a56`](https://github.com/saustin2010/mpc-vst-plugins/commit/f210a56) | Host test with `QLINK_TRAVEL` | Placeholder params and restore tolerance. | the host test |
| [`94e85dd`](https://github.com/saustin2010/mpc-vst-plugins/commit/94e85dd) | Host test: a preset's option value is what the option sends | An option list like `-`, `0` ... `127` with values -1 ... 127 made the test expect the wrong option. | the host test (both Stevequencers) |
| [`7afa72e`](https://github.com/saustin2010/mpc-vst-plugins/commit/7afa72e) | `"live"` params in `vst.json`: display params the engine moves by itself, reported to MPC after every block | The step light follows playback; without it, it stays where it was when the page was drawn. | 2: Stevequencer, Stevequencer 16 |
| [`7f0653c`](https://github.com/saustin2010/mpc-vst-plugins/commit/7f0653c) | A host set of parameter 0 waits for the next block; a pair that ends where it started is dropped; the host test replays it | On insert and on every STOP, MPC's host (JUCE) sets parameter 0 to the far end and straight back: where that is a preset, every knob moved since went back to it (Live II trace, 2026-10-10). | all: the 7 with a preset or kit there (Fizzik, Helm, Hera, Hush One, NuSaw, Libpo32, Percolator) lost edits on STOP. Offered to sd88me as [#279](https://github.com/sd88me/mpc-vst-plugins/pull/279) (2026-10-11), merged |
| [`eaef502`](https://github.com/saustin2010/mpc-vst-plugins/commit/eaef502) | `mpc_engine_changed()`: an engine reports parameters it changed itself, the wrapper tells MPC as it does a MIDI CC's | MPC's screen and Q-Links follow controls an engine sets from MIDI it reads itself; without it they keep the old values. | 1: Percolator (2.0.0: the Pērkons CCs and MIDI MOD lanes on its own MIDI port). Merged upstream as [#399](https://github.com/sd88me/mpc-vst-plugins/pull/399) (2026-10-12), rebuilt on his main: his wrapper has no `QLINK_TRAVEL`, so it resets `settle()`'s `last_pos` instead, with a test engine (`poc/selfset`) |
| [`b014168`](https://github.com/saustin2010/mpc-vst-plugins/commit/b014168) | `QLINK_TRAVEL` keeps `nudge_pct`: a long whole-number list steps one per Q-Link event or wheel click | With travel on, a 0-255 sample number moved 2 per Q-Link event and 2.5 per wheel click, skipping numbers (owner on the Live II, 2026-10-19), and `nudge_pct` was never read. | 1: Incipience (SAMPLE, SCROLL). Not offered to sd88me: his wrapper has no `QLINK_TRAVEL` |
| [`dedcf6c`](https://github.com/saustin2010/mpc-vst-plugins/commit/dedcf6c) [`a532d37`](https://github.com/saustin2010/mpc-vst-plugins/commit/a532d37) [`ddc161c`](https://github.com/saustin2010/mpc-vst-plugins/commit/ddc161c) [`1c2dfa4`](https://github.com/saustin2010/mpc-vst-plugins/commit/1c2dfa4) | sd88me's wrapper fix, his #486-#489 cherry-picked (2026-10-23): no UpdateDisplay on a plain host write unless the param has `"refresh_display": true`, at most one per 50 ms, `params.lock`, PORTING notes | Q-Links didn't move parameters on MPC's track automation screen (his discussion #499 asks every port to rebuild with it) | all |
| [`b4fa97d`](https://github.com/saustin2010/mpc-vst-plugins/commit/b4fa97d) [`cd56d54`](https://github.com/saustin2010/mpc-vst-plugins/commit/cd56d54) | NOTES without the copies the cherry-pick brought; `test_port.sh` mounts the tools once for a port inside the repo (as upstream) | Tidy-ups of the cherry-pick | the poc ports |
| [`f7fa837`](https://github.com/saustin2010/mpc-vst-plugins/commit/f7fa837) | Host test: `"clamped"` params left out of the nudge and preset checks too | A sample list with nothing loaded, a delay time synced to the tempo, read back what the engine holds | Percolator, Mutable Vibe, Libpo32, Hush One, Plaits |
| [`38a5e0e`](https://github.com/saustin2010/mpc-vst-plugins/commit/38a5e0e) [`de1f916`](https://github.com/saustin2010/mpc-vst-plugins/commit/de1f916) [`e3a0af4`](https://github.com/saustin2010/mpc-vst-plugins/commit/e3a0af4) | From this repo's old patch: a `banks=` stepper's arrows from its own sub-page's picture; a list's `ink=` / `lit=` / `focus=no`; a readout's `vs=` / `ink=` | Percolator, Percolator OUT and Incipience OUT build the screens they were approved with | those three (and the browsers) |
| [`923db97`](https://github.com/saustin2010/mpc-vst-plugins/commit/923db97) | `docs/NOTES.md`: the 15 sections of device and porting notes this repo's old patch kept | Kept when the patch was retired | - |
| [`ea7f73c`](https://github.com/saustin2010/mpc-vst-plugins/commit/ea7f73c) | `mpc_engine_transport()` gets MPC's recording flag (`MPC_TRANSPORT_RECORDING` added to `playing`); NOTES: MPC's count-in as a plugin sees it | MPC reports playing through its count-in, so sequencers played the count-in bar; with the flag a sequencer can wait out a count-in when recording (Live II, 2026-10-24) | the 10 MIDI sequencers (dev-tools/midifx: COUNT-IN on both Stevequencers) |

Most are opt-in: they change nothing unless a plugin's own files ask for them (a layout line, a `params.json` or `vst.json`
key, a `defines` entry, the transport hook), so sd88me's own ports build as before. The rest are fixes: the option-list read
past its end, the installer's path match, the host test (macOS, long ranges, travel) and the release workflow's `tools_repo`. Since
2026-10-23 `tools/build.sh` builds with this branch too (`setup.sh`), and the patch below is retired.
The pin is `ea7f73c` since 2026-10-24 (MPC's recording flag for the count-in).
The plugins' `release.yml` name `e3a0af4`, the pin of their 2026-10-23 releases (sd88me's wrapper fix), until their
next release runs `prepare.py`.
To move the pin: commit on `steve-features`, push it to the fork, set `REF` in `setup-release.sh`, run
`python3 dev-tools/catalogue/prepare.py --ready` (it rewrites each plugin's `release.yml` and `FRAMEWORK.md`), then
`check.sh` the plugins.

## Before 2026-10-23: the patch

Until 2026-10-23 this folder held `mpc-vst-plugins.patch`, our changes against sd88me's `39660f2`, which `tools/build.sh`
built with while the releases used this branch. All 44 plugins now build and pass on the branch (with sd88me's wrapper
fix), so the patch is gone (it is in git history). What it had that a plugin needs went to the branch (the rows of
2026-10-23 above); its notes are in the branch's `docs/NOTES.md`. Five screens it drew aren't reproduced by the
branch's tools (sub-page mode pictures, a stepper drawn into its sub-page's picture, knob `label_scale`, meter strips):
Incipience, Stevequencer, Stevequencer 16, MPC Plaits and Mutable Vibe keep their committed `deploy/` screens
(`tools/build.sh` `KEEP_SCREEN`; their releases ship `deploy/` too). The private controller builds keep their own
copy of the old base with the wrapper fix back-ported (outside this repo).

To change one of those five screens, draw it with the old tools and keep it (done 2026-10-24 for the Stevequencers'
COUNT-IN): `git -C framework/release-tools worktree add --detach $PWD/framework/old-tools 39660f2`, then
`git -C framework/old-tools apply` the patch as it was last committed (`git show df04efc1^:framework/mpc-vst-plugins.patch`);
`bash framework/old-tools/tools/build_port.sh <plugin>/vst.json` draws `build/skin/`, which replaces
`deploy/Synths/<folder>`; then `tools/build.sh <plugin>` builds the plugin itself on the current framework (the screen
stays). Compare the new screen with the committed one first: only the pages you changed should differ.

A rebase of the branch onto sd88me's current `main` (tried 2026-10-23) changes 33 of the 44 screens: his newer skin
tools draw stepper text, arrows, mode pictures and buttons differently. That is a separate job with a screen review.
