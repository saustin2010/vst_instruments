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
  - [ ] Rename the seven repos, publish, release; tell sd88me (and ask about OB-Xd, 303, Rampage)
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
  - [ ] Later (procwatch, 2026-10-12): `ab.sh` flags Percolator ALWAYS ON (idle/play 0.6): the BBD delay runs every
    sample while silent, as in 1.2.0 (native idle 0.95 us/block then, 1.09 now). A sleep once the delay has rung out
    would save most of it.
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

- [ ] Choose a licence for this repo's own scripts and documents (the plugins keep theirs).
- [ ] Hush One and Libpo32 state no licence upstream: ask their authors, or leave them out of releases.
- [ ] GitHub releases: a zip per plugin (its `deploy/` + an on-device install script) for people without git.
- [ ] Chiptune (Schwung GB + NES chips) isn't included: it doesn't link (two `Blip_Buffer` classes).
- [ ] Offer the ports and the framework patch to sd88me for his repo.
