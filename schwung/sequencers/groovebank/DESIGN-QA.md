# Design QA: GrooveBank

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel or group, as Moog; outlines that frame
one panel).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 GROOVE, VARIANT, SWING, GATE  ·  2 STRUM, ACCENT, LATCH | 1 VARIANT, SWING, GATE  ·  2 STRUM, ACCENT, LATCH  ·  3 GROOVE |

- Column 1 had been GROOVE plus three FEEL knobs, so its outline took in both panels. Now FEEL's six controls are
  two columns (VARIANT / SWING / GATE and STRUM / ACCENT / LATCH) and GROOVE has the third (last, as a preset).
- A "HOLD SW" caption under the LATCH switch (it sat under MPC's own LATCH name) went, and the emblem's "MPC
  EMBEDDED DSP" reads "SCHWUNG MIDI FX".

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
