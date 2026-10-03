# Design QA: NuSaw

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 PATCH, SAWS, DETUNE, SPREAD  ·  2 SUB LEVEL, CUTOFF, RESONANCE, ENV MOD  ·  3 ATTACK, DECAY, SUSTAIN, RELEASE | 1 SAWS, DETUNE, SPREAD, SUB LEVEL  ·  2 CUTOFF, RESONANCE, ENV MOD  ·  3 ATTACK, DECAY, SUSTAIN, RELEASE  ·  4 PATCH |
| MORE | 1 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  2 SUB OCTAVE, VELOCITY, BEND RANGE, VOLUME  ·  3 CHORUS, CHORUS DEPTH, DELAY, DELAY TIME  ·  4 DELAY FBK, DELAY TONE | 1 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  2 SUB OCTAVE, VELOCITY, BEND RANGE, VOLUME  ·  3 CHORUS, CHORUS DEPTH  ·  4 DELAY, DELAY TIME, DELAY FBK, DELAY TONE |

- **MAIN**: column 1's outline took in the PROGRAM and SAWS panels, column 2's SAWS and FILTER. Now SAWS | FILTER |
  AMP ENVELOPE | PATCH (the patch last, as on Aphex and MonkSynth). The scope moved to the left of the PROGRAM panel
  and the filter curve to the left of the FILTER panel, with the controls on the right.
- **MORE**: CHORUS and DELAY get a column each (the delay had been split over two). A NUSAW wordmark fills the empty
  corner.
- The ADSR slider strips were rebuilt with the slider fix (the "silly" slider animation seen on Braids).

Made in the design (convert.py MAPS `augment` / `qlinks`, and an `art` plate) and re-converted, so a rerun keeps it.

## Presets
27 built-in patches, in MPC's PRESET menu (unchanged).

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).
