# Design QA: Tablor

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right; groups of up to four, 3-2-4-3 style where a group is smaller).

## What changed
Every page had its Q-Links in plain page order, so on all but VOICE the columns ran from one panel into the next.

Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 WT1 TABLE, WT1 POSITION, WT1 LEVEL, WT1 TUNE  ·  2 WT1 UNISON, WT2 TABLE, WT2 POSITION, WT2 LEVEL  ·  3 WT2 TUNE, WT2 UNISON | 1 WT1 TABLE  ·  2 WT1 POSITION, WT1 LEVEL, WT1 TUNE, WT1 UNISON  ·  3 WT2 TABLE  ·  4 WT2 POSITION, WT2 LEVEL, WT2 TUNE, WT2 UNISON |
| FILTER | 1 CUTOFF, RESONANCE, FILTER TYPE, FILTER ENV  ·  2 KEY TRACK, VEL TRACK, SUB LEVEL, SUB WAVE  ·  3 SUB TUNE, NOISE LEVEL, NOISE TYPE | 1 CUTOFF, RESONANCE, FILTER TYPE  ·  2 FILTER ENV, KEY TRACK, VEL TRACK  ·  3 SUB LEVEL, SUB WAVE, SUB TUNE  ·  4 NOISE LEVEL, NOISE TYPE |
| SHAPE | 1 WT1 DETUNE, WT1 SPREAD, WT1 PAN, WT2 DETUNE  ·  2 WT2 SPREAD, WT2 PAN, WT1 BEND, WT1 FORMANT  ·  3 WT2 BEND, WT2 FORMANT | 1 WT1 DETUNE, WT1 SPREAD, WT1 PAN  ·  2 WT2 DETUNE, WT2 SPREAD, WT2 PAN  ·  3 WT1 BEND, WT1 FORMANT  ·  4 WT2 BEND, WT2 FORMANT |
| ENVELOPES | 1 VCA ATTACK, VCA DECAY, VCA SUSTAIN, VCA RELEASE  ·  2 VELOCITY, FLT ATTACK, FLT DECAY, FLT SUSTAIN  ·  3 FLT RELEASE | 1 VCA ATTACK, VCA DECAY, VCA SUSTAIN, VCA RELEASE  ·  2 VELOCITY  ·  3 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE |
| MOD ENVS | 1 EG1 A, EG1 D, EG1 S, EG1 R  ·  2 EG1 DST, EG1 AMT, EG2 A, EG2 D  ·  3 EG2 S, EG2 R, EG2 DST, EG2 AMT | 1 EG1 A, EG1 D, EG1 S, EG1 R  ·  2 EG1 DST, EG1 AMT  ·  3 EG2 A, EG2 D, EG2 S, EG2 R  ·  4 EG2 DST, EG2 AMT |
| VOICE | 1 VOICE MODE, VOICES, GLIDE, GLIDE MODE  ·  2 LEGATO, BEND RANGE, VOLUME | 1 VOICE MODE, VOICES, LEGATO  ·  2 GLIDE, GLIDE MODE  ·  3 BEND RANGE, VOLUME  ·  4 PRESET |

Layout:
- **MAIN**: each oscillator's TABLE (with its display under it, on the left) is a column, and its four knobs (on the
  right) another. No controls moved.
- **FILTER**: CUTOFF / RESONANCE / FILTER TYPE and FILTER ENV / KEY TRACK / VEL TRACK as two groups with a gap; SUB
  / NOISE split into a SUB panel and a NOISE panel.
- **SHAPE**: a panel per oscillator and job: WT1 UNISON | WT2 UNISON / WT1 SHAPE | WT2 SHAPE.
- **ENVELOPES**: AMP ENV's four and its VELOCITY in panels of their own; FILTER ENV's four; a TABLOR wordmark in the
  empty corner.
- **MOD ENVS**: per envelope, A / D / S / R and DST / AMT as two groups with a gap.
- **VOICE**: VOICE (mode, voices, legato) | GLIDE (glide, glide mode) | MASTER (bend range, volume), and (2026-10-04) a
  PRESET panel in the lower half.

MAIN is made in the design (convert.py MAPS `qlinks`); the other pages in the port's page plan (`layout.grid.conf`,
plus an `art` plate for the wordmark). A rerun keeps both.

## Presets
Its 9 factory presets (upstream's `factory.tbl`: Init, First Contact, Neu Bass, Formant Keys, Dust Pad, Sub Punch, Glass Bells, Res Bass, E Piano) are now in MPC's PRESET menu, and on the VOICE page's new
PRESET panel (Q-Link column 4), which takes the lower half where the wordmark was. That took a small engine patch (see
the README's "Changes for the MPC").

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).
