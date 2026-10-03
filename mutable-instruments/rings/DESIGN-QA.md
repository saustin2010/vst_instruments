# Design QA: Rings

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| RINGS | 1 MODEL, POLYPHONY, STRUCTURE, BRIGHTNESS  ·  2 DAMPING, POSITION, VELOCITY, OCTAVE  ·  3 BEND RANGE, SYNTH FX, WIDTH, VOLUME | 1 MODEL, POLYPHONY  ·  2 STRUCTURE, BRIGHTNESS, DAMPING, POSITION  ·  3 VELOCITY, OCTAVE, BEND RANGE, SYNTH FX  ·  4 WIDTH, VOLUME |

- The columns ran across panels (column 1 took in MODEL and half of RESONATOR; column 2 the rest of RESONATOR and
  half of PLAYING). Now MODEL | RESONATOR | PLAYING | OUTPUT, a column each.
- **MODEL**: the dispersion display moved to the left, MODEL and POLYPHONY to the right; its made-up readouts
  (FUNDAMENTAL, DISPERSION, 64 MODES) went.
- **OUTPUT**: the made-up peak meter (it never moved) went; WIDTH and VOLUME are centred.

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

## Presets
None upstream (the module has no presets). See the batch's presets pass.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
