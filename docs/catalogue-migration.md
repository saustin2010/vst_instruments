# Moving the plugins into the catalogue

A plan, started 2026-10-08. Tick the steps off here (with the date) as they're done, and keep
[ROADMAP.md](../ROADMAP.md) pointing at this file.

sd88me's catalogue ([site](https://sd88me.github.io/mpc-vst-plugins/),
[source](https://github.com/sd88me/mpc-vst-plugins/tree/main/catalog)) lists **one plugin per GitHub repo** and reads
its versions from that repo's releases. To be listed, a plugin needs a public repo with a licence, a release zip made
by sd88me's `tools/release.py --repo owner/name --license <SPDX>` (which writes the `mpc-plugin.json` the catalogue
reads), and one pull request adding `catalog/plugins/<id>.json` to his repo. Peers suggested starting with one or two
plugins as a pilot, and one offered to review the first.

**In short:** this repo stays the master where all the work happens. Each plugin we publish gets its own repo,
`saustin2010/mpc-vst-<name>`, pushed from its folder here with `git subtree`. Releases are made in that repo by
sd88me's release workflow, and a GitHub Project board tracks every plugin across the repos. 30 of our 40 plugins are
candidates: 6 are already in the catalogue (offer those as skins instead) and 4 are waiting on a licence. Before any
of that, our framework patch has to catch up with upstream (Phase 0).

## What is already published (2026-10-08)

The catalogue's `catalog.json` (generated 2026-10-08 02:02 UTC) has 69 entries: 22 Airwindows effects and these 47.

| Publisher | Plugins |
|---|---|
| sd88me | Acid (303-style acid *sequencer*), Boris Granular, Clementine-XT, Crate Digger, Dexed (DX7), Euclidier, JV-880, Lucky Dip, Maze Sequencer, Maze Voice, Morpho-PE, OTTmpc, Profit-08, TR-MPC with 6W6, 8W8, 9W9, CW-78 and TR-MPC Tap FX; Machinemodule and Monomodule (build-yourself: they need the user's firmware) |
| jacob-sabella | Chordsmith, Keyscope, NAM; addins MPC Commander, MPC Remote, MPC USB audio |
| gmorb | Dragonfly Early Reflections, Hall, Plate, Room; Liminal Hz |
| poloq-instruments | MPC Plaits, Plugin Manager (installs catalogue plugins on the device) |
| nachtaktiv303 | Mutable Vibe MPC (Rings + Plaits), Vibe FX |
| Devko | EffectForce, PolyForce, SubForce |
| FullPace | Glass Spheres (a Marbles-style random sampler), Overcast |
| Others | Helm (Lewinator56), Dub Force Siren (no3z), MeowSynth (mcardonaserrano), Omni Sampler (B0ss-M), RMXXXL (sunskiefer), Stream (stampman3000-jpg) |

On GitHub but not in the catalogue yet (a search on 2026-10-08): lunarscraper's `mpc-vst-` airmaster, cloudreverb,
faderfx, filter, fsvr, fsvrvoc, rat, riffbox and wumms, and enorrmann's `mpc-vst-helm`. None overlap ours except
that Helm.

The rules, from the catalogue's [Add yours](https://github.com/sd88me/mpc-vst-plugins/blob/main/catalog/pages/add.md)
page, [docs/CATALOG_SPEC.md](https://github.com/sd88me/mpc-vst-plugins/blob/main/docs/CATALOG_SPEC.md),
[docs/RELEASING.md](https://github.com/sd88me/mpc-vst-plugins/blob/main/docs/RELEASING.md) and PORTING.md section 5:

- A public repo with the source and a root `LICENSE` (SPDX id). No closed binaries, no Akai files, no content we
  have no right to share.
- A release zip `<Name>-<X.Y.Z>-mpc-armv7.zip` made by `release.py` or the reusable `vst-release.yml` workflow, with
  `--repo` and `--license`. `tools/catalog_check.py <zip> --catalog --expect-id <id> --expect-repo <owner/name>`
  must print OK.
- The zip installs the **portable layout**: the plugin is one folder in `/sdcard/Synths` with the `.so` inside it,
  registered from its own `plugin-meta.xml`. Engines find their data next to the `.so` (`MODULE_SUBDIR`), never at
  a fixed `/sdcard/vst/...` path. Its `install.sh` stops and restarts MPC, backs up `MPC.settings`, replaces any entry
  with the same uid, and moves an old `/sdcard/vst` install's user files in.
- Keep the `uid`, `.so` name and catalogue id fixed forever. X of X.Y.Z is `param_compat`: bump it only when
  parameter indices change (we never do: parameters are append-only).
- Every night the catalogue re-reads each repo's releases and checks each zip. A failing release is left out and an
  issue is opened. Prereleases appear as beta. An optional `tested.json` at the repo root shows "Tested on".
- MPC OS 2.x support is worked out from the zip (glibc and skin file versions). Our Stitch skins will be listed as
  "MPC OS 3.x only", which is fine.

## Our 40, sorted

| Group | Plugins | What to do |
|---|---|---|
| **Already in the catalogue** (6) | MPC Plaits (poloq's), Mutable Vibe (nachtaktiv303's), Helm (Lewinator56's), Maze Lite (sd88me's own Maze Sequencer), Plaits (poloq's MPC Plaits has all 24 models, polyphonic), Marbles (FullPace's Glass Spheres) | Don't release. Offer the screens as skins (below) |
| **Near neighbours** (4) | Eucalypso (Euclidier is also Euclidean, but from a different source), Mono Voice (Monomodule is the real Monomachine engine but build-yourself only), Rings and Rings FX (Mutable Vibe includes six Rings resonator models) | Different plugins, so release them unless you'd rather not |
| **No licence** (2) | Hush One, Libpo32 (no licence upstream, and the authors can't be asked) | Stay here only, not published |
| **Written for this repo** (2) | Stevequencer, Stevequencer 16 (MIT, chosen 2026-10-08) | Released 2026-10-10 (below) |
| **New to the catalogue** (26) | Instruments: 303, Aphex, Braids, Chordism, Denis, Elements, Fizzik, Hank, Hera, MonkSynth, Moog, Mr Drums, Mr Hyde, Noisemaker, NuSaw, OB-Xd, Tablor, Wurl. Sequencers: Grids, Groove Bank, MIDI Player, Pixel Walkers, Rampage, Super Arp. Effects: Verglas, Warps | Release |

No catalogue entry shares a uid, `.so` name or skin folder name with the 30 candidates (checked 2026-10-08 against
every version's manifest). The clashes are all in the first group. Our Helm uses Lewinator56's uid `Helm` and
`helm.so`. Our MPC Plaits and Mutable Vibe have their authors' uids (`MiPl`, `MtVb`), so installing their releases
replaces our builds on the device. Our Plaits' `plaits.so` has the same name as poloq's, but in a different folder.

303 complements sd88me's Acid: Acid is a sequencer that drives a synth track, and 303 is the synth it needs.

## Skins for the plugins already listed

The catalogue has no place for an alternative skin. A skin is tied to one build's parameter indices and is shipped
inside that plugin's zip. There are three routes:

1. **Offer it to the author** as a pull request adding our screen as an optional skin for their release. This route
   is closest for MPC Plaits and Mutable Vibe, because we build them from the authors' own source. Compare our
   `params.json` with their current release first: our Mutable Vibe adds a VOLUME parameter at the end that their
   build doesn't have.
2. **For Helm, Maze Lite, Plaits and Marbles**, our screens drive different engines (the Schwung modules) from the
   listed plugins. Offer the design (`design/stitch.html` and the screenshots) as a starting point, not the built skin.
3. **Ask sd88me** whether the catalogue could carry skin packs: an entry with its own id, keyed to a plugin's id and
   `param_compat`.

Until then they stay as they are in this repo and on the device.

## How the repos fit together

GitHub can't fork a folder: a fork copies the whole repo, all 40 plugins included. The folder-level equivalent of
"fork the instruments off this repo" is a **split**. `git subtree split --prefix=<plugin folder>` turns the history
of one folder into a branch whose root is that folder, and the branch is pushed to the plugin's own repo.

| | Master + split repos (recommended) | Plugin repos are the masters, this repo pulls them in as submodules | Separate repos only (sd88me's way) |
|---|---|---|---|
| Where you work | Here, as now | In each plugin repo, or here with submodules | In each plugin repo |
| A change across all plugins (NRPN, name tags, a skin fix) | One commit | One commit per repo, plus a submodule bump here | One commit per repo |
| Shared tools (`tools/build.sh`, the Stitch pipeline, `check_skin.py`, `install.sh`, fake-mpc) | Unchanged | Paths unchanged, but submodules get in the way | Copied into each repo, or a new tools repo |
| Contributors | PR the plugin repo; you pull it back here | PR the plugin repo | PR the plugin repo |
| Risk | Someone commits straight to a split repo | Submodule pointers out of date | 30 copies of the tooling drift apart |

The recommended model, master + split repos, has these rules:

- **All work happens here.** Each plugin folder holds everything its repo needs at the root: `LICENSE`, `README.md`,
  `.github/workflows/release.yml` (which does nothing in this repo, because GitHub only runs root workflows),
  `tested.json`, `screenshots/` and the build files.
- **`tools/publish.sh <plugin>`** (to write) runs `git subtree split` on the folder and pushes the result to
  `saustin2010/mpc-vst-<name>` `main`. The same history always gives the same commits, so the push is always a fast
  forward. The script refuses to push if the plugin repo has commits this repo lacks.
- **Nobody commits straight to a plugin repo.** Merge a contributor's PR there, then bring it back with
  `git subtree pull --prefix=<plugin folder> mpc-vst-<name> main` before the next publish.
- **Releases and tags live in the plugin repos**, because the catalogue reads releases from the entry's `repo`.
  Users' issues land there too, and the Project board collects them.
- **Decide the owner before the first release.** `owner/name` is written into every zip's manifest (`source_repo`)
  and must equal the catalogue entry's `repo`, so moving a repo to an organisation later leaves its old releases
  mismatched. A personal account (`saustin2010`, as sd88me, poloq and gmorb do) is simplest. An organisation is worth
  it only if other people will maintain plugins alongside you.

## Working across them: the project

- **On your Mac:** this repo is the workspace, as it is now. One checkout, `tools/build.sh`, the Stitch pipeline,
  `install.sh` for the whole set, and Claude sessions run from here.
- **On GitHub:** a Project (Projects v2) under `saustin2010`, with one item per plugin (all 40, so the held ones are
  visible too). Each plugin's work is an issue in this repo ("Catalogue: Hera"), and the plugin repos' own issues
  and PRs are added automatically by the Project's auto-add workflow (filter `repo:saustin2010/mpc-vst-*`).
  - Fields: **Stage** (Hold, Licence, Ready, Builds on upstream, Repo published, Draft release, Device tested,
    Catalogue PR, Listed), **Group** (Schwung / Mutable Instruments / VCV Rack / originals / MPC ports), **Kind**,
    **Licence**, **Catalogue id**, **Repo**, **Design QA**.
  - Views: a board by Stage, and a table by Group.
  - Creating it from the terminal needs the `project` scope on `gh`, which the current login doesn't have:
    `gh auth refresh -s project`.

## Phase 0: catch the framework up (this blocks everything)

Our plugins build with `framework/mpc-vst-plugins.patch` on sd88me's `39660f2` (his PR #8). Upstream `main` is at
#229, and the patch no longer applies there: it fails on `docs/NOTES.md`, `tools/gen_vst.py`, `tools/host_test.c`,
`tools/shadow_skin.py` and `wrapper/vst2_wrap.c`. Most of it is upstream already: on 2026-10-07 sd88me studied this
repo ([docs/COMMUNITY_SKINS.md](https://github.com/sd88me/mpc-vst-plugins/blob/main/docs/COMMUNITY_SKINS.md)) and
adopted `banks=`, `ns=`/`vs=`, `bw=`, `lay=side`, filmstrip sliders and display meters, `sh=` on `enum_v`, a skin
checker, `"programs"`/`"presets"` (MPC's PRESET menu), the per-instance engine-call lock, and MIDI CC 20-35 and NRPN,
but often as his own implementation, so a three-way merge of the patch gives two of each.

**The approach (2026-10-08):** start from his `main` and add back, one commit each, only what a plugin being released
needs, on the branch `steve-features` of the fork [saustin2010/mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins/tree/steve-features).
Those commits are what the plugins' releases build with (`framework/setup-release.sh`, the `release.yml` pins), and each
is meant to go to sd88me as a PR. `tools/build.sh` keeps using the old base and patch until all 40 build on the new one.

- [x] For the pilot (2026-10-08, at `6c87b6d`): a hand-made `params` file beside a Schwung `module` (22 plugins); the
      host test in the Linux container on macOS (Apple's ASan hangs on macOS 26); `focus_ring=1` and `qlink_box=slot`
      (opt-in layout lines that keep our screens' look: his defaults hide the focus ring and draw tighter Q-Link
      boxes); `when=<param>:<i>/<N>` bands on a continuous parameter; a `tools_repo` input on the release workflow.
- [x] For the rest (2026-10-08, at `55e60a0`): option `values` / `send` (Eucalypso, Super Arp, MIDI Player), option
      labels matched before numbers, and a fix for a read past an option list (his wrapper overflowed on Aphex's preset
      under ASan); `mpc_engine_transport()`, the host's tempo, position and play state for engines clocked from it (the
      sequencers: without it they'd pass offline and never play in time on the MPC); the host test's wheel check on
      long whole-number ranges (a click moves 1/100 of a 0-5000 ms range, rightly); `"programs"` `count`, `name_at` and
      `name` (19 plugins: menus without empty slots, names without loading each preset); `"clamped"` params the engine
      limits to what it has loaded (Groove Bank's pattern, MIDI Player's file and track), which the host test skips.
- [x] From the first device test (2026-10-08, Live II): the installer refused names with brackets (`[SYN] 303`: its
      check read them as a grep pattern; MPC.settings was left unchanged), fixed with a test; and his wrapper's
      per-event stepping flipped two-option switches when other Q-Links were turned. `"defines": {"QLINK_TRAVEL": 1,
      "SET_IF_CHANGED": 1}` (on in all 30) brings back this repo's behaviour: ticks add up like a detented knob, and a set
      to the value the engine holds is skipped. Release tools now `f210a56` (the pilots' drafts: `10d8f5f`, same wrapper).
- [x] For the Stevequencers (2026-10-10, at `7afa72e`): `"live"` params in `vst.json`, display params the engine moves
      by itself (the playing step), reported to MPC after every block so the step light follows playback; and the host
      test reads a preset's option value as what the option sends (its `values` entry or `send` word), not its index.
      Only the two Stevequencers are released at `7afa72e`; the other 28 keep `f210a56` in their `release.yml` until
      their next release.
- [ ] Offer the 16 commits to sd88me as PRs (each plugin's `FRAMEWORK.md` and `framework/README.md` explain them) (ask the owner first), and move the pins to his repo as they're merged.
- [ ] Not ported yet: `"category"` in the plugin list (13 plugins: his entries are Synth or Effect, so our sequencers
      and drum machines are Synth in a release's entry; harmless, since MPC ignores the category and the kind tags do
      the sorting), and the host test's extra checks from our patch (restore, slow Q-Link, effect and program checks).
- [ ] When all 40 build on the new base: replace `mpc-vst-plugins.patch` and `setup.sh`'s pin, and update
      `framework/README.md` and BUILDING.md.

Each plugin's `layout.conf` gets four lines so his tools draw the screen it was checked with (`check.sh` then says
"screen: same"): `qlink_bounds=column`, `qlink_box=slot`, `label_scale=1` (his default scales value text by 1.15) and
`focus_ring=1`. The old tools ignore them. Glue changes in the plugins themselves (both tools build them): the three
effects (Verglas, Warps, Rings FX) give his wrapper the `process()` it asks effects for (it read past their engine
table otherwise); Hera answers `preset:<n>` with a preset's name.

One screen changes on his tools: **Aphex's** envelope displays get 28 frames instead of 32, because 32 frames of
428 px make a 13,696 px strip, over the 12,288 px a filmstrip draws right at (our own Live II finding). Check it on the
device with the release.

## Phase 1: the project and the publish tooling

- [x] Decisions answered (2026-10-08, below).
- [ ] Project board created, with one item per plugin and its Stage filled in from the table above (needs
      `gh auth refresh -s project` first).
- [x] `tools/publish.sh <plugin> [--create] [--dry-run]` (2026-10-08): subtree split of the committed folder, pushed
      to `saustin2010/mpc-vst-<plugin>`; refuses a dirty folder or a plugin repo with commits this repo lacks.
- [x] `dev-tools/catalogue/check.sh <plugin>` (2026-10-08): build with the release tools, host test, screen the same
      as `deploy/` (`skin_same.py`, by content). `framework/setup-release.sh` fetches those tools.
- [x] `dev-tools/catalogue/prepare.py <plugin>` (2026-10-08): writes a folder's repo files from its table of ids,
      licences and user folders (LICENSE fetched from the upstream repo or the text in `licenses/`, `release.yml`,
      `release/` for a library, `MODULE_SUBDIR`, README sections). `check.sh` compares pictures by their pixels (the
      newer tools re-encode and share pop-up images).

## Per-plugin checklist

The pilots (303, Hera) are the worked examples: copy from their folders.

1. **Licence:** a root `LICENSE` in the plugin folder: the upstream's own file with its copyright lines (MIT, BSD), or
   the licence text (GPL, from `licenses/`). It is missing today for 303 and Hera (done), Braids, Hank, Moog, Mr Drums,
   Mr Hyde, Noisemaker and OB-Xd (their licences come from Schwung module tarballs: fetch the file from the module's
   source repo), and for the four held plugins.
2. **Data next to the `.so`:** add `"MODULE_SUBDIR": "\"<plugin>\""` beside `MODULE_DIR` in `vst.json` (Braids, Groove
   Bank, MIDI Player, Mr Drums, Noisemaker, OB-Xd, Tablor; Hera done). The data folder is then `<plugin>/` next to the
   `.so`: in a release's plugin folder, and with this repo's installer `/sdcard/vst/<plugin>`, as before.
3. **Presets in the zip:** move the plugin's `tools/fetch-presets.py` entry to `release/library.json` and copy
   `release/fetch-library.py` (as Hera's); the release build fetches into `library/` and ships it with
   `extra: library:<plugin>`. Check that each library's licence allows redistribution. Folders where the user adds
   files (OB-Xd and Noisemaker banks, Libpo32 kits) go in `user_data`, so the installer keeps them.
4. **Name:** releases keep the kind tags (`[SYN] 303`; decided 2026-10-08). The zip is then `SYN-303-<version>-...`.
5. **Catalogue id:** kebab-case and permanent: `open303` and `hera` for the pilots.
6. **Keep the `uid` and `.so` name** as they are (your projects use them). Start at version 1.0.0; tags are `v<version>`.
7. **Plugin-repo files** in the folder: `LICENSE`, `.gitignore` (`build/ logs/ preview/ library/ dist/`),
   `.github/workflows/release.yml` (copy the pilot's; it pins the release tools and names `plugin_id`, `license`,
   `about`, and for a library its fetch, test and `extra`), and README sections for a reader of the plugin repo:
   absolute links to this repo's docs, "Install" (the release zip, or the collection), "Building" and "Development".
   `tested.json` after the device test.
8. **Screen:** the four layout lines (Phase 0), then `dev-tools/catalogue/check.sh <plugin>` must print PASSED and
   "screen: same"; a plugin whose engine names its presets differently gets the `<key>:<n>` answer (Hera's
   `upstream-changes.diff` shows the five lines).
9. **`deploy/`:** the split repo carries it (including the `.so`) because it is in the folder's history. That is
   harmless: the catalogue uses the release zip. Revisit if the repos get heavy.
10. **Release:** Actions, then a dry run, then a draft release. Install the draft's zip on the Live II: this stops and
   restarts MPC, so only with your go-ahead. Run RELEASING.md step 5 (play, turn every page and Q-Link, save and
   reload a project, uninstall and reinstall) and `tools/bench.sh` (commit its `-j` output as `bench.txt`). Then
   publish the draft, which creates the tag.
11. **List it:** `catalog_check.py <zip> --catalog --expect-id <id> --expect-repo saustin2010/mpc-vst-<name>` prints
    OK, then open a PR to sd88me/mpc-vst-plugins adding `catalog/plugins/<id>.json`. Include `screenshot` as a raw
    link pinned to the tag (2:1 frame: `screenshots/page_0.png` is 1280x628, close enough).
12. **Tell the upstream author** (most of these are other people's Schwung modules) before the listing goes up, and
    offer to hand the repo over if they would rather host it.

## Phase 2: the pilot

**303 and Hera**, one simple and one that tests the harder parts:

- **303:** Design QA ✅, GPL-3.0, nothing like it is listed (and it pairs with sd88me's Acid), no data folder.
  sd88me already holds its skin up as a model in COMMUNITY_SKINS.md.
- **Hera:** Juno-60, Design QA ✅, GPL-3.0, 56 presets. It covers the data folder next to the `.so`, presets in the zip
  and an engine naming its presets, so the other 28 follow a known path.

- [x] Both build on the release tools, pass the host test, and draw the same screen as their device builds; both still
      build on the old tools too (2026-10-08).
- [x] Local release zips pass `catalog_check.py --catalog` (2026-10-08): `SYN-303-1.0.0` (`open303`, 2.0 MB) and
      `SYN-Hera-1.0.0` (`hera`, 3.5 MB, presets in `hera/`). Both MPC OS 3.x only, as every Stitch skin will be.
- [x] Repos created and published (2026-10-08): [mpc-vst-303](https://github.com/saustin2010/mpc-vst-303),
      [mpc-vst-hera](https://github.com/saustin2010/mpc-vst-hera). The release workflow's dry run and then a draft
      release v1.0.0 each, built by GitHub Actions.
- [x] Device test of each draft's zip on the Live II (2026-10-08), after two fixes (above); `tested.json` in both repos.
- [x] Releases v1.0.0 published and proposed to the catalogue (2026-10-08): sd88me/mpc-vst-plugins
      [#230](https://github.com/sd88me/mpc-vst-plugins/pull/230) (Hera) and [#231](https://github.com/sd88me/mpc-vst-plugins/pull/231) (303);
      his catalogue build, run locally on the two entries, found no problems.
- [ ] sd88me's review and merge; `bench.txt` (CPU) for the next release.

## Phase 3: the rest

Status 2026-10-10: 21 in the catalogue (the pilot's two and #236's 19), 11 released and proposed in #255, 2 not
published. A plugin was ready when its folder had its repo files (`dev-tools/catalogue/prepare.py`) and
`dev-tools/catalogue/check.sh` said PASSED and "screen: same" on the release tools (`f210a56`; the Stevequencers:
`7afa72e` and "screen: ships deploy/"). They waited for the pilot to be in the catalogue (2026-10-08); each went up
with `tools/publish.sh <plugin> --create`. GitHub stops an account creating repos after about ten in a window ("You
have created too many repositories, too quickly"), however far apart they're spaced: the last 8 went up as one batch the next day.

| Plugin | Repo | Catalogue id | Licence | Ships | Status |
|---|---|---|---|---|---|
| Open303 (formerly 303) | mpc-vst-open303 | `open303` | GPL-3.0-only | | **in the catalogue**: v1.0.0, PR #231 merged 2026-10-08; 2026-10-13: renamed **Open303** (repo mpc-vst-open303), v1.1.0 to come |
| Hera | mpc-vst-hera | `hera` | GPL-3.0-only | 56 presets | **in the catalogue**: v1.0.1 (2026-10-10: STOP keeps edits, parameter 0 fix), v1.0.0 PR #230 merged 2026-10-08 |
| Aphex | mpc-vst-aphex | `aphex` | MIT | | **in the catalogue**: v1.0.0, #236 merged 2026-10-09 (offline only); envelope displays 28 frames (above) |
| Macro Osc (formerly Braids) | mpc-vst-macro-osc | `braids` | MIT | 10 presets | **in the catalogue**: v1.0.1 (2026-10-10: STOP keeps edits, parameter 0 fix), v1.0.0 #236 merged 2026-10-09 (offline only); 2026-10-12: renamed **Macro Osc** (repo mpc-vst-macro-osc), **v1.1.0 released 2026-10-13** (offline: check.sh, catalog_check) ([below](#other-companies-names-2026-10-12)) |
| Chordism | mpc-vst-chordism | `chordism` | MIT | | **in the catalogue**: v1.0.1 (2026-10-10: STOP keeps edits, parameter 0 fix), v1.0.0 #236 merged 2026-10-09 (offline only) |
| Denis | mpc-vst-denis | `denis` | MIT | | **in the catalogue**: v1.0.0, #236 merged 2026-10-09 (offline only) |
| Modal Voice (formerly Elements) | mpc-vst-modal-voice | `elements` | MIT | | **in the catalogue**: v1.0.0, #236 merged 2026-10-09 (offline only); 2026-10-12: renamed **Modal Voice** (repo mpc-vst-modal-voice), **v1.1.0 released 2026-10-13** (offline: check.sh, catalog_check) ([below](#other-companies-names-2026-10-12)) |
| Fizzik | mpc-vst-fizzik | `fizzik` | MIT | | **in the catalogue**: v1.0.1 (2026-10-10: STOP keeps edits, parameter 0 fix), v1.0.0 #236 merged 2026-10-09 (offline only) |
| MonkSynth | mpc-vst-monksynth | `monksynth` | MIT | | **in the catalogue**: v1.0.0, #236 merged 2026-10-09 (offline only) |
| Mono Voice | mpc-vst-monovoice | `mono-voice` | MIT | | **in the catalogue**: v1.0.1 (2026-10-10: STOP keeps edits, parameter 0 fix), v1.0.0 #236 merged 2026-10-09 (offline only) |
| RaffoSynth (formerly Moog) | mpc-vst-raffosynth | `raffosynth` | MIT | 14 presets | **in the catalogue**: v1.1.0 (2026-10-12: EMPHASIS and CUTOFF fixed, FILTER CLEAN/DRIVE switch; the repo build played on the Live II), v1.0.0 #236 merged 2026-10-09 (offline only); 2026-10-12: renamed **RaffoSynth** (repo mpc-vst-raffosynth), **v1.2.0 released 2026-10-13** (its release zip played on the Live II) ([below](#other-companies-names-2026-10-12)) |
| Mr Drums | mpc-vst-mrdrums | `mr-drums` | MIT | starter kit (made here); keeps your kits | **in the catalogue**: v1.0.0, #236 merged 2026-10-09 (offline only) |
| Mr Hyde | mpc-vst-mrhyde | `mr-hyde` | MIT | | **in the catalogue**: v1.0.1 (2026-10-10: STOP keeps edits, parameter 0 fix), v1.0.0 #236 merged 2026-10-09 (offline only) |
| Noisemaker | mpc-vst-noisemaker | `noisemaker` | GPL-2.0-only | keeps your banks | **released** v1.0.0 2026-10-10, proposed in #255 (offline only) |
| NuSaw | mpc-vst-nusaw | `nusaw` | MIT | | **in the catalogue**: v1.0.1 (2026-10-10: STOP keeps edits, parameter 0 fix), v1.0.0 #236 merged 2026-10-09 (offline only) |
| OB-Xd | mpc-vst-obxd | `obxd` | GPL-3.0-only | factory bank; keeps your banks | **in the catalogue**: v1.0.0, #236 merged 2026-10-09 (offline only) |
| Wurl | mpc-vst-wurl | `wurl` | GPL-3.0-only | | **in the catalogue**: v1.0.0, #236 merged 2026-10-09 (offline only) |
| Resonator (formerly Rings) | mpc-vst-resonator | `rings` | MIT | | **in the catalogue**: v1.0.1 (2026-10-10: STOP keeps edits, parameter 0 fix), v1.0.0 #236 merged 2026-10-09 (offline only); 2026-10-12: renamed **Resonator** (repo mpc-vst-resonator), **v1.1.0 released 2026-10-13** (offline: check.sh, catalog_check) ([below](#other-companies-names-2026-10-12)) |
| Resonator FX (formerly Rings FX) | mpc-vst-resonator-fx | `rings-fx` | MIT | | **in the catalogue**: v1.0.1 (2026-10-10: STOP keeps edits, parameter 0 fix), v1.0.0 #236 merged 2026-10-09 (offline only); 2026-10-12: renamed **Resonator FX** (repo mpc-vst-resonator-fx), **v1.1.0 released 2026-10-13** (offline: check.sh, catalog_check) ([below](#other-companies-names-2026-10-12)) |
| Meta Mod (formerly Warps) | mpc-vst-meta-mod | `warps` | MIT | | **in the catalogue**: v1.0.0, #236 merged 2026-10-09 (offline only); 2026-10-12: renamed **Meta Mod** (repo mpc-vst-meta-mod), **v1.1.0 released 2026-10-13** (offline: check.sh, catalog_check) ([below](#other-companies-names-2026-10-12)) |
| Drum Map (formerly Grids) | mpc-vst-drum-map | `grids` | GPL-3.0-only | | **in the catalogue**: v1.0.0, #236 merged 2026-10-09 (offline only); 2026-10-12: renamed **Drum Map** (repo mpc-vst-drum-map), **v1.1.0 released 2026-10-13** (offline: check.sh, catalog_check) ([below](#other-companies-names-2026-10-12)) |
| Groove Bank | mpc-vst-groovebank | `groove-bank` | MIT | 14 grooves | **in the catalogue**: v1.0.1 (2026-10-10: STOP keeps edits, parameter 0 fix), v1.0.0 #236 merged 2026-10-09 (offline only) |
| MIDI Player | mpc-vst-midiplayer | `midi-player` | MIT | demo file (made here); keeps your files | **released** v1.0.0 2026-10-10, proposed in #255 (offline only) |
| Pixel Walkers | mpc-vst-pixelwalkers | `pixel-walkers` | MIT | | **released** v1.0.0 2026-10-10, proposed in #255 (offline only) |
| Dual Slope (formerly Rampage) | mpc-vst-dual-slope | `rampage` | GPL-3.0-or-later | | **in the catalogue**: v1.0.0, #255 merged 2026-10-10 (offline only); 2026-10-13: renamed **Dual Slope** (repo mpc-vst-dual-slope, maker Andrew Belt), v1.1.0 to come |
| Super Arp | mpc-vst-superarp | `super-arp` | MIT | | **released** v1.0.0 2026-10-10, proposed in #255 (offline only) |
| Eucalypso | mpc-vst-eucalypso | `eucalypso` | MIT | | **released** v1.0.0 2026-10-10, proposed in #255 (offline only) |
| Verglas | mpc-vst-verglas | `verglas` | MIT | | **released** v1.0.0 2026-10-10, proposed in #255 (offline only); 2026-10-12: maker becomes Émilie Gillet, **v1.1.0 released 2026-10-13** (offline: check.sh, catalog_check) ([below](#other-companies-names-2026-10-12)) |
| Hank | mpc-vst-hank | `hank` | MIT | | **released** v1.0.0 2026-10-10, proposed in #255 (offline only); upstream declares MIT in its README and module.json but has no file, so `LICENSE` is the MIT text naming the author (2026-10-08) |
| Tablor | mpc-vst-tablor | `tablor` | BSD-3-Clause | 115 wavetables, 9 presets; keeps your tables | **released** v1.0.0 2026-10-10, proposed in #255 (offline only); ships both wavetable packs, as Tablor's repo does (Neu KatalYst: "use them in all your synths"; owner's choice 2026-10-08) |
| Hush One, Libpo32 | | | none upstream | | **not published**: no licence, and the authors can't be asked (2026-10-08) |
| Stevequencer | mpc-vst-stevequencer | `stevequencer` | MIT (2026-10-08) | | **released** v1.0.0 2026-10-10, proposed in #255 (offline only); tools `7afa72e` (`"live"` params), ships its own screen (below) |
| Stevequencer 16 | mpc-vst-stevequencer16 | `stevequencer-16` | MIT (2026-10-08) | | **released** v1.0.0 2026-10-10, proposed in #255 (offline only); as Stevequencer |
| Percolator | mpc-vst-percolator | `percolator` | MIT (2026-10-10) | none shipped: users add the Pērkons kit packs (`user_data: percolator`) | **in the catalogue**: v1.2.0 (#255, merged 2026-10-10). **2.0.0 released** 2026-10-12 after a device test of its zip (`tested.json`; tools `eaef502`: MIDI & OUT, MIDI MOD, the Pērkons CCs, Percolator MIDI In, COMP SEND/LEVEL); its entry has `asset_pattern` since sd88me/mpc-vst-plugins#398 (merged 2026-10-12), as the repo now has two zips. **2.0.1 released** 2026-10-13 after a device test of its zip (kit packs one folder down found too; `tested.json`); ships its own screen; written for this repo |
| Percolator OUT | mpc-vst-percolator (`percolatorout/`) | `percolator-out` | MIT | | **in the catalogue: 2.0.0 released** 2026-10-12, tag `percolator-out-v2.0.0`, its zip checked on the Live II (2026-10-12): the first companion (`prepare.py` COMPANIONS), a second zip and job in Percolator's release, as TR-MPC's Tap FX. Published (not as Latest) once sd88me had merged the entries (`entries/percolator.json` with `asset_pattern`, `entries/percolator-out.json`; sd88me/mpc-vst-plugins#398, merged 2026-10-12, with the saustin2010 author for Percolator and both Stevequencers): his nightly build matches every `*-mpc-armv7.zip` in the repo against an entry without a pattern. His `catalog_build.py` on both entries after publishing: 0 problems. 2.0.1 released with Percolator's 2.0.1 (2026-10-13), the same `.so` as 2.0.0 |
| Incipience | mpc-vst-incipience | `incipience` | MIT (2026-10-13) | none shipped: users add the AlphaBase sample set (Jomox's free download; `user_data: incipience`) | **1.0.0 released** 2026-10-19 after a device test of its zip (`tested.json`; tools `b014168`: `nudge_pct` with `QLINK_TRAVEL`, so SAMPLE steps one per Q-Link tick); proposed in [sd88me#480](https://github.com/sd88me/mpc-vst-plugins/pull/480) (owner, 2026-10-19: "get this loaded up to 88 as a new instrument"); ships its own screen (`banks=` sub-pages, as the Stevequencers); written for this repo |
| Incipience OUT | mpc-vst-incipience (`incipienceout/`) | `incipience-out` | MIT | | **1.0.0 released** 2026-10-19, tag `incipience-out-v1.0.0`, its zip checked on the Live II with Incipience's; a companion as Percolator OUT; proposed in #480 |

The Stevequencers and Percolator ship the screen built here (`deploy/Synths`, from this repo's framework) instead of one from sd88me's
skin builder: his builder draws their `banks=` pages and their readouts (`vs=`, `ink=accent`, `box=no`) differently (Percolator:
its readouts' size and colour), so
the two can't be made to match with layout lines. `prepare.py` lists them in `OWN_SKIN`: their `release.yml` copies
`deploy/Synths` into the release, their `FRAMEWORK.md` says so, and `check.sh` prints "screen: ships deploy/" in
place of "screen: same". Both release zips' skins are byte-identical to `deploy/` (2026-10-10).

- [x] Publish the ready ones: `tools/publish.sh <plugin> --create`, a draft release each, `catalog_check --catalog` on
      every zip, then published (2026-10-09 and 2026-10-10). Device tests of the release zips are still to do.
- [x] The held ones (2026-10-10): the Stevequencers, after `"live"` params were added to the release tools.
- [ ] Skins for the plugins already listed: the offers above.

## Other companies' names (2026-10-12)

sd88me added a rule to the catalogue on 2026-10-12 (`catalog/README.md`, "Skins and branding"): a listed plugin's
name, skin and screenshot carry no other company's name or logo; the summary and tags may say what it models ("Minimoog-style")
as long as they don't suggest the original maker made it. He delisted LXRXXL for its screen and renamed our "Moog" to
RaffoSynth in its entry. Mutable Instruments goes further in its own repo: the name "Mutable Instruments" "should not be used
on any of the derivative works", and it advises against keeping the module names. So:

| Was | Now | Maker in MPC | Repo | Kept |
|---|---|---|---|---|
| Moog | RaffoSynth | Raffo | mpc-vst-raffosynth | uid `Moog`, `moog.so`, id `raffosynth` |
| Braids | Macro Osc | Émilie Gillet | mpc-vst-macro-osc | uid, `.so`, id `braids` |
| Plaits | Macro Osc 2 | Émilie Gillet | (not released) | uid, `.so` |
| Elements | Modal Voice | Émilie Gillet | mpc-vst-modal-voice | uid, `.so`, id `elements` |
| Rings | Resonator | Émilie Gillet | mpc-vst-resonator | uid, `.so`, id `rings` |
| Rings FX | Resonator FX | Émilie Gillet | mpc-vst-resonator-fx | uid, `.so`, id `rings-fx` |
| Warps | Meta Mod | Émilie Gillet | mpc-vst-meta-mod | uid, `.so`, id `warps` |
| Grids | Drum Map | Émilie Gillet | mpc-vst-drum-map | uid, `.so`, id `grids` |
| Marbles | Random Sampler | Émilie Gillet | (not released) | uid, `.so` |
| Verglas | Verglas | Émilie Gillet (was Mutable Instruments) | mpc-vst-verglas | everything but the maker |
| 303 | Open303 (2026-10-13) | Robin Schmidt | mpc-vst-open303 | uid `A303`, `acid303.so`, id `open303` |
| Rampage | Dual Slope (2026-10-13) | Andrew Belt (was Befaco) | mpc-vst-dual-slope | uid `BfRp`, `rampage.so`, id `rampage` |

The folders keep their names (`tools/publish.sh` splits a folder's history; a renamed folder would no longer
fast-forward onto its repo), so `dev-tools/catalogue/repo-names.tsv` maps a folder to its new repo. Parameters are
unchanged, so projects and Q-Link assignments keep their values. Also: the screens' backgrounds (rendered from the Stitch
mockups) lost the brand text they had (Mutable Instruments, Akai, Roland, Moog, Wurlitzer, Befaco...), and three option
labels changed: Fizzik's PROPHET / OBERHEIM voicings are CLEAN 4P / WARM 4P, Noisemaker's Moog / Moog2 filters are
Ladder / Ladder 2, Aphex's REV.1 KORG-35 is REV.1 SALLEN-KEY (values are saved as numbers, so saved sounds keep them).
OB-Xd, 303 and Rampage were left for sd88me to decide; his answer on #404 (2026-10-12): 303 should be Open303 (its
engine's name); OB-Xd is fine (the name of the open-source project, not Oberheim's OB-X); Rampage "need[s] to say based on
the Befaco Rampage VCV module" and had better be renamed, since it is still Befaco's product and only its code is open.
So (2026-10-13) 303 is **Open303**, and Rampage is **Dual Slope** with Andrew Belt of VCV as its maker (the code is
VCV's, © VCV and contributors), its summary saying it is based on Befaco's Rampage VCV Rack module; its screen's
BARCELONA SYNTH LAB (Befaco's home) is OPEN-SOURCE DSP, its tab DUAL SLOPE. Dual Slope opens its MIDI port as
**[SEQ] Dual Slope MIDI Out**: turn Track on for it again in Preferences > MIDI.

What a user sees: a new name and maker in MPC's browser, and a new folder in `/sdcard/Synths` for a release install (the
`.so` moves with it; the old folder can be deleted after the update). A saved project names the plugin and its `.so`
path, but MPC finds the plugin by its uid after a rename or a move (checked on the Live II, below), so projects keep
working.
Drum Map and Random Sampler open their MIDI port under the new name: turn Track on for it again in Preferences > MIDI.

- [x] Repo: names, makers, screens, READMEs, catalogue entries, release workflows' `about`, `repo-names.tsv` (2026-10-12)
- [x] Device (2026-10-12): the renamed builds installed on the Live II (one MPC restart); the browser shows the new names
      and "Émilie Gillet" correctly (MPC stores it as `&#201;milie Gillet` in MPC.settings); old screen folders removed
- [x] Device (2026-10-12): old projects find them. "0001 Yeild", saved with [SYN] Moog at `/sdcard/vst/moog.so`, opens
      with RaffoSynth and its sound; its Percolator, saved at `/sdcard/vst/percolator.so` (gone since the release install
      moved it to `/sdcard/Synths/...`), loads too. MPC matches a saved plugin by its uid, not its name or `.so` path, so
      a release install's new folder is fine and the old one can be deleted.
- [x] GitHub (2026-10-13): the seven repos renamed (the old addresses redirect) with new descriptions, all eight
      published; `check.sh` PASSED with "screen: same" for all eight; releases built by GitHub Actions, every zip OK in
      `catalog_check --catalog` with its new repo; RaffoSynth 1.2.0's zip installed on the Live II (the `.so` matches the
      zip, `/sdcard/vst/moog.so` removed, one plugin-list entry) and played (`tested.json`); RaffoSynth 1.2.0 and the
      other seven 1.1.0 released
- [x] sd88me's catalogue: [#404](https://github.com/sd88me/mpc-vst-plugins/pull/404) (opened and merged 2026-10-13) gives the
      eight entries their new name, repo, summary and screenshot (and keeps the `"role": "midi"` he added to Grids), and
      asks about OB-Xd, 303 and Rampage. Until it is merged his nightly build skips the new versions (their zips name the
      new repos); after it, the older versions drop out instead (their zips name the old repos; all `superseded`, so no
      issues), so each plugin lists just its new version (Verglas keeps 1.0.0).
- Open303 and Dual Slope (2026-10-13, sd88me's answer on #404):
  - [x] Repo: names, maker, Dual Slope's tab and background text, READMEs, catalogue entries, release workflows (re-pinned
        to the release tools `eaef502`), `repo-names.tsv`; builds PASSED, check_skin OK, screenshots refreshed
  - [x] GitHub (2026-10-13): mpc-vst-303 renamed mpc-vst-open303 and mpc-vst-rampage mpc-vst-dual-slope (the old
        addresses redirect), both published; v1.1.0 drafts built by Actions, both zips OK in `catalog_check --catalog`
        (3 warnings each: the installer scripts are older than his newest template), `.so` byte-identical to `check.sh`'s
  - [x] Device (2026-10-13): both zips installed on the Live II with one MPC stop/start (`install.sh -y -n` each); one
        entry each, `[SYN] Open303` by Robin Schmidt and `[SEQ] Dual Slope` by Andrew Belt (category Synth, as v1.0.0's);
        the `.so` files match the zips; the old-named screen folders removed by hand (the installer leaves them), and
        `/sdcard/vst/rampage.so` by the installer; played by the owner ("seem ok"); `tested.json` 1.1.0
  - [x] Both v1.1.0 releases published (Latest) and `tested.json` 1.1.0 on both repos (2026-10-13); his `catalog_build.py`
        on the two entries: both 1.1.0 tested, the 1.0.0 zips superseded (their zips name the old repos), no issues
  - [ ] sd88me's review of [#417](https://github.com/sd88me/mpc-vst-plugins/pull/417) (opened 2026-10-13): the two entries

## Full-height screens (2026-10-19)

The Stitch screens used to leave ~110 px of the 628 empty above and below (`dev-tools/stitch` README, "Full-height
screens"; vst_instruments#29). The 24 listed plugins with a new screen were published (`tools/publish.sh`) and released
from their repos' "Release (draft)" workflow, every zip OK in `catalog_check --catalog --expect-id --expect-repo` (the
3 known warnings: installer scripts older than sd88me's newest template) and its screen byte-identical to `deploy/`'s:

- 1.1.0: Aphex, Denis, Groove Bank, Hank, Hera, MIDI Player, MonkSynth, Mr Drums, Mr Hyde, Noisemaker, OB-Xd,
  Pixel Walkers, Super Arp, Tablor, Wurl
- 1.2.0: Modal Voice, Drum Map, Open303, Dual Slope, Resonator, Resonator FX, Verglas, Meta Mod
- 1.3.0: RaffoSynth

The screens were checked on the Live II with this repo's builds (the zips are offline only, so no `tested.json`
change). [sd88me/mpc-vst-plugins#479](https://github.com/sd88me/mpc-vst-plugins/pull/479) moves the 24 entries'
`screenshot` links to the new tags; the versions arrive with his nightly build. Not released: Hush One (its layout
lacks the four display lines of 2026-10-08, so the release tools draw it differently) and Helm, Libpo32, Maze Lite,
Macro Osc 2, Random Sampler (unlisted; the release tools' offline test fails for them).

## The device and this repo's installer

A release installs in the portable layout (`/sdcard/Synths/<skin folder>/<plugin>.so`). Installing one over our
`/sdcard/vst` build is safe: same uid, so the entry is replaced, the old `.so` is removed and the user's files move
in. This repo's `install.sh` still uses the old layout. Whether it should switch to installing the release zips
(or point people at poloq's Plugin Manager) is a later decision, and nothing here depends on it.

## Decisions

Answered 2026-10-08: this repo is the master with split repos per plugin; the repos are under `saustin2010`, named
`mpc-vst-<plugin>`; released names keep the kind tags; the pilot is 303 and Hera, published now, the rest each when
it builds on the release tools.

Still open:

1. The pilot's device test: installing each draft's zip on the Live II stops and restarts MPC.
2. Publishing the 26 ready ones: going ahead (2026-10-08, the pilot is in the catalogue); 20 repos are up, 8 to go
   (midiplayer pixelwalkers rampage superarp eucalypso verglas hank tablor). GitHub stops repo creation after 10 in a
   window, however far apart (hit twice, 2026-10-08/09): run the rest as one batch later. 2026-10-09: 19 of the 20
   released (v1.0.0, every zip OK in sd88me's `catalog_check --catalog` and his `catalog_build.py`) and in the
   catalogue: [sd88me/mpc-vst-plugins#236](https://github.com/sd88me/mpc-vst-plugins/pull/236), merged 2026-10-09,
   marked offline only: the owner
   skipped the device test of the release zips, so they get `tested.json` only after one. 2026-10-10: the other 8
   repos created in one batch; Noisemaker's leaks fixed (`FilterHandler`'s Moog filter and its noise source, the
   Envelope Editor's points), and `dev-tools/catalogue/check.sh` now runs with leak detection on, as the release
   workflow does. All 9 released v1.0.0 (zips OK in `catalog_check --catalog` and `catalog_build.py`) and proposed in
   [sd88me/mpc-vst-plugins#255](https://github.com/sd88me/mpc-vst-plugins/pull/255), offline only like #236. Later
   on 2026-10-10: the two Stevequencers released v1.0.0 (tools `7afa72e`, their own screen) and added to #255, now 11
   plugins; `catalog_build.py` on all 11 with sd88me's current main (Gen2 support added): 0 problems.
3. The near neighbours (Eucalypso, Mono Voice, Rings, Rings FX): release or hold.
4. Offering the 16 framework commits to sd88me as PRs.
5. The project board: `gh auth refresh -s project` lets Claude create it.
6. Licences (answered 2026-10-08, the authors can't be contacted): Hank ships with the MIT text its author declares;
   Tablor ships both wavetable packs; Hush One and Libpo32 aren't published; the Stevequencers are MIT.
