# Design QA: Moog

Status: **passed on the device** before this batch (MAIN is the batch's Q-Link reference); one fix since
(2026-10-03), waiting for the device check.

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 CUTOFF, EMPHASIS, CONTOUR, KEY TRACK  ·  2 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  3 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  4 PATCH | 1 CUTOFF, EMPHASIS, CONTOUR, KEY TRACK  ·  2 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  3 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  4 PATCH (unchanged) |
| OSCILLATORS | 1 NOISE, OSC1 WAVE, OSC1 RANGE, OSC1 LEVEL  ·  2 OSC2 WAVE, OSC2 RANGE, OSC2 DETUNE, OSC2 LEVEL  ·  3 OSC3 WAVE, OSC3 RANGE, OSC3 DETUNE, OSC3 LEVEL  ·  4 OSC4 WAVE, OSC4 RANGE, OSC4 DETUNE, OSC4 LEVEL | 1 NOISE, OSC1 WAVE, OSC1 RANGE, OSC1 LEVEL  ·  2 OSC2 WAVE, OSC2 RANGE, OSC2 DETUNE, OSC2 LEVEL  ·  3 OSC3 WAVE, OSC3 RANGE, OSC3 DETUNE, OSC3 LEVEL  ·  4 OSC4 WAVE, OSC4 RANGE, OSC4 DETUNE, OSC4 LEVEL (unchanged) |
| MODULATION | 1 LFO RATE, LFO PITCH, LFO FILTER, WHEEL FILTER  ·  2 WHEEL PITCH, GLIDE, BEND RANGE, VELOCITY  ·  3 VOLUME | 1 LFO RATE, LFO PITCH, LFO FILTER  ·  2 WHEEL FILTER, WHEEL PITCH  ·  3 GLIDE, BEND RANGE, VELOCITY  ·  4 VOLUME |

- **MODULATION**: four panels of 3, 2, 3 and 1 controls, but the Q-Links ran in fours, so column 1's outline took in
  the LFO and half of MOD WHEEL. Now LFO | MOD WHEEL | PLAYING | OUTPUT, a column each (the 3-2-4-3 kind of layout).
- **MAIN** and **OSCILLATORS**: unchanged (the reference).

Moog's screen doesn't reproduce exactly from a rerun of convert.py, so the change is made in `layout.conf` by hand
(see its header) and also written in convert.py MAPS `qlinks`.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page now (qlink_overlay.py).
