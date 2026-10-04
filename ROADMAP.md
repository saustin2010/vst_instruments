# Roadmap

Open items as of 2026-10-04, most useful first. Tick them off here (with the date) as they're done.

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
- [ ] Sequencers: route each one to another track through its own MIDI port on a **stock** MPC (works on a Force).
- [ ] Audio effects (Verglas, Warps, Rings FX): MPC lists them in a track's insert effect slots, under VST (2026-10-04,
  Live II). Still to check: that one processes audio in the slot.
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
