# Roadmap

Open items as of 2026-10-05, most useful first. Tick them off here (with the date) as they're done.

## Catalogue ([docs/catalogue-migration.md](docs/catalogue-migration.md))

- [ ] **Other companies' names out** (2026-10-12; sd88me's new catalogue rule and Mutable Instruments' own terms; plan
  and status in [docs/catalogue-migration.md](docs/catalogue-migration.md#other-companies-names-2026-10-12)). Moog is
  RaffoSynth; the Mutable Instruments ports are Macro Osc (Braids), Macro Osc 2 (Plaits), Modal Voice (Elements),
  Resonator (Rings), Resonator FX, Meta Mod (Warps), Drum Map (Grids), Random Sampler (Marbles), with Émilie Gillet as
  their maker (Verglas keeps its name); the screens' backgrounds lost their brand text; Fizzik's, Noisemaker's and
  Aphex's option labels lost theirs. IDs, `.so` names and parameters unchanged.
  - [x] Repo (branch `branding`): builds PASSED, check_skin OK, screenshots, READMEs, catalogue entries (2026-10-12)
  - [x] Live II (2026-10-12): installed (one MPC restart); the browser shows the new names and the maker "Émilie Gillet"
    correctly (MPC writes it as `&#201;` in MPC.settings); the old screen folders are gone
  - [x] Live II (2026-10-12): an old project still finds them ("0001 Yeild": saved with [SYN] Moog, it opens with
    RaffoSynth and its sound; its Percolator, saved at a `/sdcard/vst/percolator.so` that no longer exists, loads from
    the release folder). MPC matches a saved plugin by its uid, not its name or path.
  - [x] GitHub (2026-10-13): seven repos renamed, all eight published and released (RaffoSynth 1.2.0, its zip played
    on the Live II; the others 1.1.0, offline checks)
  - [x] sd88me's review of [#404](https://github.com/sd88me/mpc-vst-plugins/pull/404) (merged 2026-10-13): the eight
    entries' new names and repos. His answer on OB-Xd, 303 and Rampage: OB-Xd stays (the open-source project's name),
    303 becomes Open303, Rampage gets a new name and says it's based on Befaco's Rampage VCV module
  - [x] Open303 and Dual Slope (2026-10-13, branch `rename-open303-dualslope`): 303 is [SYN] Open303; Rampage is
    [SEQ] Dual Slope by Andrew Belt (VCV's code), its screen without BARCELONA SYNTH LAB, its tab DUAL SLOPE, its MIDI
    port [SEQ] Dual Slope MIDI Out. uids, `.so` names, catalogue ids and parameters unchanged. Builds PASSED,
    check_skin OK, `check.sh` PASSED with "screen: same" (release tools `eaef502`)
  - [x] GitHub (2026-10-13): repos renamed (mpc-vst-open303, mpc-vst-dual-slope) and published; v1.1.0 drafts built by
    Actions, both zips OK in `catalog_check --catalog`, `.so` byte-identical to `check.sh`'s builds
  - [x] Live II (2026-10-13): both zips installed with one MPC restart (`install.sh -y -n` each); one plugin-list entry
    each under the new names, the `.so` files match the zips, the old screen folders and `/sdcard/vst/rampage.so`
    gone; the owner played both ("seem ok"). `tested.json` 1.1.0 for both
  - [x] Published both v1.1.0 releases (Latest), `tested.json` on both repos; his `catalog_build.py` on the two entries:
    both 1.1.0 tested, the 1.0.0 zips superseded, no issues to open (2026-10-13)
  - [ ] sd88me's review of [#417](https://github.com/sd88me/mpc-vst-plugins/pull/417) (the two entries, opened 2026-10-13)
- [x] **Moog's EMPHASIS and CUTOFF** (2026-10-12, PR #19): EMPHASIS hardly changed the sound (the
  ladder's feedback topped out at a quarter of what it needs to resonate: +2 dB at full) and CUTOFF fully open stopped
  at 2.3 kHz. Fixed under `MPC_PORT` (EMPHASIS up to 0.9 of self-oscillation, matched cutoff tuning); the 14 presets
  got new values that keep their sound (within about 1 dB offline). Offline test PASSED. The owner compared offline
  renders and wanted two sounds, so MAIN has a FILTER switch (parameter 41): CLEAN (the above) or DRIVE (soft-clipped
  ladder input, bass kept: fat and squelchy).
  - [x] Install on the Live II and listen (2026-10-12): installed (no restart, verified by hash); the owner played it
    and accepted it for now ("it'll do for now"). More squelch may come later (see the README's FILTER notes).
  - [x] Re-released `saustin2010/mpc-vst-moog` v1.1.0 (2026-10-12): check.sh PASSED and "screen: same" with the
    release tools (`7f0653c`), published, release built by Actions, catalog_check OK; the catalogue picks it up on its
    nightly read. The release zip itself wasn't installed on the device (the repo build was).
- [x] **MPC's STOP toggle of parameter 0** (2026-10-10, PR #16): on insert and on every STOP MPC's host (JUCE) sets
  parameter 0 to the far end and straight back; plugins with a preset there lost every edit on STOP (Hera confirmed
  by the owner). Fixed in the wrapper (held until the next block) in both tools (fork `7f0653c`); every host test now
  replays it (all 41 PASSED). Re-released v1.0.1: Fizzik, Hera, NuSaw, Braids, Groove Bank, Mr Hyde, Rings, Rings FX,
  Chordism, Mono Voice (Percolator in 1.2.0).
  - [ ] Install all on the Live II (`./install.sh <mpc> all`, no restart) and check STOP on Hera.
  - [x] Offered to sd88me: sd88me/mpc-vst-plugins#279 (2026-10-11, merged), the fix on his current main. It also applies a
    held parameter 0 when the host reads it (his step tests sweep an int parameter 0), and the check's text buffers
    follow `PARAM_TEXT_MAX` (his Maschine port shows 96-character text). His `poc/steptest` and `ports/maschine`
    PASSED in a Linux container.
- [ ] **One repo per plugin for sd88me's catalogue** (planned 2026-10-08): this repo stays the master, each released
  plugin is split into `saustin2010/mpc-vst-<plugin>` (`tools/publish.sh`). The 6 plugins already in the catalogue are
  offered as skins instead.
  - [x] Pilot 303 + Hera published (2026-10-08): `saustin2010/mpc-vst-303`, `mpc-vst-hera`, draft releases v1.0.0
    built by GitHub Actions on the release tools (sd88me's `main` + commits on `saustin2010/mpc-vst-plugins`
    `steve-features`), catalogue check OK, same screens.
  - [x] 26 more ready to publish (2026-10-08; `dev-tools/catalogue/prepare.py`, `check.sh`). Held: Hank, Tablor,
    Hush One, Libpo32 (licences), the Stevequencers.
  - [x] Device test of the pilot drafts (2026-10-08): Q-Links fine after QLINK_TRAVEL (see the doc).
  - [x] Released v1.0.0 and proposed to the catalogue (2026-10-08): sd88me/mpc-vst-plugins #230 (Hera), #231 (303).
  - [x] sd88me merged the pilot (2026-10-08). 20 more repos published; 19 released v1.0.0 and in the catalogue
    (sd88me/mpc-vst-plugins #236, merged 2026-10-09), checked offline only.
  - [x] The other 9 (2026-10-10): 8 repos created (MIDI Player, Pixel Walkers, Rampage, Super Arp, Eucalypso, Verglas,
    Hank, Tablor), Noisemaker's leaks fixed; all 9 released v1.0.0 and proposed (sd88me/mpc-vst-plugins #255).
  - [x] The Stevequencers (2026-10-10): release tools `7afa72e` (`"live"` params for the step light), their own screen
    (`deploy/`, byte-identical in the zips); both released v1.0.0 and added to #255 (now 11).
  - [ ] sd88me's merge of #255.
  - [x] Percolator (2026-10-10): `saustin2010/mpc-vst-percolator` released v1.0.0 to v1.2.0 (its own screen like the
    Stevequencers, since sd88me's builder sizes its readouts differently) and added to #255 (now 12). The Pērkons kit
    packs are not shipped (no licence to pass them on); its README links Erica's downloads.
  - [ ] Device test of the 31 release zips (all but 303 and Hera), then each repo's `tested.json`.
- [ ] Idea, not now (owner, 2026-10-08: "static is fine"): Hera's top wave display could show one still picture per preset,
  drawn from the engine (dev-tools/stitch/waveforms.py, switched with `when=preset:<i>/56`), instead of one for all.

## Presets and libraries ([docs/presets-and-libraries.md](docs/presets-and-libraries.md))

New parameters always go at the **end** of a plugin's `params.json`: MPC saves projects and Q-Link assignments by
parameter index, so existing projects keep working.

- [x] **Tablor factory presets** (2026-10-04). `factory.tbl` installs to `/sdcard/vst/tablor/presets/`; the engine maps
  its Move wavetable paths to `wavetables/`, and PRESET (appended params, MPC's PRESET menu, the VOICE page) applies a
  preset's state blob after resetting to defaults.
- [x] **OB-Xd banks** (2026-10-03). BANK selector under PATCH (`bank_index`/`bank_name`, appended); `.fxb` banks in
  `/sdcard/vst/obxd/presets/` are chosen there. `tools/obxd-lv2-to-fxb.py` converts OB-Xd 1.x LV2 banks.
- [x] **Noisemaker imports** (2026-10-03). `MODULE_DIR` set; each folder in `/sdcard/vst/noisemaker/presets/` is a
  bank, chosen with BANK under PATCH. PATCH reaches a bank's first 256 presets.
- [x] **Hush One imports** (2026-10-03). `MODULE_DIR` set; PATCH widened to 0-522 (11 built-in + 512); imported
  presets named by file (`MPC_PORT` patch). Check on the device: a project saved with the old PATCH range (0-10)
  may reopen showing a different PATCH number (MPC sets parameters back from normalized values).
- [x] **Libpo32 SAVE KIT** (2026-10-04): a SAVE KIT button on the KIT page (the engine's `save_kit`, which makes its
  own `presets/` folder); saved kits join KIT and the PRESET menu.
- [ ] INSTALL.md / plugin READMEs: document each of the above once it works (OB-Xd, Noisemaker, Hush One: done
  2026-10-03).

## New plugins

- [ ] **Incipience** ([originals/incipience](originals/incipience/), started 2026-10-13, branch `incipience`): an
  eleven-instrument drum synth laid out like the Jomox AlphaBase Mk II (kick, MBrane, two hats, clap, rim, crash, ride
  with the hardware's sample + metal noise + analog filter chain, X1 / X2 sample players, a four-operator FM synth,
  reverb and delay sends), with the Mk II's controls and 0-127 ranges (376 parameters, from `mpc/gen.py`). DSP written
  from the manual; it reads the AlphaBase's own sample set (`ABWV*.raw`, Jomox's free download) from its data folder.
  No sequencer (the owner: voices and control matter, not the sequencer).
  - [x] Phase 1 (2026-10-13): the engine offline, four built-in kits (no samples needed), WAV renders for the owner to
    judge by ear (`mpc/render.c`); native CPU about 3.7x Percolator's in the heaviest case (estimate: ~7 % on the Live II)
  - [ ] The owner listens to the renders (the owner had an AlphaBase): what's off, voice by voice
  - [x] Phase 2, offline (2026-10-13): the screen designed in Stitch with the AlphaBase Mk II's look (owner: "design on
    stitch but keep the alphabase vibe"; project "Incipience — MPC drum synth skin"): 13 tabs (11 instruments, FX,
    MIX), each with A / B Q-Link sub-pages laid out as the hardware's 4 x 4 encoder matrix, a row a Q-Link column.
    `tools/build.sh` PASSED, `check_skin.py` OK, screenshots in the README (`mpc/screen.sh` redoes it all)
  - [x] Installed on the Live II (2026-10-13, owner's go): registered (one MPC restart, MPC.settings backed up), the .so
    verified by hash, Jomox's sample set in `/sdcard/vst/incipience/` (the owner's `AlphaBaseWaves.zip`)
  - [x] The owner's first look on the Live II (2026-10-13): "looks and sounds great". Asked for, and done the same day:
    Q-Links on each instrument's pan / sends (a third sub-page, KICK MIX ...), FM VOICE its own Q-Link sub-page,
    SAMPLE stepping one at a time (a box with arrows; range 0-255), PITCH audible without a sample (it tunes the metal
    noise too), more kits (three that use the AlphaBase samples when they're there). Also: metal noise starts
    mid-cycle (it ran in step at first)
  - [x] procwatch (TESTS.md, 2026-10-13): no flags; one thread (the sample loader), asleep while idle; 1.2 KB of heap
    per insert (under the 4 KB flag, a loader thread's own; Tablor's is 14.8 KB). Idle cost was 0.83 of playing (the
    reverb and delay ran on empty): they sleep once nothing goes in and nothing is left; now 0.15 (`ab.sh`)
  - [x] Testers (owner, 2026-10-13: a PR to send out for testing; not to sd88me's catalogue yet). Superseded
    2026-10-19: the owner asked for it in sd88me's catalogue
  - [ ] On the Live II: design QA of the new sub-pages (Q-Link outlines on the right-hand panels), `bench.sh`
  - [x] MIDI, OUTs and modulation, offline (2026-10-17, branch `incipience-midi-mod`; owner: "play the voices
    chromatically like I can with the percolator ... output routing for each of the tracks ... all the controls should
    be able to be modulated"; chose MIDI lanes and a mod matrix both). As Percolator: a KEYS CH per instrument (every
    note on it plays the instrument pitched, C3 as tuned, pitch bend +-2; channels 2-12 to start with), Incipience MIDI
    In (its own ALSA port, so channels arrive), MIDI MOD lanes (mod wheel, Gen Purpose 1-3 per instrument, each moving
    any of its controls); an OUT per instrument (MAIN, OUT 1-11) and [FX] Incipience OUT
    ([incipienceout](originals/incipience/incipienceout/)), Percolator OUT made stereo and eleven wide. New: a mod matrix
    of 4 LFOs and 16 slots (SOURCE: an LFO, RANDOM or VELOCITY at each hit; DEST + CONTROL: any control of an
    instrument or of FX; DEPTH +-100 %). Three tabs: MOD, MIDI & OUT (NOTE moved here from MIX), MIDI MOD. 702
    parameters (the first 414 unchanged). The kits render bit-for-bit as before; `mpc/selftest.c` PASSED (natively, as
    32-bit ARM Linux code, and under ASan / UBSan in Linux); the matrix costs nothing measurable natively (all eleven
    busy: 0.48 s vs 0.46 s for 60 s of audio); `tools/build.sh` PASSED and `check_skin.py` OK for both; procwatch: no
    flags (Incipience idle/play 0.18, was 0.15; Incipience OUT 3 µs idle; procwatch now covers companion plugins).
    Framework fixes in `shadow_skin.py` (the patch): a pop-up's option list was on every Q-Link sub-page of its tab (the
    screen's TUI.json 8.2 -> 5.2 MB), and a stepper's arrows were cut from the tab's last sub-page's picture, not its
    own. The screen is 16 MB (Percolator's 15)
  - [x] Installed on the Live II (2026-10-18): both, Incipience OUT registered with one MPC restart, the .so files
    verified by hash
  - [x] On the Live II (owner, 2026-10-18: "other features look ok"): KEYS CH through Incipience MIDI In, OUT 1-11,
    the lanes and the matrix. Still to do: `bench.sh` with the matrix busy
  - [x] The owner's notes from the device, offline (2026-10-18, branch `incipience-midi-mod`): **RANDOMISE** per
    instrument, its sound not its mix ("a randomise button on each voice which sits outside of the mix"; gen.py's
    `RAND` ranges plus rules so a roll is always heard: 0 quiet in 20,000 rolls an instrument); **NOISE VOL** beside
    SAMPLE on page A ("the sample button doesn't do anything on CL Hat A": measured, the metal noise at its full level
    covers the sample, 3 dB apart across samples with it, 19 dB without); **your own samples** ("can we browse samples
    on the MPC or point to folders?"): a SAMPLES tab browsing the drives, a WAV file tapped loads into an instrument
    whose SAMPLE then steps through that folder, by name in the project (`src/library.c`: one thread reads, 64 MB kept,
    a playing file never freed); the project's state is now only what differs from the defaults. 730 parameters.
    `build.sh` PASSED, `check_skin.py` OK, the self-test PASSED natively and under ASan/UBSan and TSan. Installed on
    the Live II 2026-10-19 (no restart; the .so verified by hash)
  - [x] The **data wheel** (owner, 2026-10-18: "I cannot use the main mpc button to dial in parameters"): it works, on
    the control you've selected (owner, 2026-10-19: "the data wheel works as long as a box is selected"); nothing was
    selected. Since b014168 a click on SAMPLE moves one sample. (Whether a knob takes it once tapped: not asked yet)
  - [x] On the Live II (owner, 2026-10-19: "looks like all these things are working"): the SAMPLES tab, RANDOMISE,
    NOISE VOL on page A
  - [x] **1.0.0 released** (owner, 2026-10-19: "update in a PR and get this loaded up to 88 as a new instrument"):
    its own repo [mpc-vst-incipience](https://github.com/saustin2010/mpc-vst-incipience) (`prepare.py incipience`: the
    release workflow with Incipience OUT as a second job and zip, as Percolator OUT; FRAMEWORK.md; where to get the
    AlphaBase samples, which may now sit one folder down), v1.0.0 and incipience-out-v1.0.0 published after both zips
    were installed on the Live II with their own installer and checked by the owner ("looks good"; `tested.json`). The
    SAMPLES tab's first place is SAMPLE SET BY NUMBER (no maker's name on the screen). The owner's one note, "when
    dialing through the samples the numbers skip": with `QLINK_TRAVEL` the release tools' wrapper ignored `nudge_pct`,
    so a Q-Link tick moved SAMPLE 2 and a wheel click 2.5; fixed on the fork (`steve-features` b014168, the new pin),
    SAMPLE and SCROLL step one per tick (host test: six ticks step six). PR #31 (merged 2026-10-19, merge commit)
  - [x] **1.1.0: the sample browser** (owner, 2026-10-19: "i would like to improve the sample browsers as it's a bit
    clunky"; chose a cursor with preview, LOAD TO as buttons, shorter names, and AUDITION as a switch): a cursor (FILE:
    the data wheel on its box, Q-Link 1, its arrows; SCROLL a page) whose row is lit and whose file plays straight from
    the folder as it lands (PREVIEW); a tap on a file moves the cursor there and a second tap or LOAD loads it; LOAD TO
    as eight buttons that take the list to that instrument's folder; names without `.wav`, long ones start..end;
    AUDITION: turning SAMPLE on an instrument's page plays it. Also fixed: the `>` mark now finds the loaded file by name
    (it marked the wrong one after a file was added to the folder). 735 parameters (the first 730 as in 1.0.0). Offline:
    self-test PASSED (native, ASan/UBSan, TSan), `tools/build.sh` PASSED (three runs; the drives are listed at create, so
    the host test's restore check no longer catches the list mid-read), `check_skin.py` OK, `check.sh incipience
    incipienceout` PASSED, procwatch no flags (idle/play 0.17). To check on the Live II: the lit row (the row's value
    reads 1; MPC turns it off on a tap, taken as the second tap unless within 100 ms of the first), the wheel on FILE,
    the preview's timing on the SSD; then release 1.1.0
  - [x] **1.1.0: the wheel alone** (owner, 2026-10-20: "rather than touchscreen can we just use the wheel to scroll
    through folders and sounds?"): a folder lists its subfolders' WAV files too, depth first, each folder's under a
    heading (folders without any left out; past 999 rows or 256 folders looked in, folder by folder as before), FILE
    steps over the headings from one folder into the next and FOLDER shows the cursor's file's folder; a heading tapped
    opens that folder; LOAD TO only moves the list when its folder isn't listed; AUTO LOAD (new switch, Q-Link 6): the
    file FILE lands on loads into LOAD TO's instrument. 736 parameters. Offline: self-test PASSED (native, ASan/UBSan,
    TSan), `tools/build.sh` PASSED, `check_skin.py` OK. Found on the way (UBSan): the sine table's lookup read one past
    its end when a phase a hair under 0 wrapped to 1 (fixed). Then (owner, 2026-10-20, on the 1.1.0 drafts: "the main
    wheel does not scroll through the files in the browser. i need to use the rows button to look", "make the font
    white and not red"): the list's rows had no `Q-Link` touch action, so a tap on a row left the wheel elsewhere, and
    the cursor's border was near-black on black. The skin's `list` takes `wheel=br_cursor` (a row's touch hands the
    wheel to FILE; `framework/mpc-vst-plugins.patch`), `ink=ffffff`, `lit=d32a2a`. How it all works, for other
    plugins: [docs/sample-browser.md](docs/sample-browser.md).
    On the Live II (owner, 2026-10-20): white text confirmed; a tap plays; Q-Links move the cursor and PREVIEW plays,
    but "the qlinks change really really quickly and skip a few rows" and "using the big mpc job wheel doesn't do
    anything" (the rows' `wheel=` didn't take). Akai's own skins on the device (`/usr/share/Akai/Content/Synths`, read
    in a scratch folder, not kept) only ever act on a control's own `Data` handle (`Mouse Down` → `Q-Link`, `Enter
    Pressed` → `Toggle Switch`), so a row can't hand the wheel to another parameter. Now: `wheel=` gone, the list has
    `focus=no` (no row takes the wheel), FILE's box is the wheel's ("FILE: TAP, THEN THE WHEEL"); Q-Link 1 is BROWSE
    (new, 737 params, `qlink_only`), the cursor a row per three Q-Link events, SCROLL a page per three (an arrow a page)
    (`QL_TICKS`). Checked on the Live II with the rebuilt drafts (owner, 2026-10-20: "this is working"): the wheel on
    FILE's box, a row tap leaving it there, BROWSE and SCROLL at three events a step. Left for now (owner: "can leave
    it for now"). **Released 2026-10-20** (owner: "publish"): v1.1.0 (Latest) and incipience-out-v1.1.0, tags at the
    commit they were built from (1687b5c), the zips' `.so` files byte-identical to the ones checked on the device;
    `tested.json` 1.1.0 (MPC Live II, 3.9.1). How it works, for other plugins: [docs/sample-browser.md](docs/sample-browser.md)
  - [ ] **In sd88me's catalogue**: entries `incipience` and `incipience-out` proposed in
    [sd88me/mpc-vst-plugins#480](https://github.com/sd88me/mpc-vst-plugins/pull/480) (2026-10-19; `catalog_build` on
    both: 0 problems), waiting for his merge
  - [ ] The next release: a `bench.sh` figure on the Live II (`bench.txt`, the catalogue's "cpu-bench"), with the matrix
    busy; KIT still moves 4 kits a Q-Link tick (parameter 0: the wrapper holds its set to the next block, so the host
    test's `nudge_pct` check can't run on it); this repo's own build (`tools/build.sh`, the older wrapper) has no
    `nudge_pct`, so an install with `./install.sh` still skips samples on a Q-Link
  - [ ] Phase 3: decode the AlphaBase kit files (`ABPR*.BIN`: 128 x 496-byte records; analog values stored x32 as
    12-bit DAC words, e.g. AlphaOne's kick Tune 86 / Pitch 28 / Decay 84 / Harm 0; most fields not mapped yet) so its
    63 factory kits load; the AlphaBase's CC chart on channels 1-11 (needs its MIDI implementation)

- [ ] **Percolator** ([originals/percolator](originals/percolator/), 2026-10-10): a four-voice drum synth laid out like
  the Erica Synths Pērkons HD-01, DSP written from scratch, reads the Pērkons' `.KIT` packs. Seven pages (a voice
  each, MASTER, two of LFO depths), a colour per voice, envelope displays that follow DECAY / PITCH / ATTACK. Checked on the
  Live II 2026-10-10 (all five pages, Q-Links, the displays following their knobs, the packs): Design QA ✅.
  - [ ] 2.0.0 (owner's requests, 2026-10-12; branch `percolator-changes`, installed on the Live II for testing): a MIDI
    & OUT page with each voice's NOTE (36-39 to start with; were fixed at 20-23), CHANNEL, KEYS and OUT (MAIN, OUT
    1-4: a second Percolator on another track plays an OUT, since MPC plays only a plugin's first stereo pair); COMP
    SEND per voice, the compressor bypassed at AMOUNT 0 and only ever louder above it, COMP LEVEL; a MIX panel per
    voice. On the Live II (2026-10-12) the owner's checks passed (notes, an OUT on an audio track, the compressor, the
    timing) except a MIDI channel per voice: MPC hands a plugin's track every note-on on channel 1 (traced). Zones
    instead were turned down; Percolator now opens its own MIDI input port, Percolator MIDI In, which a MIDI track
    sends to on its channel: working on the Live II (2026-10-12). The Pērkons' CCs 70-113 (each voice's knobs and
    switches; owner: "as close to the real machine as possible"): DECAY works on the Live II. MIDI MOD lanes per
    voice (mod wheel, Gen Purpose 1-3, each moving a control picked by name): working on the Live II (2026-10-12).
    Release (2026-10-12): Percolator OUT ships in Percolator's release (owner); both 2.0.0 drafts built by Actions on
    tools `eaef502` (steve-features + mpc_engine_changed), both zips OK in `catalog_check --catalog`, screens identical
    to `deploy/`. Both zips installed on the Live II and checked by the owner: "all working"; v2.0.0 published with
    `tested.json` (2026-10-12). The catalogue entries (asset_pattern, percolator-out, author): sd88me/mpc-vst-plugins#398,
    merged 2026-10-12, then percolator-out-v2.0.0 published (not Latest: v2.0.0 stays the repo's Latest); his
    `catalog_build.py` on both entries: 0 problems, each 2.0.0 its own zip and tested. eaef502 (`mpc_engine_changed`)
    merged upstream as sd88me/mpc-vst-plugins#399 (2026-10-12, with a test engine and docs).
    Left: whether MPC's own automation on Percolator's track lists its parameters by name.
  - [x] 2.0.1 (2026-10-13): kit packs one folder down are found too (Windows' "Extract All" layout; users had
    trouble installing the zips). Offline: correct installs list exactly what 2.0.0 lists, the Windows layout all 169
    kits; build, host test, ASan/UBSan and `check.sh` PASSED. Drafts v2.0.1 and percolator-out-v2.0.1 built by Actions (2026-10-13, from
    050603a), both OK in `catalog_check --catalog`; Percolator OUT's `.so` is byte-identical to 2.0.0's (the one
    checked on the Live II). On the Live II: the zip installed (one restart), Pack 3 in the Windows layout listed
    through Pack 3 62 (owner: "fine"); both published 2026-10-13 (v2.0.1 Latest), `tested.json` 2.0.1.
  - [ ] 2.1.0 (owner, 2026-10-19: Incipience's sample browser for Percolator, samples "allowed on each voice", and "a
    random button to randomise each parameter per voice"; branch `percolator-samples`, not pushed): each voice's SOURCE
    (SYNTH, or SAMPLE: its own WAV file played as voice 4's SAMPLE plays its slots; the owner chose a switch over
    layering) and SAMPLE (stepping through the file's folder), a SAMPLES page browsing the data folder and the MPC's
    drives (`src/library.c`, as Incipience's), RANDOMISE per voice (sound controls only, the mix and MIDI kept). 37
    parameters appended (176), the SAMPLES page last. Offline: the 177 kits render bit-identical to 2.0.1;
    `mpc/selftest.c` PASSED natively, as armv7, under ASan/UBSan and TSan (every RANDOMISE roll heard: 80,000 over -36
    dBFS); build and host test PASSED, `check_skin.py` OK, procwatch as 2.0.1 (the new thread sleeps). Also fixed: a
    sine-table read one past its end (UBSan, FM; in 2.0.1 too). Then (owner, "fix this first"): every voice stops
    once its hit has died away (envelope 80 dB under its peak, output under -100 dB for 2048 frames); up to 2.0.1 only
    SNARE did, so silence after playing cost 1.7x a fresh instance's. A stopped voice's filter follows CUTOFF, and a
    sample's end lets the filter and DRIVE ring out (2.0.1 cut them: a click, up to -25 dBFS in Hrtl 60). Offline:
    procwatch `after/fresh` 1.69 -> 1.00; the kits as 2.0.1 to within 2 LSB a hit and 6 in the groove (every hit from
    the same noise state), Hrtl 54 and 60 but for that click; the selftest's 288 hits all stop. Installed on the Live
    II 2026-10-19 (this repo's build in the 2.0.1 release folder, 2.0.1 backed up in `/sdcard/vst-backups/`; no
    restart). 2026-10-20 (owner: "the same browser" as Incipience 1.1.0's; the voice pages' SOURCE box "doesn't look
    nice"), offline: the SAMPLES page is Incipience 1.1.0's browser (one list of a folder and its subfolders, FILE on
    the data wheel, BROWSE on Q-Link 1 a file per three events, PREVIEW from the drive, a second tap / LOAD / AUTO LOAD,
    AUDITION; 7 parameters appended, 183; the framework patch's list `ink=`, `lit=`, `focus=no` and `check_skin.py`
    taken from `incipience-midi-mod`); SOURCE moved to the voice pages' title bar (its two buttons had drawn apart:
    the skin builder keeps one picture per option of a parameter, the SAMPLES page's smaller ones in the voice page's
    place) and SAMPLE level with CUTOFF and DRIVE; SAMPLE and FILE a file a click with this repo's wrapper too (the
    engine steps small moves, as `nudge_pct` does); amber stepper arrows. `mpc/selftest.c` PASSED natively, as armv7, under ASan/UBSan and TSan; procwatch as before, build and
    host test PASSED, `check_skin.py` OK. Installed on the Live II 2026-10-20 (same way, no restart; the 2026-10-19
    build backed up in `/sdcard/vst-backups/`). The owner checked it there the same day: "this is working well".
    Left: a release (its `release.yml` pin to `b014168`
    or later for `nudge_pct`, `prepare.py --ready`).
  - [ ] Later (procwatch, 2026-10-12): `ab.sh` flags Percolator ALWAYS ON (idle/play 0.6): the BBD delay runs every
    sample while silent, as in 1.2.0 (native idle 0.95 us/block then, 1.09 now). A sleep once the delay has rung out
    would save most of it. (The voices that never stopped after their first hit, found 2026-10-19: fixed in 2.1.0.)
- [ ] **Percolator OUT** ([originals/percolator/percolatorout](originals/percolator/percolatorout/), 2026-10-12): an insert effect that plays
  one of Percolator's OUT 1-4 on its own track (owner: an audio track instead of a second plugin). Offline tests
  passed; installed and working on an audio track on the Live II (2026-10-12). Left: a release with Percolator 2.0.0
  (its own repo, or Percolator's, as TR-MPC ships its Tap FX).
- [ ] **Stevequencer** ([originals/stevequencer](originals/stevequencer/)): a 16-step, four-page (64-step) melodic
  sequencer edited from the Q-Links (pitch, length, on/off, velocity, chance, ratchet per step). Browser prototype
  2026-10-04 (`design/prototype.html`); built and installed on the Live II the same day (engine, 418 parameters, 6
  presets, the skin with per-sub-page controls and a step light; framework: `"live"` parameters, `banks=`); the owner
  tried it: working (2026-10-04). Still to look at on the device: the step light's cost on MPC's screen thread and that it isn't recorded as automation, how MPC switches
  six Q-Link sub-pages per tab, a project saving and reopening its pattern; then Design QA. Grid order is rows (steps
  1-4 across the top) as in the prototype; beats or pads order is a one-line change in `mpc/gen.py`.
  MOD lanes (2026-10-04, offline): MOD A / MOD B, a CC value per step ("-" = none) sent just before the step's note,
  HOLD or RETURN to a base value; two more Q-Link sub-pages per step page and a MOD tab; 134 parameters appended (552);
  state SQ2 (SQ1 still loads); `mpc/mod_test.c` passes. On the device: that the CCs reach the instrument (below).
  Presets: 20 now (14 seeded random patterns added 2026-10-04, six of the 20 using the MOD lanes).
- [ ] **Mutable Vibe and MPC Plaits** ([mpc-ports/](mpc-ports/)): two instruments other authors wrote for MPC OS
  (nachtaktiv303's Rings + Plaits instrument; poloq's polyphonic Plaits), built here from their source with this
  repo's framework (2026-10-05). Offline: built, test PASSED, Q-Links one column per panel on every page (checked with
  check_skin.py and the column outlines), names fixed where MPC would show AMOUNT four times. Framework gained what
  they use from newer upstream and poloq's fork: `dynamic_name` / `dynamic_display` (names and value text from the
  engine) and `HAS_TRANSPORT`. Installed; the owner: "seems to be working nicely" (2026-10-05). Presets: Mutable Vibe 24 (with a new VOLUME
  control to level them), MPC Plaits 31, levels evened out offline (`dev-tools/presets/levels.sh`). Still to do: Design
  QA on the Live II (per-model names on Mutable Vibe, synced LFOs restarting on MPC Plaits, how the presets sound).
- [x] MIDI CC 20-35 move every plugin's first-page Q-Links (wrapper, 2026-10-04). On the Live II (2026-10-05): a
  sequencer's CCs on the instrument track's MIDI input move its controls; the owner: "the automation lanes are working".
- [ ] **Stevequencer 16** ([originals/stevequencer16](originals/stevequencer16/), 2026-10-05, offline): 16 steps and eight
  modulation lanes, each to a CC or to any parameter of the instrument (PARAM = NRPN, which every plugin here now takes
  as its parameter n, any page: `docs/parameter-numbers.md`); per lane HOLD, RETURN, SLIDE (across empty steps), LFO
  (six shapes, a cycle of 1-64 steps) or FOLLOW (modulation groups: lanes that follow a leader, each with its own
  target and range), and its own RATE and LENGTH. A separate plugin, so projects using Stevequencer keep working. All
  40 plugins rebuilt for NRPN; all installed on the Live II (2026-10-05). On the device: that MPC passes NRPN (CC 99/98/6) through, eight sub-pages on MOD and
  LANES, the load with several sliding lanes; then Design QA.
  Rampage sends a CC almost every block on a fast LFO (~290 a second per output): the wrapper only tells MPC ~40 times a
  second, but a rate limit in Rampage itself would be kinder.

## Background work and idle CPU ([TESTS.md](TESTS.md))

- [ ] The ten repairs found in the simulator (2026-10-09), in TESTS.md's order: 303 never sleeps after its first note,
  Helm sets `HOME` for all of MPC, a "sleep when silent" switch in the wrapper for 14 plugins, Mono Voice, Rampage,
  the Noisemaker and Helm leaks, Marbles, Tablor's loader cores, one transport read per block. Each one is checked
  offline with `dev-tools/procwatch/`, then on the MPC with `dev-tools/procwatch/device.sh`. TESTS.md's status table
  tracks them.

## On a device (nothing below has been tried on hardware yet)

- [x] **Full-height screens** (2026-10-18, branch `skins-fill-height`): a user noticed the Schwung ports' screens didn't
  use the whole plugin area. The Stitch conversion centred each design at the height it had under its own header and
  footer, ~110 px empty above and below on most. `dev-tools/stitch` now fills the 628 px (`fill`, its README); 30 screens
  rebuilt (fixed-height rows grown per design, plan pages that stopped halfway down spread, Moog's and Noisemaker's
  hand edits carried over). Builds PASSED, check_skin OK, screenshots refreshed; `.so` files unchanged. 2026-10-19:
  Noisemaker's screen lost its "TAL" text (just NOISEMAKER).
  - [x] Installed on the Live II (2026-10-19), all 30, no restart: the 26 the repo installs with `install.sh` (files
    hash-checked); Hera, Open303, RaffoSynth and Dual Slope run from release installs (`.so` inside their
    `/sdcard/Synths` folder, which `install.sh` would replace), so only their `Plugin Skins` was swapped in (staged,
    hash-checked); their released `.so` stay. `check.sh`: screen same for those four
  - [x] The owner looked at them on the Live II (2026-10-19): "these all look good"
  - [x] Released (2026-10-19): the 24 in sd88me's catalogue published and released (1.1.0 / 1.2.0 / RaffoSynth 1.3.0),
    every zip OK in `catalog_check --catalog` ([docs/catalogue-migration.md](docs/catalogue-migration.md#full-height-screens-2026-10-19))
  - [ ] sd88me's review of [#479](https://github.com/sd88me/mpc-vst-plugins/pull/479): the 24 entries' screenshots
    at the new tags
  - [ ] Not released: Hush One (its layout lacks the four display lines of 2026-10-08, screen DIFFERENT under the
    release tools); the release tools' offline test fails for Helm, LibPO32, MazeLite, Plaits and Marbles (older than
    this work)

- [ ] MPC's PRESET menu (2026-10-03): every instrument with presets now reports them as VST programs (16 + Moog). Names
  show and load on the Live II (checked 2026-10-03). Still to check: that OB-Xd / Noisemaker show the new bank's presets
  after a BANK switch (the list is read live). Not included: Plaits' FM patches (they belong to its 6-op model), the
  sequencers. Added 2026-10-04, to check on the device: Tablor's 9 and Mono Voice's 12 factory sets (both keep the chosen
  one in their state), and presets made for Rings, Plaits (with a new VOLUME), Mr Hyde, Rings FX, Verglas and Warps.
- [ ] Install everything on an MPC and re-insert each plugin; check every page of the new Stitch screens (names,
  values, Q-Link columns, touch areas, pop-ups, envelope and waveform displays). Track it in README.md's Design QA column.
  Design QA first pass done offline for all 36 (2026-10-04, branch design-qa-batch1): one Q-Link column per panel, each
  plugin's `DESIGN-QA.md` says what changed and what to look at. **Passed on the device (2026-10-04): 27 of 36**, every
  synth, drum machine and audio effect. Left: the 9 sequencers and generators.
- [x] Sequencers on a **stock** MPC (2026-10-04, Live II): a sequencer's own MIDI port plays another track, as on a Force.
  The two settings people miss: the target track's Monitor on In (not Auto), and Remote off for the port
  ([docs/sequencers.md](docs/sequencers.md)). Each of the nine is still to be heard in its own design QA.
- [x] Audio effects (Verglas, Warps, Rings FX) (2026-10-04, Live II): in a track's insert effect slots, under VST, and
  they work there (their screens passed the design QA in the slot).
- [x] Plugin browser groups (2026-10-04). Sorted **by type**, MPC puts every VST in one VST folder: the plugin-list
  `category` changes nothing, not even Akai's own folder names ("Drum", "Delay/Reverb", "Modulation", "Harmonic": tested
  on the Live II). So every name now starts with its kind: **[SYN]**, **[DRUM]**, **[SEQ]**, **[FX]** (MPC sorts the
  list case-sensitively, so they group after other makers' VSTs). Renamed before any project used them (no project on
  the device referenced one). `install.sh` removes a renamed plugin's old screen folder when it re-registers it.
- [ ] `install.sh` / `uninstall.sh` on a real MPC (so far tested only against a simulated one:
  `dev-tools/fake-mpc/`).
- [ ] CPU: run the framework's `tools/bench.sh` for the heavy ones (Helm, Chordism, Tablor, Mono Voice, Elements,
  Verglas).

## Repo

- [ ] sd88me's wrapper fix (#499, 2026-10-23): all 44 rebuilt and PASSED on `steve-features` + #486-#489 (`e3a0af4`,
      not pushed), `refresh_display` where a set changes other controls (dev-tools/displaydeps), 39 screens the same and
      5 kept (framework/README.md). Left: the device check (Q-Links on track edit -> automation; a preset change moves
      the other knobs; labels follow), then push the branch, `prepare.py --ready`, releases, one catalogue PR, a reply
      on #499. Plan: the owner's doc "Wrapper Fix #499: Rebuild Plan".
- [x] Plaits crashed under an offline stress of random parameter writes and MIDI (2026-10-23, in the build installed
      then too, also on emulated armv7): the wavetable model's coordinates reached 8 (or bank -1 after the mirror) and
      read a garbage wave pointer; the speech model read past its phoneme table. Fixed in Plaits, Mutable Vibe and Mr
      Hyde with the fixes MPC Plaits already had (its `src/VENDORED.md`): each plugin's README and
      `upstream-changes.diff`. Ten seeds each under ASan/UBSan clean (private stress script). Left as is: `wave_map_`
      is allocated for one bank and filled for four, inside the engines' shared buffer (harmless once indices stay in range).
- [ ] Choose a licence for this repo's own scripts and documents (the plugins keep theirs).
- [ ] Hush One and Libpo32 state no licence upstream: ask their authors, or leave them out of releases.
- [ ] GitHub releases: a zip per plugin (its `deploy/` + an on-device install script) for people without git.
- [ ] Chiptune (Schwung GB + NES chips) isn't included: it doesn't link (two `Blip_Buffer` classes).
- [ ] Offer the ports and the framework patch to sd88me for his repo.
