# Design QA: Fizzik

Status: done offline (2026-10-03), **waiting for the device check**. No owner's notes yet.

## What changed
| Page | before | after |
|---|---|---|
| MAIN | 1 PRESET, RANDOM ALL, RND EXCITER, MODEL A  ·  2 MODEL B, COUPLE, BALANCE, CUTOFF  ·  3 RESONANCE, FILTER TYPE, VOICING, RND RESON  ·  4 DRIVE, WIDTH, LEVEL | 1 MODEL A, MODEL B, COUPLE, BALANCE  ·  2 CUTOFF, RESONANCE, FILTER TYPE, VOICING  ·  3 DRIVE, WIDTH, LEVEL  ·  4 PRESET |
| EXCITER | 1 EXC MIX, CRACKLE, COLOR, ATTACK  ·  2 DECAY, EXC RESO, VEL LEVEL, VEL COLOR  ·  3 STRUCTURE A, DECAY A, DAMP A, POSITION A  ·  4 TONE A, TUNE A, TENSION A | 1 EXC MIX, CRACKLE, COLOR, ATTACK  ·  2 DECAY, EXC RESO, VEL LEVEL, VEL COLOR  ·  3 STRUCTURE A, DECAY A, DAMP A, POSITION A  ·  4 TONE A, TUNE A, TENSION A (unchanged) |
| RESONATOR B | 1 STRUCTURE B, DECAY B, DAMP B, POSITION B  ·  2 TONE B, TUNE B, TENSION B, GLIDE  ·  3 AMP ATK, AMP REL, SPREAD, REVERB  ·  4 REV SIZE, REV DAMP | 1 STRUCTURE B, DECAY B, DAMP B, POSITION B  ·  2 TONE B, TUNE B, TENSION B  ·  3 GLIDE, AMP ATK, AMP REL, SPREAD  ·  4 REVERB, REV SIZE, REV DAMP |
| DELAY / FX | 1 DELAY MIX, DLY TIME, DLY FBK, DLY TONE  ·  2 TONE, BODY, CHORUS, CHO RATE  ·  3 CHO DEPTH, GLUE, LIM DRIVE, LIM CEIL | 1 DELAY MIX, DLY TIME, DLY FBK, DLY TONE  ·  2 TONE, BODY, CHORUS, CHO RATE  ·  3 CHO DEPTH, GLUE, LIM DRIVE, LIM CEIL (unchanged) |
| LFO / AT | 1 LFO1 RATE, LFO1 DEPTH, LFO1 SHAPE, LFO1 TARGET  ·  2 LFO2 RATE, LFO2 DEPTH, LFO2 SHAPE, LFO2 TARGET  ·  3 AT PRESET, AT BRIGHT, AT BOW, AT CUTOFF  ·  4 AT VIB, AT BEND, AT VIB RATE, AT CURVE | 1 LFO1 RATE, LFO1 DEPTH, LFO1 SHAPE, LFO1 TARGET  ·  2 LFO2 RATE, LFO2 DEPTH, LFO2 SHAPE, LFO2 TARGET  ·  3 AT PRESET, AT BRIGHT, AT BOW, AT CUTOFF  ·  4 AT VIB, AT BEND, AT VIB RATE, AT CURVE (unchanged) |

- **MAIN**: one Q-Link column per panel: RESONATORS (model A, model B, couple, balance) | FILTER (cutoff, resonance,
  type, voicing) | OUT (drive, width, level) | PATCH (preset). The three RANDOM buttons are touch only and now sit
  together in PATCH, stacked beside a narrower scope (RND RESON was alone in OUT).
- **RESONATOR B**: the row of seven splits 4 + 3 instead of running into the next panel; VOICE & ENVELOPE and
  REVERB get a column each.
- **EXCITER**, **DELAY / FX**, **LFO / AT**: unchanged (already a half row per column).

Made in the design (convert.py MAPS) and re-converted.

## Presets
31, in MPC's PRESET menu.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap.
