# Roadmap

Open items as of 2026-10-03, most useful first. Tick them off here (with the date) as they're done.

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
- [ ] **Libpo32 SAVE KIT** button (`save_kit`), and create its `presets/` folder on install for it to write to.
- [ ] INSTALL.md / plugin READMEs: document each of the above once it works (OB-Xd, Noisemaker, Hush One: done
  2026-10-03).

## On a device (nothing below has been tried on hardware yet)

- [ ] MPC's PRESET menu (2026-10-03): every instrument with presets now reports them as VST programs (16 + Moog). Names
  show and load on the Live II (checked 2026-10-03). Still to check: that OB-Xd / Noisemaker show the new bank's presets
  after a BANK switch (the list is read live). Not included: Mono Voice (its patches load into a track; nothing reports the current one),
  Plaits' FM patches (they belong to its 6-op model), Tablor (its presets aren't reachable yet), the sequencers.
- [ ] Install everything on an MPC and re-insert each plugin; check every page of the new Stitch screens (names,
  values, Q-Link columns, touch areas, pop-ups, envelope and waveform displays). Track it in README.md's Design QA column.
- [ ] Sequencers: route each one to another track through its own MIDI port on a **stock** MPC (works on a Force).
- [ ] Audio effects (Verglas, Warps, Rings FX): does MPC offer third-party VST effects in its insert list at all?
- [ ] Plugin browser groups. Checked on the Live II 2026-10-03: MPC's plugin menu sorted **by type** shows only VST
  Instruments / VST Effects (the plugin-list `category` is ignored; the sequencers and drum machines keep reporting
  theirs); sorted **by manufacturer** it makes a folder per maker field (Grids as "Sequencers" got its own folder).
  The owner sorts by type, so that was put back. Left for later: name prefixes (SEQ / DRM) to bunch them in the
  by-type list, after checking that an older project still finds a renamed plugin.
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
