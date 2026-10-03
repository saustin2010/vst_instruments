# Design QA: Hank

Status: done offline (2026-10-03), **waiting for the device check**. No owner's notes yet.

## What changed
| Page | before | after |
|---|---|---|
| HANK | 1 PATCH, RATIO, BRIGHT, BITE (MOD)  ·  2 TONE, ATTACK, DECAY, SUSTAIN  ·  3 NOISE, GLIDE, VOICES, TRANSPOSE  ·  4 VOLUME | 1 RATIO, BRIGHT, BITE (MOD), TONE  ·  2 ATTACK, DECAY, SUSTAIN  ·  3 NOISE, GLIDE, VOICES  ·  4 TRANSPOSE, VOLUME |

- One Q-Link column per panel: OPERATOR (ratio, bright, bite, tone) | MOD ENVELOPE (attack, decay, sustain) | VOICE,
  whose five knobs split 3 + 2 (noise, glide, voices | transpose, volume). Before, the columns ran from the patch
  display across the operator and envelope panels. The PATCH stepper is touch only (its arrows, and MPC's PRESET menu).

Made in the design (convert.py MAPS) and re-converted.

## Presets
32, in MPC's PRESET menu.

## For the owner
- The RATIO pop-up overhangs the top edge of its panel (as before). Say if it should move.

## Checked
Offline test PASSED; check_skin OK; Q-Link outlines don't overlap (the two VOICE halves meet edge to edge).
