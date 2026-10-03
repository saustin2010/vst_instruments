# Design QA: Rampage

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right; trigger buttons off the Q-Links).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| RAMPAGE | 1 A RANGE, A RISE, A FALL, A SHAPE  ·  2 B RANGE, B RISE, B FALL, B SHAPE  ·  3 CYCLE A, A TRIGGER, BALANCE, CYCLE B  ·  4 B TRIGGER, AUDIO, VOLUME | 1 A RANGE, A RISE, A FALL, A SHAPE  ·  2 B RANGE, B RISE, B FALL, B SHAPE  ·  3 CYCLE A, BALANCE, CYCLE B  ·  4 AUDIO, VOLUME |
| MIDI | 1 A NOTES, B NOTES, A KEY TRACK, B KEY TRACK  ·  2 CC CHANNEL, CC OUT A, CC OUT B, CC MIN  ·  3 CC MAX, EOC NOTES, EOC NOTE A, EOC NOTE B | 1 A NOTES, B NOTES, A KEY TRACK, B KEY TRACK  ·  2 CC CHANNEL, CC OUT A, CC OUT B  ·  3 CC MIN, CC MAX  ·  4 EOC NOTES, EOC NOTE A, EOC NOTE B |

- **RAMPAGE**:
  - The channel scopes moved to the left of CHANNEL A and CHANNEL B, the knobs to the right.
  - CHANNEL A CONTROLS, LOGIC / COMP and CHANNEL B CONTROLS became one CYCLE / TRIGGER / BALANCE panel: TRIG A |
    CYCLE A, BALANCE, CYCLE B | TRIG B. The middle three are one Q-Link column (its outline had covered three
    panels); TRIG A / TRIG B are touch only (turning a knob fired them, as on Aphex).
  - The made-up output meters went.
- **MIDI**: NOTES IN | CC OUT (CHANNEL, OUT A, OUT B) / CC RANGE (MIN, MAX) | END OF CYCLE, a column each. CC OUT's
  five controls were split in two, and the empty MIDI OUT panel (its info plate never rendered) is now CC OUT.

RAMPAGE is made in the design (convert.py MAPS `augment`, with the new `move` op, and `qlinks`); MIDI in the port's
page plan (`layout.grid.conf`). A rerun keeps both.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py). The two RANGE
selectors sit in their panels' title bars, so those outlines reach a little above the panels.
