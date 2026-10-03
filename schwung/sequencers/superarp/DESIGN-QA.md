# Design QA: SuperArp

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel or group, as Moog; outlines that frame
one panel; displays on the left, controls on the right).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 SYNC, RATE, TRIPLET, TEMPO (BPM)  ·  2 LATCH STATE, OCTAVES, GATE, VELOCITY  ·  3 SWING, VOICES | 1 SYNC, RATE, TRIPLET, TEMPO (BPM)  ·  2 LATCH STATE, OCTAVES  ·  3 GATE, VELOCITY, SWING, VOICES |
| PATTERN | 1 MODE, PATTERN, MODE TRIGGER, MISSING NOTE  ·  2 MODE SEED, RHYTHM, RHY TRIGGER, RND LENGTH  ·  3 RND CHORDS, RND CH SEED | 1 MODE, PATTERN  ·  2 MODE TRIGGER, MISSING NOTE, MODE SEED  ·  3 RHYTHM, RHY TRIGGER  ·  4 RND LENGTH, RND CHORDS, RND CH SEED |
| MODIFY | 1 MOD LOOP, MOD TRIGGER, DROP, DROP SEED  ·  2 VEL RANDOM, VEL SEED, GATE RANDOM, GATE SEED  ·  3 OCT RANDOM, OCT RANGE, OCT SEED, NOTE RANDOM  ·  4 NOTE SEED | 1 MOD LOOP, MOD TRIGGER, DROP, DROP SEED  ·  2 VEL RANDOM, VEL SEED, GATE RANDOM, GATE SEED  ·  3 OCT RANDOM, OCT RANGE, OCT SEED  ·  4 NOTE RANDOM, NOTE SEED |

- **MAIN**: CLOCK & TIMING | LATCH + OCTAVES | GATE / VELOCITY / SWING / VOICES (the groups the design draws dividers
  between). The MIDI event display moved to the left of the top row, CLOCK & TIMING to the right.
- **PATTERN**: MODE + PATTERN | the progression's MODE TRIGGER / MISSING NOTE / MODE SEED | RHYTHM | RANDOM PATTERN,
  a column each, with a gap between the progression's two groups (the columns had run across all three panels).
- **MODIFY**: the modifiers' two fours as before, then RANDOM OCTAVE | RANDOM NOTE (NOTE RANDOM had been in the
  octave column).

MAIN is made in the design (convert.py MAPS `augment` / `qlinks`); PATTERN and MODIFY in the port's page plan
(`layout.grid.conf`). A rerun keeps both.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).
