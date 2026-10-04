# Design QA: Noisemaker

Status: **passed on the device** (2026-10-04, ✅ in the README).

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

## What changed
Every page had its Q-Links in plain page order, so most columns ran across two or three panels (on OSC, column 4
was OSC2 PHASE + OSC1 WAVE + OSC SYNC + OSC2 WAVE, from three corners of the page).

Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 CUTOFF, RESONANCE, FILTER ENV, VOLUME  ·  2 VOICES, PORTAMENTO, PORTA MODE, AMP ATTACK  ·  3 AMP DECAY, AMP SUSTAIN, AMP RELEASE, PATCH  ·  4 FILTER TYPE, BANK | 1 FILTER TYPE, CUTOFF, RESONANCE, FILTER ENV  ·  2 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  3 VOLUME, VOICES, PORTAMENTO, PORTA MODE  ·  4 PATCH, BANK |
| OSC | 1 OSC1 TUNE, OSC1 FINE, OSC1 PW, OSC2 TUNE  ·  2 OSC2 FINE, OSC2 FM, OSC1 LEVEL, OSC2 LEVEL  ·  3 SUB LEVEL, RING MOD, MASTER TUNE, OSC1 PHASE  ·  4 OSC2 PHASE, OSC1 WAVE, OSC SYNC, OSC2 WAVE | 1 OSC1 WAVE, OSC1 TUNE, OSC1 FINE, OSC1 PW  ·  2 OSC2 WAVE, OSC2 TUNE, OSC2 FINE, OSC2 FM  ·  3 OSC1 LEVEL, OSC2 LEVEL, SUB LEVEL, RING MOD  ·  4 OSC SYNC, MASTER TUNE, OSC1 PHASE, OSC2 PHASE |
| FILTER | 1 KEY TRACK, DRIVE, HIGH PASS, VEL CUTOFF  ·  2 DETUNE, VINTAGE, BITCRUSH, FLT TIME  ·  3 AMP TIME, VEL VOLUME, VEL ENV, FLT ATTACK  ·  4 FLT DECAY, FLT SUSTAIN, FLT RELEASE | 1 KEY TRACK, DRIVE, HIGH PASS, VEL CUTOFF  ·  2 DETUNE, VINTAGE, BITCRUSH  ·  3 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  4 FLT TIME, AMP TIME, VEL VOLUME, VEL ENV |
| LFO | 1 LFO1 RATE, LFO1 AMOUNT, LFO1 PHASE, LFO2 RATE  ·  2 LFO2 AMOUNT, LFO2 PHASE, LFO1 SYNC, LFO1 KEYTRIG  ·  3 LFO2 SYNC, LFO2 KEYTRIG, LFO1 WAVE, LFO1 DEST  ·  4 LFO2 WAVE, LFO2 DEST | 1 LFO1 WAVE, LFO1 RATE, LFO1 AMOUNT, LFO1 DEST  ·  2 LFO1 SYNC, LFO1 KEYTRIG, LFO1 PHASE  ·  3 LFO2 WAVE, LFO2 RATE, LFO2 AMOUNT, LFO2 DEST  ·  4 LFO2 SYNC, LFO2 KEYTRIG, LFO2 PHASE |
| MOD | 1 ENV3 ATTACK, ENV3 DECAY, ENV3 AMOUNT, DRAW AMOUNT  ·  2 WHEEL CUTOFF, BEND RANGE, CHORUS I, CHORUS II  ·  3 ENV3 DEST, DRAW SPEED, DRAW DEST | 1 ENV3 ATTACK, ENV3 DECAY, ENV3 AMOUNT, ENV3 DEST  ·  2 DRAW AMOUNT, DRAW SPEED, DRAW DEST  ·  3 WHEEL CUTOFF, BEND RANGE  ·  4 CHORUS I, CHORUS II |
| FX | 1 REVERB WET, REV DECAY, REV PREDELAY, REV HI CUT  ·  2 REV LO CUT, DELAY WET, DELAY TIME, DLY FEEDBACK  ·  3 DLY HI CUT, DLY LO CUT, DELAY SYNC, DELAY 2X L  ·  4 DELAY 2X R | 1 REVERB WET, REV DECAY, REV PREDELAY  ·  2 REV HI CUT, REV LO CUT  ·  3 DELAY WET, DLY FEEDBACK, DLY HI CUT, DLY LO CUT  ·  4 DELAY TIME, DELAY SYNC, DELAY 2X L, DELAY 2X R |

Layout:
- **MAIN**: FILTER | AMP ENVELOPE | VOICE & MASTER | PATCH + BANK (the patch last, as on Aphex and MonkSynth).
  No controls moved; the curve and scope displays were already on the left.
- **OSC**: RING MOD moved into MIXER GAIN (it's a level in the mix), so the four panels have four controls each.
- **FILTER**: a column per panel as drawn.
- **LFO**: each LFO split into WAVE / RATE / AMOUNT / DEST and SYNC / KEYTRIG / PHASE (they already sat apart).
- **MOD**: ENVELOPE 3 | ENVELOPE DRAW | WHEEL / BEND | CHORUS.
- **FX**: reverb split into WET / DECAY / PREDELAY and its HI / LO CUT; delay into level and tone (WET, FEEDBACK,
  HI CUT, LO CUT) and time (TIME, SYNC, 2X L, 2X R), with the controls moved to match.
- The slider strips were rebuilt with the slider fix (the "silly" slider animation seen on Braids).

Noisemaker's screen is a hand-edited `layout.conf` (a rerun of convert.py doesn't reproduce it; see its header), so
these changes are in `layout.conf` too.

## Presets
Its factory presets and any banks you add are in MPC's PRESET menu (unchanged).

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).
