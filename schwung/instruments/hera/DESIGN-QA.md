# Design QA: Hera

Status: **passed on the device** (2026-10-04, ✅ in the README). No owner's notes yet.

## What changed
| Page | before | after |
|---|---|---|
| MAIN | 1 PATCH, CHORUS I, CHORUS II, OCTAVE  ·  2 VOLUME, HPF, VCF FREQ, RESONANCE  ·  3 VCF ENV, VCF LFO, ATTACK, DECAY  ·  4 SUSTAIN, RELEASE | 1 VCF FREQ, RESONANCE, VCF ENV, VCF LFO  ·  2 ATTACK, DECAY, SUSTAIN, RELEASE  ·  3 OCTAVE, VOLUME, HPF  ·  4 PATCH, CHORUS I, CHORUS II |
| DCO / LFO | 1 RANGE, DCO LFO, PWM DEPTH, PWM MODE  ·  2 PULSE, SAW, SUB, NOISE  ·  3 LFO RATE, LFO DELAY, LFO TRIG, VCA MODE  ·  4 VCF KYBD, VCF BEND, VCA LEVEL | 1 RANGE, DCO LFO, PWM DEPTH, PWM MODE  ·  2 PULSE, SAW, SUB, NOISE  ·  3 LFO RATE, LFO DELAY, LFO TRIG  ·  4 VCA MODE, VCF KYBD, VCF BEND, VCA LEVEL |

- **MAIN**: one Q-Link column per panel: VCF (freq, resonance, env, LFO) | ENV (the four faders) | MASTER (octave,
  volume, HPF) | PROGRAM + CHORUS (patch, chorus I, chorus II; the two panels sit side by side). The VCF panel's
  cutoff-envelope display moved to the left of its knobs (the owner's "displays left, controls right").
- **DCO / LFO**: DCO | the source mixer faders | LFO GENERATOR (rate, delay, trigger) | VCA & HPF (mode, keyboard,
  bend, level); before, VCA MODE was in the LFO's column.
- The envelope faders get the rebuilt slider filmstrips (see Braids' DESIGN-QA.md: smaller strips MPC draws right).

Made in the design (convert.py MAPS) and re-converted.

## Presets
56, in MPC's PRESET menu.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap.
