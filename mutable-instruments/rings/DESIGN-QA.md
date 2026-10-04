# Design QA: Rings

Status: **passed on the device** (2026-10-04, ✅ in the README).

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
The module has none, so the port brings 14 (`presets.json`, the wrapper's own presets, in MPC's PRESET menu): Init, Glass Marimba, Tubular Bell, Wood Block, Sympathetic Sitar, Chord Harp, Nylon String, Steel String, Dulcimer, FM Tines, FM Gong, Verb String, Synth Strings, Choir Pad. Each sets every control; offline all 14 play, levels evened out with VOLUME (the short, percussive ones a little lower).

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
