# Design QA: Braids

Status: **passed on the device** (2026-10-04, ✅ in the README).

## Owner's notes
- Better Q-Link alignment.
- Fix the sliders so the animation isn't silly.
- Move the waveforms (envelope curves) to the left and the sliders to the right.

## What changed
| Page | before | after |
|---|---|---|
| BRAIDS | 1 TIMBRE, COLOR, CUTOFF, RESONANCE  ·  2 FILTER ENV, FM, OCTAVE, VOLUME  ·  3 PATCH, ALGORITHM | 1 ALGORITHM, TIMBRE, COLOR  ·  2 CUTOFF, RESONANCE, FILTER ENV, FM  ·  3 OCTAVE, VOLUME  ·  4 PATCH |
| ENVELOPES | 1 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  2 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE | 1 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  2 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE (unchanged) |

- **BRAIDS**: one Q-Link column per panel: OSCILLATOR (algorithm, timbre, color) | FILTER + MOD (the two panels
  sit side by side) | OUTPUT | PROGRAM. Each highlight now frames its own panel; before, columns cut across three.
- **ENVELOPES**: the curve displays on the left, the sliders on the right (in the design: the row is reversed).
- **Sliders**: their filmstrips were 160 x 20480 px, taller than MPC draws correctly (knob strips work up to 12288),
  which is the likely cause of the odd thumb movement. The skin builder now makes slider strips the way Akai's own
  are made: frames of the slider's size, 76 of them here (44 x 12160 px). The same fix reaches every plugin with
  sliders (Noisemaker, Libpo32, Hush One, Hera, NuSaw).

All made in the design (convert.py MAPS) and re-converted.

## Presets
10, in MPC's PRESET menu.

## For the owner
- The slider fix is built the way the envelope displays were fixed (that one was confirmed on the Live II), but
  this exact change hasn't been seen on a device yet: check that the thumbs now track smoothly.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
