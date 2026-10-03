# Design QA: Marbles

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MARBLES | 1 T MODEL, CLOCK DIV, T BIAS, JITTER  ·  2 GATE LEN, DEJA VU, LENGTH, T DEJA VU  ·  3 SCALE, X DEJA VU, SPREAD, X BIAS  ·  4 STEPS, CHANNELS | 1 T MODEL, CLOCK DIV, T BIAS, JITTER  ·  2 DEJA VU, LENGTH, T DEJA VU, X DEJA VU  ·  3 SCALE, SPREAD, X BIAS, STEPS  ·  4 CHANNELS, GATE LEN |
| SETUP | 1 RATE BASE, T RANGE, GATE RAND, X RANGE  ·  2 X MODE, BASE NOTE, VELOCITY | 1 RATE BASE, T RANGE, GATE RAND  ·  2 X RANGE, X MODE  ·  3 BASE NOTE, VELOCITY |

- **MARBLES**: T RHYTHM and X PITCH had five Q-Link controls each, so every column ran into the next panel. Now
  T RHYTHM | DEJA VU | X PITCH | MIDI OUT, a column each:
  - X DEJA VU moved from X PITCH's title bar into the DEJA VU panel, beside T DEJA VU (where Marbles has it).
  - GATE LEN moved from T RHYTHM into MIDI OUT, beside CHANNELS (it sets the length of the notes sent).
  - The T and X displays moved to the left of their panels, the knobs to the right and up, so their values sit
    inside the panel.
  - The T1 > X1 / T2 > X2 / T3 > X3 routing switches stay touch only, as before.
- **SETUP**: CLOCK | X | MIDI, a column each; the empty OUTPUTS panel (its switches are on MARBLES) went.

Made in the design (convert.py MAPS `augment`, with the new `move` op, and `qlinks`) and the page plan
(`layout.grid.conf`), so a rerun keeps it.

## Checked
Offline test PASSED; check_skin OK (three small touch overlaps < 12 px); no Q-Link outlines overlap on either page
(qlink_overlay.py). The outlines of the two title-bar pop-ups (T MODEL / CLOCK DIV, SCALE) reach a little above their
panels: MPC leaves room above a pop-up.
