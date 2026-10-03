# Design QA: Eucalypso

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel or group, as Moog; outlines that frame
one panel; displays on the left, controls on the right).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 SYNC, RATE, BPM, SWING  ·  2 PLAY MODE, RETRIGGER, VOICES, RAND CYCLE  ·  3 VELOCITY, VEL RANDOM, GATE, GATE RANDOM | 1 SYNC, RATE, BPM, SWING  ·  2 PLAY MODE, RETRIGGER, VOICES, RAND CYCLE  ·  3 VELOCITY, VEL RANDOM, GATE, GATE RANDOM (unchanged) |
| LANE 1 | 1 L1 ON, L1 STEPS, L1 PULSES, L1 ROTATE  ·  2 L1 DROP, L1 DROP SEED, L1 VELOCITY, L1 GATE  ·  3 L1 NOTE, L1 NOTE RND, L1 NOTE SEED, L1 OCTAVE  ·  4 L1 OCT RND, L1 OCT SEED, L1 OCT RANGE | 1 L1 ON, L1 STEPS, L1 PULSES, L1 ROTATE  ·  2 L1 DROP, L1 DROP SEED, L1 VELOCITY, L1 GATE  ·  3 L1 NOTE, L1 NOTE RND, L1 NOTE SEED  ·  4 L1 OCTAVE, L1 OCT RND, L1 OCT SEED, L1 OCT RANGE |
| LANE 2 | 1 L2 ON, L2 STEPS, L2 PULSES, L2 ROTATE  ·  2 L2 DROP, L2 DROP SEED, L2 VELOCITY, L2 GATE  ·  3 L2 NOTE, L2 NOTE RND, L2 NOTE SEED, L2 OCTAVE  ·  4 L2 OCT RND, L2 OCT SEED, L2 OCT RANGE | 1 L2 ON, L2 STEPS, L2 PULSES, L2 ROTATE  ·  2 L2 DROP, L2 DROP SEED, L2 VELOCITY, L2 GATE  ·  3 L2 NOTE, L2 NOTE RND, L2 NOTE SEED  ·  4 L2 OCTAVE, L2 OCT RND, L2 OCT SEED, L2 OCT RANGE |
| LANE 3 | 1 L3 ON, L3 STEPS, L3 PULSES, L3 ROTATE  ·  2 L3 DROP, L3 DROP SEED, L3 VELOCITY, L3 GATE  ·  3 L3 NOTE, L3 NOTE RND, L3 NOTE SEED, L3 OCTAVE  ·  4 L3 OCT RND, L3 OCT SEED, L3 OCT RANGE | 1 L3 ON, L3 STEPS, L3 PULSES, L3 ROTATE  ·  2 L3 DROP, L3 DROP SEED, L3 VELOCITY, L3 GATE  ·  3 L3 NOTE, L3 NOTE RND, L3 NOTE SEED  ·  4 L3 OCTAVE, L3 OCT RND, L3 OCT SEED, L3 OCT RANGE |
| LANE 4 | 1 L4 ON, L4 STEPS, L4 PULSES, L4 ROTATE  ·  2 L4 DROP, L4 DROP SEED, L4 VELOCITY, L4 GATE  ·  3 L4 NOTE, L4 NOTE RND, L4 NOTE SEED, L4 OCTAVE  ·  4 L4 OCT RND, L4 OCT SEED, L4 OCT RANGE | 1 L4 ON, L4 STEPS, L4 PULSES, L4 ROTATE  ·  2 L4 DROP, L4 DROP SEED, L4 VELOCITY, L4 GATE  ·  3 L4 NOTE, L4 NOTE RND, L4 NOTE SEED  ·  4 L4 OCTAVE, L4 OCT RND, L4 OCT SEED, L4 OCT RANGE |
| NOTES | 1 REGISTER, NOTE ORDER, MISSING NOTE, SCALE  ·  2 ROOT, SCALE RANGE, OCTAVE, ORDER SEED  ·  3 MISSING SEED, RANDOM SEED | 1 REGISTER, NOTE ORDER, MISSING NOTE  ·  2 SCALE, ROOT, SCALE RANGE, OCTAVE  ·  3 ORDER SEED, MISSING SEED, RANDOM SEED |

- **MAIN**: already a column per panel. The wheel display moved to the left of the top row, CLOCK & TRANSPORT to the
  right.
- **LANE 1-4**: the notes row split by meaning: NOTE / NOTE RND / NOTE SEED and OCTAVE / OCT RND / OCT SEED / OCT
  RANGE (it had been NOTE ... OCTAVE | the rest), with a gap between the groups. The rhythm row's two columns are
  unchanged.
- **NOTES**: REGISTER / NOTE ORDER / MISSING NOTE | SCALE / ROOT / SCALE RANGE / OCTAVE | the three SEEDS, a column
  each (the columns had run from one panel into the other).

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).
