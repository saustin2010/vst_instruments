# Design QA: Mr Hyde

Status: **passed on the device** (2026-10-04, ✅ in the README).

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right; groups of up to four, 3-2-4-3 style where a group is smaller).

## What changed
Every page had its Q-Links in plain page order, so columns ran across panels: on MAIN, column 2 was MORPH + the LPG
+ FILTER MODE; on the modulation pages, rows of six spilled into the next row.

Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 MODEL, PITCH, HARMONICS, TIMBRE  ·  2 MORPH, LPG DECAY, LPG COLOR, FILTER MODE  ·  3 CUTOFF FREQ, RESONANCE, FM AMOUNT, AUX MIX  ·  4 VOLUME, PAN | 1 MODEL, FM AMOUNT, AUX MIX  ·  2 PITCH, HARMONICS, TIMBRE, MORPH  ·  3 FILTER MODE, CUTOFF FREQ, RESONANCE  ·  4 LPG DECAY, LPG COLOR, VOLUME, PAN |
| LFO ENV | 1 LFO SHAPE, LFO RATE, LFO SYNC, LFO RETRIG  ·  2 LFO PHASE, VEL CURVE, AT CURVE, ENV ATTACK  ·  3 ENV DECAY, ENV SUSTAIN, ENV RELEASE, ENV RETRIG | 1 LFO SHAPE, LFO RATE, LFO PHASE, LFO SYNC  ·  2 VEL CURVE, AT CURVE  ·  3 ENV ATTACK, ENV DECAY, ENV SUSTAIN, ENV RELEASE  ·  4 LFO RETRIG, ENV RETRIG |
| CYC RAND | 1 CYC ATTACK, CYC DECAY, CYC SHAPE, CYC SYNC  ·  2 CYC RETRIG, CYC BIPOLAR, RND MODE, RND RATE  ·  3 RND SYNC, RND SLEW, RND RETRIG | 1 CYC ATTACK, CYC DECAY, CYC SHAPE  ·  2 CYC SYNC, CYC RETRIG, CYC BIPOLAR  ·  3 RND MODE, RND RATE, RND SLEW  ·  4 RND SYNC, RND RETRIG |
| ASSIGN | 1 A1 TARGET, A1 LFO, A1 ENV, A1 CYCLE  ·  2 A1 RANDOM, A1 VEL, A1 AT, A2 TARGET  ·  3 A2 LFO, A2 ENV, A2 CYCLE, A2 RANDOM  ·  4 A2 VEL, A2 AT | 1 A1 TARGET, A1 LFO, A1 ENV, A1 CYCLE  ·  2 A1 RANDOM, A1 VEL, A1 AT  ·  3 A2 TARGET, A2 LFO, A2 ENV, A2 CYCLE  ·  4 A2 RANDOM, A2 VEL, A2 AT |
| PITCH HARM | 1 PITCH LFO, PITCH ENV, PITCH CYCLE, PITCH RANDOM  ·  2 PITCH VEL, PITCH AT, HARM LFO, HARM ENV  ·  3 HARM CYCLE, HARM RANDOM, HARM VEL, HARM AT | 1 PITCH LFO, PITCH ENV, PITCH CYCLE  ·  2 PITCH RANDOM, PITCH VEL, PITCH AT  ·  3 HARM LFO, HARM ENV, HARM CYCLE  ·  4 HARM RANDOM, HARM VEL, HARM AT |
| TIMB CUT | 1 TIMB LFO, TIMB ENV, TIMB CYCLE, TIMB RANDOM  ·  2 TIMB VEL, TIMB AT, CUT LFO, CUT ENV  ·  3 CUT CYCLE, CUT RANDOM, CUT VEL, CUT AT | 1 TIMB LFO, TIMB ENV, TIMB CYCLE  ·  2 TIMB RANDOM, TIMB VEL, TIMB AT  ·  3 CUT LFO, CUT ENV, CUT CYCLE  ·  4 CUT RANDOM, CUT VEL, CUT AT |
| VOICE | 1 VOICE MODE, POLYPHONY, UNISON, DETUNE  ·  2 SPREAD, GLIDE | 1 VOICE MODE, POLYPHONY, GLIDE  ·  2 UNISON, DETUNE, SPREAD |

Layout:
- **MAIN**: five panels became four, one per Q-Link column, in a 2 × 2 grid as Moog's MAIN:
  - **PLAITS ENGINE** (top left): the model scope on the left, MODEL, FM AMOUNT and AUX MIX on the right (FM and AUX
    are the Plaits engine's own inputs).
  - **OSCILLATOR** (top right): PITCH, HARMONICS, TIMBRE, MORPH.
  - **FILTER** (bottom left): unchanged.
  - **LPG / OUTPUT** (bottom right): the LPG's two knobs and MASTER OUT's VOLUME and PAN in one row. The made-up
    stereo meter went.
- **LFO ENV**: LFO | CURVES on top, ENVELOPE | RETRIG below. The two RETRIG switches share a small panel, so the
  LFO and the envelope are four controls each.
- **CYC RAND**: each half split into its settings (CYCLE ENVELOPE, RANDOM) and its switches (CYCLE OPTIONS,
  RANDOM OPTIONS).
- **ASSIGN, PITCH HARM, TIMB CUT**: each row of six sources split into two groups with a gap: LFO / ENV / CYCLE
  (with ASSIGN's target in front) | RANDOM / VEL / AT.
- **VOICE**: VOICE (mode, polyphony, glide) | UNISON (unison, detune, spread), and a MR HYDE wordmark fills the
  empty lower half.

MAIN is made in the design (convert.py MAPS `augment`, using a new `move` op in extract.py that moves the design's
own controls between panels); the other pages in the port's page plan (`layout.grid.conf`). A rerun keeps both.

## Presets
Upstream has none, so the port brings 19 (`presets.json`, in MPC's PRESET menu): Init, Freak Bass, Wobble Bass, Sync Lead, Terrain Pad, String Machine, Supersaw Stack, Folded Pluck, FM Keys, Formant Choir, Drawbar Organ, Wavetable Sweep, Chord Memory, Speech Synth, Swarm, Noise Sweep, Particle Rain, Glass String, Modal Bells. Each sets all 81 controls;
offline all 19 play. VOLUME drives the low-pass gate (it saturates above about 1.3), so it evens the levels out only so far:
the organ, string machine, inharmonic string and particle presets stay quieter by nature.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).
