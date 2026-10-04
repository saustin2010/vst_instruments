# Design QA: Helm

Status: **passed on the device** (2026-10-04, ✅ in the README).

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; groups of up to four, 3-2-4-3 style where a group is smaller).

## What changed
Seven of the twelve pages had Q-Link outlines running across panels: their rows of six to eight controls filled
columns of four in page order.

Q-Link columns before → after ("-" = an empty slot; names as they are now):

| Page | before | after |
|---|---|---|
| MAIN | 1 PATCH, VOLUME, POLYPHONY, OCTAVE  ·  2 LEGATO, CUTOFF, RESONANCE, FILTER TYPE  ·  3 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE | 1 VOLUME, POLYPHONY, OCTAVE, LEGATO  ·  2 CUTOFF, RESONANCE, FILTER TYPE  ·  3 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  4 PATCH |
| OSC | 1 OSC1 TRANSP, OSC1 TUNE, OSC1 DETUNE, OSC1 VOICES  ·  2 OSC1 VOLUME, OSC1 WAVE, OSC1 HARMON, OSC2 TRANSP  ·  3 OSC2 TUNE, OSC2 DETUNE, OSC2 VOICES, OSC2 VOLUME  ·  4 OSC2 WAVE, OSC2 HARMON | 1 OSC1 WAVE, OSC1 TRANSP, OSC1 TUNE, OSC1 VOLUME  ·  2 OSC1 VOICES, OSC1 DETUNE, OSC1 HARMON  ·  3 OSC2 WAVE, OSC2 TRANSP, OSC2 TUNE, OSC2 VOLUME  ·  4 OSC2 VOICES, OSC2 DETUNE, OSC2 HARMON |
| OSC MIX | 1 CROSS MOD, FBK AMOUNT, FBK TRANSP, FBK TUNE  ·  2 OSC MIX, NOISE VOL, SUB OCT, SUB SHUF  ·  3 SUB VOL, SUB OSC WAVE | 1 CROSS MOD, FBK AMOUNT, FBK TRANSP, FBK TUNE  ·  2 OSC MIX  ·  3 NOISE VOL  ·  4 SUB OCT, SUB SHUF, SUB VOL, SUB OSC WAVE |
| FILTER | 1 FLT ENV AMT, FILTER BLEND, FILTER DRIVE, FILTER ON  ·  2 SATURATION, FILTER SHELF, FILTER STYLE, FLT KEYTRACK  ·  3 FORMANT ON, FORMANT X, FORMANT Y, FLT ATTACK  ·  4 FLT DECAY, FLT RELEASE, FLT SUSTAIN | 1 FILTER ON, FILTER STYLE, FILTER SHELF, FILTER BLEND  ·  2 FILTER DRIVE, SATURATION, FLT ENV AMT, FLT KEYTRACK  ·  3 FORMANT ON, FORMANT X, FORMANT Y  ·  4 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE |
| MOD ENV | 1 MOD ATTACK, MOD DECAY, MOD RELEASE, MOD SUSTAIN  ·  2 LFO1 AMOUNT, LFO1 FREQ, LFO1 RETRIG, LFO1 SYNC  ·  3 LFO1 TEMPO, LFO1 WAVE | 1 MOD ATTACK, MOD DECAY, MOD SUSTAIN, MOD RELEASE  ·  2 LFO1 WAVE, LFO1 AMOUNT, LFO1 RETRIG  ·  3 LFO1 SYNC, LFO1 FREQ, LFO1 TEMPO |
| MONO LFO | 1 LFO2 AMOUNT, LFO2 FREQ, LFO2 RETRIG, LFO2 SYNC  ·  2 LFO2 TEMPO, LFO2 WAVE, PLFO AMOUNT, PLFO FREQ  ·  3 PLFO SYNC, PLFO TEMPO, PLFO WAVE | 1 LFO2 WAVE, LFO2 AMOUNT, LFO2 RETRIG  ·  2 LFO2 SYNC, LFO2 FREQ, LFO2 TEMPO  ·  3 PLFO WAVE, PLFO AMOUNT  ·  4 PLFO SYNC, PLFO FREQ, PLFO TEMPO |
| STEP SEQ | 1 NUM STEPS, STEP FREQ, STEP RETRIG, STEP SYNC  ·  2 STEP TEMPO, STEP SMOOTH, STEP 1, STEP 2  ·  3 STEP 3, STEP 4, STEP 5, STEP 6  ·  4 STEP 7, STEP 8 | 1 NUM STEPS, STEP SMOOTH, STEP RETRIG  ·  2 STEP SYNC, STEP FREQ, STEP TEMPO  ·  3 STEP 1, STEP 2, STEP 3, STEP 4  ·  4 STEP 5, STEP 6, STEP 7, STEP 8 |
| STEPS | 1 STEP 9, STEP 10, STEP 11, STEP 12  ·  2 STEP 13, STEP 14, STEP 15, STEP 16  ·  3 ARP FREQ, ARP GATE, ARP OCTAVES, ARP ON  ·  4 ARP PATTERN, ARP SYNC, ARP TEMPO | 1 STEP 9, STEP 10, STEP 11, STEP 12  ·  2 STEP 13, STEP 14, STEP 15, STEP 16  ·  3 ARP ON, ARP PATTERN, ARP OCTAVES, ARP GATE  ·  4 ARP SYNC, ARP FREQ, ARP TEMPO |
| DISTORTION | 1 DIST DRIVE, DIST MIX, DIST ON, DIST TYPE  ·  2 DELAY MIX, DELAY FBK, DELAY FREQ, DELAY ON  ·  3 DELAY SYNC, DELAY TEMPO | 1 DIST ON, DIST TYPE, DIST DRIVE, DIST MIX  ·  2 DELAY ON, DELAY MIX, DELAY FBK  ·  3 DELAY SYNC, DELAY FREQ, DELAY TEMPO |
| REVERB | 1 REV DAMPING, REVERB MIX, REVERB FBK, REVERB ON  ·  2 STUT FREQ, STUTTER ON, RESAMP FREQ, RESAMP SYNC  ·  3 RESAMP TEMPO, STUT SOFT, STUT SYNC, STUT TEMPO | 1 REVERB ON, REVERB MIX, REVERB FBK, REV DAMPING  ·  2 STUTTER ON, STUT SOFT  ·  3 STUT SYNC, STUT FREQ, STUT TEMPO  ·  4 RESAMP SYNC, RESAMP FREQ, RESAMP TEMPO |
| PLAYING | 1 BPM, AUTO BPM, BEND RANGE, PORTAMENTO  ·  2 PORTA TYPE, VEL TRACK, MOD 1 AMT, MOD 2 AMT  ·  3 MOD 3 AMT, MOD 4 AMT, MOD 5 AMT, MOD 6 AMT  ·  4 MOD 7 AMT, MOD 8 AMT | 1 AUTO BPM, BPM, VEL TRACK, BEND RANGE  ·  2 PORTA TYPE, PORTAMENTO  ·  3 MOD 1 AMT, MOD 2 AMT, MOD 3 AMT, MOD 4 AMT  ·  4 MOD 5 AMT, MOD 6 AMT, MOD 7 AMT, MOD 8 AMT |
| MOD AMOUNTS | 1 MOD 9 AMT, MOD 10 AMT, MOD 11 AMT, MOD 12 AMT  ·  2 MOD 13 AMT, MOD 14 AMT, MOD 15 AMT, MOD 16 AMT | 1 MOD 9 AMT, MOD 10 AMT, MOD 11 AMT, MOD 12 AMT  ·  2 MOD 13 AMT, MOD 14 AMT, MOD 15 AMT, MOD 16 AMT (unchanged) |

Layout (each row of more than four split into groups with a gap, a Q-Link column each):
- **MAIN**: VOICE | FILTER | AMP ENVELOPE | PATCH (the patch last, as on Aphex). No controls moved.
- **OSC**: per oscillator, WAVE / TRANSPOSE / TUNE / VOLUME and its unison (VOICES, DETUNE, HARMONIZE).
- **OSC MIX** (the designed page): CROSS MOD + the feedback trio | OSC MIX / NOISE | the sub oscillator's four. The
  "Q-LINK ROW" tags (Q-Links go by column) and a dead "TEST OSC BUS" line went.
- **FILTER**: the filter's switch / style / shelf / blend and its drive / saturation / envelope amount / keytrack;
  FORMANT; FILTER ENV now in A D S R order (it was A D R S).
- **MOD ENV**: MOD ENV in A D S R order (it was A D R S); MONO LFO 1 as wave / amount / retrigger and sync / rate /
  tempo.
- **MONO LFO**: MONO LFO 2 and the POLY LFO the same way.
- **STEP SEQ / STEPS**: the sequencer's steps / smoothing / retrigger and sync / rate / tempo; steps in fours; the
  arpeggiator as on / pattern / octaves / gate and sync / rate / tempo.
- **DISTORTION**: DISTORTION's four; DELAY as on / mix / feedback and sync / rate / tempo.
- **REVERB**: REVERB's four; STUTTER (on, softness) | STUTTER RATE | RESAMPLE RATE as panels of their own.
- **PLAYING / MOD AMOUNTS**: tempo / velocity / bend and the glide pair; the sixteen mod amounts in fours.
- **Names**: 61 parameter names that Helm cut at 12 characters now read properly (MON LFO 1 RE → LFO1 RETRIG,
  STUTTE TEM 1 → RESAMP TEMPO, REVER DAMPIN → REV DAMPING, PIT BEN RANG → BEND RANGE...). Only the names changed:
  the parameters' order (what projects and Q-Link assignments save) is the same.

OSC MIX is made in the design (convert.py MAPS `augment` / `qlinks`); the other pages in the port's page plan
(`layout.grid.conf`); the names in MAPS `names`. A rerun keeps all three.

## Presets
Its patches are in MPC's PRESET menu (unchanged).

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).
