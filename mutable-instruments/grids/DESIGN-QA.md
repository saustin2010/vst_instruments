# Design QA: Grids

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right; 3-2-4-3 style groups).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| GRIDS | 1 MAP X, MAP Y, CHAOS, BD FILL  ·  2 SD FILL, HH FILL, MODE, SWING  ·  3 BD LEN, SD LEN, HH LEN | 1 MAP X, MAP Y, CHAOS  ·  2 BD FILL, SD FILL, HH FILL  ·  3 MODE, SWING  ·  4 BD LEN, SD LEN, HH LEN |
| NOTES | 1 BD NOTE, SD NOTE, HH NOTE, ACCENT VEL  ·  2 NORMAL VEL, CHANNEL, RESOLUTION | 1 BD NOTE, SD NOTE, HH NOTE  ·  2 ACCENT VEL, NORMAL VEL  ·  3 CHANNEL, RESOLUTION |

- **GRIDS**: MAP | DENSITY | ENGINE MODE | PATTERN LENGTHS, a column each (3-3-2-3); the fills had been split across
  two columns, and the second's outline took in the ENGINE panel. The rhythm display moved to the left of the
  lower row, PATTERN LENGTHS to the right.
- **NOTES**: DRUM NOTE MAP | VELOCITY | MIDI PORT, a column each (3-2-2); the first column had taken in an
  accent knob from the next panel.

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).
