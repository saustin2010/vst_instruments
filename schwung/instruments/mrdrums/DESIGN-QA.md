# Design QA: Mr Drums

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| KIT | 1 KIT, MASTER VOL, POLYPHONY, VEL CURVE  ·  2 HUMANIZE, AUTO SELECT, EDIT PAD, RAND LOOP | 1 MASTER VOL, POLYPHONY, VEL CURVE, HUMANIZE  ·  2 EDIT PAD, AUTO SELECT  ·  3 RAND LOOP  ·  4 KIT |
| PAD | 1 EDIT PAD, PAD VOLUME, PAD PAN, PAD TUNE  ·  2 PAD START, PAD MODE, CHOKE GROUP, PAD ATTACK  ·  3 PAD DECAY, RAND VOLUME, RAND PAN, RAND DECAY  ·  4 CHANCE | 1 EDIT PAD  ·  2 PAD VOLUME, PAD PAN, PAD TUNE, PAD START  ·  3 PAD MODE, CHOKE GROUP, PAD ATTACK, PAD DECAY  ·  4 RAND VOLUME, RAND PAN, RAND DECAY, CHANCE |

- **KIT**: MASTER ENGINE | 16-PAD MATRIX (EDIT PAD, AUTO SELECT) | RAND LOOP | KIT (the kit last, as a preset), a
  column each. AUTO SELECT moved from the panel header's corner to just above EDIT PAD, so its outline stays inside
  the panel.
- **PAD**: PAD (EDIT PAD) | SOUND (VOLUME, PAN, TUNE, START) / PLAYBACK (PAD MODE, CHOKE GROUP, ATTACK, DECAY) |
  RANDOM (VOLUME, PAN, DECAY, CHANCE). SOUND had six controls, so PAD MODE and CHOKE GROUP joined the envelope in a
  PLAYBACK panel (how a hit plays and ends).

KIT is made in the design (convert.py MAPS `augment`, with the new `move` op, and `qlinks`); PAD in the port's page
plan (`layout.grid.conf`). A rerun keeps both.

## Presets
Its kits are in MPC's PRESET menu (unchanged).

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).
