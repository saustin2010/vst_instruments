# Design QA: Chordism

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
- Are there presets? (Yes: 57, now in the PRESET menu.)
- Q-Links are good on page one, but the OSCILLATORS page is messy and some controls are grouped together.
- The FILTER ENV screen has funny buttons and controls; needs better alignment.

## What changed
| Page | before | after |
|---|---|---|
| MAIN | 1 CHORD, TUNING, SCALE, ROOT  ·  2 DETUNE, WIDTH, SPREAD, ROTATION  ·  3 CUTOFF, RESONANCE, FILTER MODE, SLOPE  ·  4 ATTACK, RELEASE, VCA MODE, VOLUME | 1 CHORD, TUNING, SCALE, ROOT  ·  2 DETUNE, WIDTH, SPREAD, ROTATION  ·  3 CUTOFF, RESONANCE, FILTER MODE, SLOPE  ·  4 ATTACK, RELEASE, VCA MODE, VOLUME (unchanged) |
| OSCILLATORS | 1 WAVE 1, MIX 1, WAVE 2, MIX 2  ·  2 WAVE 3, MIX 3, WAVE 4, MIX 4  ·  3 SHAPE, SHAPE 1, SHAPE 2, SHAPE 3  ·  4 SHAPE 4, LFO PHASE 1, LFO PHASE 2, LFO MODE | 1 WAVE 1, MIX 1, SHAPE 1, LFO PHASE 1  ·  2 WAVE 2, MIX 2, SHAPE 2, LFO PHASE 2  ·  3 WAVE 3, MIX 3, SHAPE 3, LFO PHASE 3  ·  4 WAVE 4, MIX 4, SHAPE 4, LFO PHASE 4 |
| SHAPE | 1 LFO PHASE 3, LFO PHASE 4, PAN MORPH, PAN MORPH IN  ·  2 FM MOD, FM AMT, MORPH INDEX, MORPH INT  ·  3 FM AMT 1, FM AMT 2, FM AMT 3, FM AMT 4  ·  4 FM POSITION | 1 SHAPE, LFO MODE, PAN MORPH, PAN MORPH IN  ·  2 FM MOD, FM AMT, MORPH INDEX, MORPH INT  ·  3 FM AMT 1, FM AMT 2, FM AMT 3, FM AMT 4  ·  4 FM POSITION |
| FILTER ENV | 1 ENV A, ENV D, ENV AMT, DRIVE  ·  2 FLT ENV MODE, FENV RESET, LOFI POS, FLT LFO RATE  ·  3 FLT LFO DPTH, FLT LFO SPRD, FLT LFO WAVE, FLT LFO MODE  ·  4 SHP LFO WAVE, SHP LFO RATE, SHP LFO DPTH | 1 ENV A, ENV D, ENV AMT, DRIVE  ·  2 FLT LFO RATE, FLT LFO DPTH, FLT LFO SPRD, FLT LFO WAVE  ·  3 SHP LFO WAVE, SHP LFO RATE, SHP LFO DPTH  ·  4 FLT ENV MODE, FENV RESET, LOFI POS |
| VIBRATO | 1 VIB DEPTH, VIB SPEED, VIB DELAY, SWEEP  ·  2 SWEEP RATE, VIBRAT STRAY, VIB OSCS, SWEEP OSCS  ·  3 LVL LFO RATE, LVL LFO DPTH, LVL LFO WAVE, LVL LFO MODE  ·  4 PAN LFO RATE, PAN LFO DPTH, PAN LFO WAVE, PAN LFO MODE | 1 VIB DEPTH, VIB SPEED, VIB DELAY, SWEEP  ·  2 SWEEP RATE, VIBRAT STRAY, VIB OSCS, SWEEP OSCS  ·  3 LVL LFO RATE, LVL LFO DPTH, LVL LFO WAVE, LVL LFO MODE  ·  4 PAN LFO RATE, PAN LFO DPTH, PAN LFO WAVE, PAN LFO MODE (unchanged) |
| TREMOLO | 1 TREM RATE, TREM DEPTH, GLIDE, TREMOLO WAVE  ·  2 GLIDE LEGATO, VCA RESET, DRONE, GRIND  ·  3 BIT SHIFT, DECIMATOR | 1 TREM RATE, TREM DEPTH, GLIDE  ·  2 TREMOLO WAVE, GLIDE LEGATO, VCA RESET, DRONE  ·  3 GRIND, BIT SHIFT, DECIMATOR |
| DELAY | 1 DELAY MIX, DLY TIME, DLY FBK, DLY TONE  ·  2 DLY MOD DPTH, DELAY MODE, DLY TONE HI, DLY TONE LO  ·  3 DLY MOD RATE | 1 DELAY MIX, DLY TIME, DLY FBK, DLY TONE  ·  2 DELAY MODE, DLY TONE HI, DLY TONE LO  ·  3 DLY MOD DPTH, DLY MOD RATE |
| REVERB | 1 REVERB MIX, REV DECAY, REV DAMPING, SHIMMER  ·  2 ROOM SIZE, REV LOW CUT, REV MOD RATE, REV MOD DPTH | 1 REVERB MIX, REV DECAY, REV DAMPING, SHIMMER  ·  2 ROOM SIZE  ·  3 REV LOW CUT, REV MOD RATE, REV MOD DPTH |
| CHORD MAP 1 | 1 C, C#, D, D#  ·  2 E, F, F#, G  ·  3 G#, A, A#, B  ·  4 INTERVAL 1, INTERVAL 2, INTERVAL 3, CTRL SRC | 1 C, C#, D, D#  ·  2 E, F  ·  3 F#, G, G#, A  ·  4 A#, B |
| CHORD MAP 2 | 1 CTRL CC, CTRL>CUTOFF, CTRL>MORPH, CTRL>VIBRATO  ·  2 CTRL>SHAPE, CTRL>FM | 1 INTERVAL 1, INTERVAL 2, INTERVAL 3  ·  2 CTRL SRC, CTRL CC, CTRL>CUTOFF, CTRL>MORPH  ·  3 CTRL>VIBRATO, CTRL>SHAPE, CTRL>FM |
| ARPEGGIATOR | 1 EUCLID STEPS, EUCLID BEATS, ARP TEMPO, VAR COUNT  ·  2 ARP STATUS, ARP HOLD, ARP DIRECTIO, ARP VAR INT  ·  3 CLOCK SYNC, CLOCK DIVISI | 1 EUCLID STEPS, EUCLID BEATS, ARP TEMPO, VAR COUNT  ·  2 ARP HOLD, ARP DIRECTIO, ARP VAR INT  ·  3 CLOCK SYNC, CLOCK DIVISI  ·  4 ARP STATUS |

- **OSCILLATORS**: one Q-Link column per voice: its WAVE, MIX, SHAPE and LFO PHASE. Each voice's SHAPE and LFO PHASE
  knobs now sit directly under its WAVE / MIX box, so each column's outline is a clean strip down the page. LFO
  PHASE 3 and 4 moved here from SHAPE; the global SHAPE and LFO MODE moved to SHAPE.
- **SHAPE**: the first panel is now SHAPE & PAN: the global SHAPE, LFO MODE and the pan-morph pair (one column),
  then the FM rows (MOD / AMOUNT / MORPH, the four per-voice FM amounts, FM POSITION).
- **FILTER ENV**: the envelope panel's knobs and their options packed together at the top (they floated with a big
  gap). Columns: ENV (A, D, amount, drive) | filter LFO (rate, depth, spread, wave) | SHAPE LFO | ENV options.
  FLT LFO MODE is touch only (no Q-Link slot left in that group).
- **TREMOLO / DELAY / REVERB / ARPEGGIATOR**: a column per row; the delay and reverb modulation pairs get their
  own column (DELAY: MOD DEPTH above MOD RATE).
- **CHORD MAP**: bank 1 = the 12 notes in order, split so no outline crosses a row (C..D# | E F | F#..A | A# B),
  with more space between the two rows; bank 2 = INTERVALS | CONTROL (source, CC, to cutoff, to morph) | to vibrato,
  shape, FM. (Bank 2's outlines meet edge to edge: those panels sit side by side.)
- **MAIN**, **VIBRATO**: unchanged (already one column per group).

All made in the design (convert.py MAPS) and re-converted.

## Presets
57 built-in presets, in MPC's PRESET menu (there's no PRESET control on the pages).

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (CHORD MAP's bank 2 had two that crossed: its CONTROL row is two groups with a gap now,
CC / TO CUTOFF / TO MORPH with SOURCE, and TO VIBRATO / TO SHAPE / TO FM).
