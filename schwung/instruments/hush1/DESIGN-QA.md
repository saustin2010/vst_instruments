# Design QA: Hush One

Status: done offline (2026-10-03), **waiting for the device check**. No owner's notes yet (its presets were the
first fix of the day: your TAL-BassLine-101 presets show after the 11 built-in ones).

## What changed
| Page | before | after |
|---|---|---|
| MAIN | 1 PATCH, CUTOFF, RESONANCE, ENV AMT  ·  2 KEY TRACK, ATTACK, DECAY, SUSTAIN  ·  3 RELEASE, VCA MODE, OCTAVE, VELO SENS  ·  4 MASTER VOL | 1 CUTOFF, RESONANCE, ENV AMT, KEY TRACK  ·  2 ATTACK, DECAY, SUSTAIN, RELEASE  ·  3 VCA MODE, OCTAVE, VELO SENS, MASTER VOL  ·  4 PATCH |
| SOURCE | 1 SAW WAVE, PULSE / SQR, SUB OSC, NOISE  ·  2 TRANSPOSE, FINE TUNE, WHITE NOISE, FLT ATTACK  ·  3 FLT DECAY, FLT SUSTAIN, FLT RELEASE, SUB MODE  ·  4 PULSE WIDTH, PWM SOURCE, PWM LFO, PWM ENV | 1 SAW WAVE, PULSE / SQR, SUB OSC, NOISE  ·  2 TRANSPOSE, FINE TUNE, WHITE NOISE  ·  3 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  4 PULSE WIDTH, PWM SOURCE, PWM LFO, PWM ENV |
| MODULATOR | 1 LFO RATE, LFO WAVE, LFO RETRIG, LFO SYNC  ·  2 LFO INVERT, PITCH SNAP, LFO PITCH, LFO FILTER  ·  3 LFO PWM, VEL FILTER, ENV POLARITY, ENV FULL  ·  4 VOL CORRECT, DECLICK | 1 LFO RATE, LFO WAVE  ·  2 LFO PITCH, LFO FILTER, LFO PWM  ·  3 VEL FILTER, ENV POLARITY, ENV FULL  ·  4 VOL CORRECT, DECLICK |
| PERFORM | 1 GLIDE, PORTA MODE, PORTA CURVE, BEND RANGE  ·  2 RETRIGGER, PRIORITY, HOLD, SAME NOTE  ·  3 GATE MODE, VEL MODE | 1 GLIDE, PORTA MODE, PORTA CURVE, BEND RANGE  ·  2 RETRIGGER, PRIORITY, HOLD, SAME NOTE  ·  3 GATE MODE, VEL MODE (unchanged) |

- **MAIN**: laid out like Moog's: VCF | ENV (the four faders) | OUTPUT (VCA mode, octave, velo sens, master vol) |
  PATCH. The VCF's response-curve display moved to the left of its knobs.
- **SOURCE**: the mixer faders | transpose, fine tune, white noise | the filter envelope | PWM (width, source, LFO,
  env). SUB MODE is touch only (alone at the top of the other panel).
- **MODULATOR**: LFO rate and wave | LFO AMOUNT | FILTER EXTRAS split 3 + 2. The four LFO switches (retrig, sync,
  invert, pitch snap) are touch only.
- **PERFORM**: unchanged (already one column per panel).
- The faders get the rebuilt slider filmstrips (see Braids' DESIGN-QA.md).

Made in the design (convert.py MAPS) and re-converted.

## Presets
11 built-in + the TAL-BassLine-101 files in /sdcard/vst/hush1/presets (124 installed), all in MPC's PRESET menu.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (SOURCE's TRANSPOSE / FINE TUNE / WHITE NOISE moved a
little left, clear of the FLT envelope's column: their outlines had crossed).
