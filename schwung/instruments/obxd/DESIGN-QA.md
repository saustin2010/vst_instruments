# Design QA: OB-Xd

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right). Known before: MAIN's and ENVELOPES' outlines overlapped.

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 PATCH, VOLUME, TUNE, VOICES  ·  2 SPREAD, UNISON, AS PLAYED, LEGATO  ·  3 PORTAMENTO, BEND 12, BEND OSC2, BANK | 1 VOLUME, TUNE  ·  2 VOICES, SPREAD, UNISON, AS PLAYED  ·  3 PORTAMENTO, LEGATO, BEND 12, BEND OSC2  ·  4 PATCH, BANK |
| OSCILLATORS | 1 OSC1 PITCH, OSC1 SAW, OSC1 PULSE, OSC2 PITCH  ·  2 OSC2 DETUNE, OSC2 SAW, OSC2 PULSE, OSC2 SYNC  ·  3 PULSE WIDTH, PW OFFSET, PW ENV, PW ENV BOTH  ·  4 X-MOD, BRIGHTNESS, OSC2 STEP | 1 OSC1 PITCH, OSC1 SAW, OSC1 PULSE  ·  2 OSC2 PITCH, OSC2 DETUNE, OSC2 SAW, OSC2 PULSE  ·  3 PULSE WIDTH, PW OFFSET, PW ENV, PW ENV BOTH  ·  4 X-MOD, BRIGHTNESS, OSC2 SYNC, OSC2 STEP |
| FILTER | 1 OSC1 LEVEL, OSC2 LEVEL, NOISE, CUTOFF  ·  2 RESONANCE, ENV AMOUNT, KEY TRACK, MULTIMODE  ·  3 BANDPASS, 24 dB, SELF OSC, ENV INVERT  ·  4 FILTER VAR, GLIDE VAR, ENV VAR, LEVEL VAR | 1 OSC1 LEVEL, OSC2 LEVEL, NOISE  ·  2 CUTOFF, RESONANCE, ENV AMOUNT, KEY TRACK  ·  3 MULTIMODE, BANDPASS, 24 dB, SELF OSC  ·  4 FILTER VAR, GLIDE VAR, ENV VAR, LEVEL VAR |
| ENVELOPES | 1 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  2 FLT VEL, AMP ATTACK, AMP DECAY, AMP SUSTAIN  ·  3 AMP RELEASE, AMP VEL | 1 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  2 FLT VEL, ENV INVERT  ·  3 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  4 AMP VEL |
| MODULATION | 1 LFO RATE, LFO SYNC, LFO SINE, LFO SQUARE  ·  2 LFO S&H, PITCH ENV, P.ENV BOTH, VIBRATO  ·  3 MOD AMOUNT, MOD OSC1, MOD OSC2, MOD FILTER  ·  4 PWM AMOUNT, PWM OSC1, PWM OSC2 | 1 LFO RATE, LFO SINE, LFO SQUARE, LFO S&H  ·  2 PITCH ENV, P.ENV BOTH, VIBRATO  ·  3 MOD AMOUNT, MOD OSC1, MOD OSC2, MOD FILTER  ·  4 PWM AMOUNT, PWM OSC1, PWM OSC2 |

Layout:
- **MAIN**: MASTER | VOICE & POLYPHONY | GLIDE // BEND | PATCH + BANK (the patch last, as on Aphex and MonkSynth).
  LEGATO MODE moved from VOICE & POLYPHONY into GLIDE // BEND, so VOICE has four controls (VOICES, SPREAD, UNISON,
  AS PLAYED) and GLIDE four (PORTAMENTO, LEGATO, BEND 12, BEND OSC2); the bottom row is now two equal panels like
  the top. The made-up OUTPUT PEAK meter (it never moved) went.
- **OSCILLATORS**: OSC2 SYNC moved from OSC 2 into OSC MOD (with X-MOD, the other oscillator-to-oscillator
  control), so OSC 2 and OSC MOD have four each.
- **FILTER**: MULTIMODE moved into FILTER MODE (with BANDPASS, 24 dB, SELF OSC); ENV INVERT moved to ENVELOPES.
- **ENVELOPES**: each envelope's A / D / S / R is one column and its velocity another; ENV INVERT (it flips the
  filter envelope) sits with FLT VEL.
- **MODULATION**: 15 controls, so one had to be touch only: **LFO SYNC** (it's set once per patch). It sits to the
  right of the LFO's column (RATE, SINE, SQUARE, S&H) so that column's outline doesn't take it in.

MAIN is made in the design (convert.py MAPS `augment`, with the new `move` op); the other pages in the port's page
plan (`layout.grid.conf`). A rerun keeps both.

## Presets
Factory patches and any .fxb banks you add are in MPC's PRESET menu, with BANK on MAIN (unchanged).

## For the owner
- LFO SYNC is the one OB-Xd control without a Q-Link. If you'd rather lose a different one (P.ENV BOTH, say), it's a
  one-line change.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).
