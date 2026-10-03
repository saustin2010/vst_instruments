# Design QA: Plaits

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| PLAITS | 1 MODEL, HARMONICS, TIMBRE, MORPH  ·  2 LPG DECAY, LPG COLOUR, ATTACK, FM  ·  3 TIMBRE MOD, MORPH MOD, AUX MIX, OCTAVE | 1 MODEL, OCTAVE  ·  2 HARMONICS, TIMBRE, MORPH  ·  3 LPG DECAY, LPG COLOUR, ATTACK  ·  4 FM, TIMBRE MOD, MORPH MOD, AUX MIX |
| PLAY | 1 FM PATCH, LEGATO, VELOCITY | 1 FM PATCH  ·  2 LEGATO, VELOCITY |

- **PLAITS**: five panels for twelve controls, so columns ran across them (column 1 took in MODEL and SOUND MACROS;
  column 2 LOW PASS GATE and MODULATION). Now four panels, a column each:
  - **MODEL**: MODEL and OCTAVE (OCTAVE moved here from OUTPUT; the made-up "16 ALGORITHMS / PITCH" footer went).
  - **SOUND MACROS**: HARMONICS, TIMBRE, MORPH.
  - **LOW PASS GATE**: LPG DECAY, LPG COLOUR, ATTACK.
  - **MODULATION / AUX**: FM, TIMBRE MOD, MORPH MOD and AUX MIX (moved here; the OUTPUT panel went).
- **PLAY**: 6-OP FM PATCH | PLAYING (LEGATO, VELOCITY), a column each. FM PATCH shows the patch's name while a 6-Op
  FM model is selected, its number otherwise.

Made in the design (convert.py MAPS `augment`, with the new `move` op, and `qlinks`), so a rerun keeps it.

## Presets
None in the PRESET menu: upstream ships only Move "chain patches" (whole Move chains, not this synth alone). See
the batch's presets pass.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).
