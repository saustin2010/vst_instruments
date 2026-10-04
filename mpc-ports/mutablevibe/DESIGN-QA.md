# Design QA: Mutable Vibe

Status: **checked offline** (2026-10-05); not yet on the device (☐ in the README).

## Owner's notes
Added on request, "check the interfaces so that the qlinks work for all pages". Checked against the batch rules (one
Q-Link column per panel, as Moog; outlines that frame one panel; a small panel shares a column with its neighbour).
The screen is nachtaktiv303's; only its Q-Links, the LFO rows and four names changed.

## What changed
The author's Q-Links ran in plain page order, so columns crossed panels on five of the seven Q-Link pages (OVERVIEW,
ENV 3-4, both LFO pages, EFFECTS), and POLY, SLOPE and the sync switches were on none.

Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| OVERVIEW | 1 MODEL, STRUCTURE, BRIGHTNESS, DAMPING  ·  2 POSITION, LPG DECAY, OCTAVE, CUTOFF  ·  3 RESONANCE, FILTER ENV, REVERB, DELAY  ·  4 CHORUS, DRIVE | 1 MODEL, STRUCTURE, BRIGHTNESS, DAMPING  ·  2 POSITION, LPG DECAY, POLYPHONY, OCTAVE  ·  3 SLOPE, CUTOFF, RESONANCE, FILTER ENV  ·  4 REVERB AMT, DELAY AMT, CHORUS AMT, DRIVE |
| ENV 1-2 | unchanged: 1 AMP ENV  ·  2 FILTER ENV  ·  3 FILTER ENV amount | |
| ENV 3-4 | 1 ENV 3 ADSR  ·  2 ENV 3 DEST, AMOUNT, ENV 4 ATTACK, DECAY  ·  3 ENV 4 SUSTAIN, RELEASE, DEST, AMOUNT | 1 ENV 3 ADSR  ·  2 ENV 3 DEST, AMOUNT  ·  3 ENV 4 ADSR  ·  4 ENV 4 DEST, AMOUNT |
| LFO 1-2 | 1 LFO1 RATE, SHAPE, AMOUNT, DIV  ·  2 LFO1 WAVE, DEST, LFO2 RATE, SHAPE  ·  3 LFO2 AMOUNT, DIV, WAVE, DEST | 1 LFO1 RATE, SHAPE, AMOUNT, POLARITY  ·  2 LFO1 SYNC, DIV, WAVE, DEST  ·  3 LFO2 RATE, SHAPE, AMOUNT, POLARITY  ·  4 LFO2 SYNC, DIV, WAVE, DEST |
| LFO 3-4 | as LFO 1-2, for LFO 3 and 4 | as LFO 1-2, for LFO 3 and 4 |
| EFFECTS | 1 REVERB DECAY, DAMPING, HI-PASS, AMOUNT  ·  2 CHORUS RATE, DEPTH, AMOUNT, DELAY TIME  ·  3 DELAY FEEDBACK, TONE, AMOUNT, DIV  ·  4 DRIVE | 1 REVERB DECAY, DAMPING, HI-PASS, AMT  ·  2 CHORUS RATE, DEPTH, AMT  ·  3 DELAY TIME, FEEDBACK, TONE, AMT  ·  4 DELAY SYNC, DIV, DRIVE, VOLUME |
| MOD | 1 VEL1 DEST, AMT, VEL2 DEST, AMT  ·  2 MW DEST, AMT, AT DEST, AT AMT | 1 VEL1 DEST, AMT  ·  2 VEL2 DEST, AMT  ·  3 MW DEST, AMT  ·  4 AT DEST, AMT |

Layout:
- **LFOS**: SYNC and POLARITY were stacked to the right of the knobs, so any split of an LFO's eight controls into two
  columns overlapped. POLARITY moved up into the knob row, SYNC down beside DIVISION (DIVISION and WAVE a little
  narrower): each LFO is two rows, a column each.
- **EFFECTS**: the DRIVE panel became OUTPUT, DRIVE and the new VOLUME (2026-10-05). It shares the fourth column with
  DELAY's SYNC and DIVISION beside it, as the rules allow a small panel; the outline takes in the right of the DELAY
  panel and OUTPUT, and nothing of another column.
- Names: the four effect levels were all AMOUNT (MPC shows the parameter's name, so OVERVIEW read AMOUNT four times):
  REVERB AMT, DELAY AMT, CHORUS AMT, DRIVE. LPG DECAY's fixed name was blank; the engine still leaves it unnamed on
  the Rings models, where it does nothing.
- Touch boxes: the envelope sliders sit 80 px apart, inside each other's 130 px touch boxes (`bw=78` now); MODEL's
  pop-up and STRUCTURE overlapped by 15 px. On LFOS the knob row moved up 10 px and the bottom row down 20 px, and on
  ENV 3-4 DESTINATION and AMOUNT moved 12 px right, so no column's outline touches another's.

Checked offline: `check_skin.py` (bindings, Q-Links, touch boxes, options) and `qlink_overlay.py` (no two column
outlines overlap on any page).

## To check on the device
- The macro knobs' names following the model (STRUCTURE → HARMONICS on a `P:` model): MPC re-reads a name on
  `audioMasterUpdateDisplay`, verified for labels upstream, not yet for this plugin.
- The right of the screen (POLY, OCTAVE, CHORUS AMT, DRIVE and the right-hand LFO and ENV panels) sits under MPC's Q-Link
  sidebar while a Q-Link is touched; the sidebar shows their values.
- The 24 presets (2026-10-05, levels evened out offline): how they sound on the Live II.
