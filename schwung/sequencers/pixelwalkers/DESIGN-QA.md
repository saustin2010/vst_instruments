# Design QA: PixelWalkers

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; utility buttons in their own box and off the Q-Links).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 BIRTH NOTE, BIRTH LEVEL, HIT LEVEL, HIT DECAY  ·  2 BOUNCE, HARDNESS, TOMBOLA, RANDOMIZE  ·  3 KILL ALL | 1 BIRTH NOTE, BIRTH LEVEL, HIT LEVEL, HIT DECAY  ·  2 BOUNCE, HARDNESS, TOMBOLA |

- RANDOMIZE and KILL ALL were on the Q-Links (column 2 and 3), so turning a knob fired them. They're touch only now,
  in their ACTIONS box, as on Aphex.
- WALKERS | WORLD DYNAMICS, a column each; the WALKERS row moved in from the edges so its outline stays inside the
  panel (it reached into MIDI OUT).

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
