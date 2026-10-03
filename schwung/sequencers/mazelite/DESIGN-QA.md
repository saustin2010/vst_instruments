# Design QA: MazeLite

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel or group, as Moog; outlines that frame
one panel; no maker badges).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAZE | 1 SCALE, NOTE RATE, NOTE LENGTH, RESET BOTH  ·  2 S1 RESET, S2 RESET, S1 CORRUPT, S1 RANGE  ·  3 S1 LENGTH, S1 TRIG MIX, S2 CORRUPT, S2 RANGE  ·  4 S2 LENGTH, S2 TRIG MIX | 1 SCALE, NOTE RATE, NOTE LENGTH, RESET BOTH  ·  2 S1 CORRUPT, S1 RANGE, S1 LENGTH, S1 TRIG MIX  ·  3 S2 CORRUPT, S2 RANGE, S2 LENGTH, S2 TRIG MIX  ·  4 S1 RESET, S2 RESET |

- Columns 2-4 had mixed the two RESET pop-ups (MIDI OUT panel) with the S1 and S2 knob rows (bottom strip), so the
  outlines covered half the page. Now OUTPUT | S1 | S2 | the RESETs, a column each.
- The S1 / S2 knob rows were packed so tight that MPC's names and values ran into each other: they're spaced out
  now, and their "S1 PARAMS:" / "S2 PARAMS:" captions went (MPC already names each knob S1 / S2 ...).
- The centre plate read "AKAI MPC LIVE II": now "SCHWUNG MIDI FX".
- FLIP / STEP stay touch only (they're triggers), as before.

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
