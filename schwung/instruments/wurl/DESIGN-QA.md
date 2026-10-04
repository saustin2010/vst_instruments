# Design QA: Wurl

Status: **passed on the device** (2026-10-04, ✅ in the README).

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| WURL | 1 PRESET, BRIGHT, DARKEN, BARK  ·  2 TUNE, ATTACK, DECAY, VOLUME  ·  3 TREMOLO, SPEAKER, REVERB | 1 BRIGHT, DARKEN, BARK, TUNE  ·  2 ATTACK, DECAY, VOLUME  ·  3 TREMOLO, SPEAKER, REVERB  ·  4 PRESET |

- **Q-Links**: the columns ran across panels (column 1 was PRESET + three TONE MATRIX knobs; column 2 took in TUNE
  and the whole amplifier). Now TONE MATRIX | SOLID STATE AMPLIFIER | CABINET | PRESET (the preset last, as on
  Aphex), a column each.
- **Displays on the left**: the reed-harmonics display moved to the left of TONE MATRIX and the photo-cell display
  to the left of CABINET, with the knobs on the right.
- **No maker badges** (the repo's rule): the emblem plate read "THE ORIGINAL / Wurlitzer / ELECTRONIC PIANO / 200 A"
  with a serial number, the factory's town and "AKAI PROFESSIONAL STANDALONE ENGINE", and the preset panel said
  "Akai MPC Standalone DSP Core". The plate now reads WURL / ELECTRIC PIANO / 200 A (naming the model it emulates
  is fine); the rest went.

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

## Presets
Its built-in presets are in MPC's PRESET menu (unchanged).

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
