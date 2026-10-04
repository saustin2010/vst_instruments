# Design QA: Denis

Status: **passed on the device** (2026-10-04, ✅ in the README). No owner's notes yet.

## What changed
| Page | before | after |
|---|---|---|
| DENIS | 1 PRESET, RANDOM ALL, OSC1 FREQ, OSC1 TIMBRE  ·  2 OSC2 FREQ, HARMONICS, OSC MIX, FOLD DEPTH  ·  3 FOLD TYPE, CUTOFF, Q (RESO), FILTER TYPE  ·  4 VEL>FILTER, PORTAMENTO, LEGATO | 1 OSC1 FREQ, OSC1 TIMBRE  ·  2 OSC2 FREQ, HARMONICS, OSC MIX  ·  3 FOLD DEPTH, FOLD TYPE, CUTOFF, Q (RESO)  ·  4 VEL>FILTER, PORTAMENTO, LEGATO |
| ENV / MOD | 1 ATTACK, DECAY, SUSTAIN, RELEASE  ·  2 NOISE MIX, NOISE TYPE, RANDOM SOUND, RANDOM MOD  ·  3 RESET MATRIX, LFO RATE, S&H RATE, ENV DEPTH  ·  4 NOISE DEPTH | 1 ATTACK, DECAY, SUSTAIN, RELEASE  ·  2 NOISE MIX, NOISE TYPE  ·  3 LFO RATE, S&H RATE, ENV DEPTH, NOISE DEPTH |
| ENV LFO | 1 ENV>PITCH1, ENV>TIMBRE, ENV>PITCH2, ENV>HARM  ·  2 ENV>FOLD, ENV>FTYPE, ENV>CUTOFF, ENV>LEVEL  ·  3 LFO>PITCH1, LFO>TIMBRE, LFO>PITCH2, LFO>HARM  ·  4 LFO>FOLD, LFO>FTYPE, LFO>CUTOFF, LFO>LEVEL | 1 ENV>PITCH1, ENV>TIMBRE, ENV>PITCH2, ENV>HARM  ·  2 ENV>FOLD, ENV>FTYPE, ENV>CUTOFF, ENV>LEVEL  ·  3 LFO>PITCH1, LFO>TIMBRE, LFO>PITCH2, LFO>HARM  ·  4 LFO>FOLD, LFO>FTYPE, LFO>CUTOFF, LFO>LEVEL (unchanged) |
| S&H NOISE | 1 S&H>PITCH1, S&H>TIMBRE, S&H>PITCH2, S&H>HARM  ·  2 S&H>FOLD, S&H>FTYPE, S&H>CUTOFF, S&H>LEVEL  ·  3 NOISE>PITCH1, NOISE>TIMBRE, NOISE>PITCH2, NOISE>HARM  ·  4 NOISE>FOLD, NOISE>FTYPE, NOISE>CUTOFF, NOISE>LEVEL | 1 S&H>PITCH1, S&H>TIMBRE, S&H>PITCH2, S&H>HARM  ·  2 S&H>FOLD, S&H>FTYPE, S&H>CUTOFF, S&H>LEVEL  ·  3 NOISE>PITCH1, NOISE>TIMBRE, NOISE>PITCH2, NOISE>HARM  ·  4 NOISE>FOLD, NOISE>FTYPE, NOISE>CUTOFF, NOISE>LEVEL (unchanged) |

- **DENIS**: one Q-Link column per panel: OSC 1 (freq, timbre) | OSC 2 (freq, harmonics, mix) | WAVEFOLDER + FILTER
  side by side (fold depth, fold type, cutoff, Q) | VOICE (vel to filter, portamento, legato). The preset pop-up,
  RANDOM ALL and FILTER TYPE are touch only: columns used to cut across panels to fit them.
- **ENV / MOD**: ENVELOPE | NOISE (mix, type) | MODULATORS; the RANDOM SOUND / RANDOM MOD / RESET MATRIX buttons are
  touch only (turning a knob to fire a button).
- **ENV LFO**, **S&H NOISE** (the mod matrix): unchanged, already half a row per column.

Made in the design (convert.py MAPS) and re-converted.

## Presets
30, in MPC's PRESET menu.

## For the owner
- ENV / MOD shows the envelope curve at the bottom right, diagonally from its knobs. Say if it should move.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap.
