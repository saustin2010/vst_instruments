# Design QA: MonkSynth

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| SINGER | 1 SINGER, VOWEL, HEAD SIZE, BREATH  ·  2 LEVEL, ATTACK, DECAY, SUSTAIN  ·  3 RELEASE, GLIDE, VIBRATO, VIB RATE  ·  4 BEND RNG | 1 VOWEL, HEAD SIZE, BREATH, LEVEL  ·  2 ATTACK, DECAY, SUSTAIN, RELEASE  ·  3 GLIDE, VIBRATO, VIB RATE, BEND RNG  ·  4 SINGER |
| CHOIR | 1 UNISON, DETUNE, SPREAD, DELAY  ·  2 DELAY RATE, PRESSURE TO, PRES DEPTH | 1 UNISON, DETUNE, SPREAD  ·  2 DELAY, DELAY RATE  ·  3 PRESSURE TO, PRES DEPTH |

- **SINGER**: the columns ran across panels (SINGER with the VOICE knobs, LEVEL with the envelope, RELEASE with
  EXPRESSION), so each outline took in two panels. Now column 1 = VOICE, 2 = the ADSR, 3 = EXPRESSION, and the SINGER
  stepper (top left) has column 4 to itself, as Aphex's PRESET.
- **CHOIR**: UNISON, ECHO and PRESSURE get a column each (ECHO's DELAY had been in UNISON's column).
- The screen itself was already displays on the left, controls on the right; unchanged.

Made in the design (convert.py MAPS `qlinks`) and re-converted, so a rerun keeps it.

## Presets
The 12 singers are VST programs: they show in MPC's PRESET menu by name (since the PRESET-menu change).

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).
