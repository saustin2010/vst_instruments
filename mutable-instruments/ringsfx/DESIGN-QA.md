# Design QA: Rings FX

Status: done offline (2026-10-03), **waiting for the device check**.

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
None upstream. See the batch's presets pass.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
