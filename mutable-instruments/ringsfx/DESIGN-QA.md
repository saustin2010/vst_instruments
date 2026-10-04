# Design QA: Rings FX

Status: **passed on the device** (2026-10-04, ✅ in the README).

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| RINGS FX | 1 MODEL, POLYPHONY, STRUCTURE, BRIGHTNESS  ·  2 DAMPING, POSITION, NOTE, FINE  ·  3 INPUT, MIX, WIDTH, VOLUME | 1 MODEL, POLYPHONY  ·  2 STRUCTURE, BRIGHTNESS, DAMPING, POSITION  ·  3 NOTE, FINE  ·  4 INPUT, MIX, WIDTH, VOLUME |

- The columns ran across panels (column 1 took in MODEL SELECTION and half of RESONATOR; column 2 the rest of
  RESONATOR and PITCH / TUNING). Now MODEL SELECTION | RESONATOR | PITCH / TUNING | INPUT & OUTPUT, a column each.
- The made-up peak meter in INPUT & OUTPUT (it never moved) went; the four knobs spread across the panel.

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

## Presets
Upstream has none, so the port brings 12 (`presets.json`, in MPC's PRESET menu): Init, Resonant Body, Metal Plate, Bright Bell, Low Drone, Sympathetic Strings, Sitar Drone, Chord Resonator, Plucked String, FM Ring, String Reverb, Shimmer Wash. Each sets every control;
levels evened out to within about 0.5 dB with a test signal through it.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
