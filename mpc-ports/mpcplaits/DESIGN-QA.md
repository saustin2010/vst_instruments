# Design QA: MPC Plaits

Status: **checked offline** (2026-10-05); not yet on the device (☐ in the README).

## Owner's notes
Added on request, "check the interfaces so that the qlinks work for all pages". Checked against the batch rules (one
Q-Link column per panel, as Moog; outlines that frame one panel). The screen and its artwork are poloq's, unchanged;
only Q-Link maps changed.

## What changed
Five of the six pages already had one column per panel (VOICE, ENV, LFO) or per matrix column (MOD, ASSIGN). The
PLAITS page didn't: its first column was the four big knobs, which sit in the panel's corners, so its outline was the
whole page, around the model selector and attenuverters of column 2.

Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| PLAITS | 1 FREQUENCY, HARMONICS, TIMBRE, MORPH  ·  2 MODEL, TIMBRE ATT, FM ATT, MORPH ATT  ·  3 OUT / AUX | 1 FREQUENCY, TIMBRE  ·  2 MODEL, TIMBRE ATT, FM ATT, MORPH ATT  ·  3 HARMONICS, MORPH  ·  4 OUT / AUX |
| VOICE | 1 LPG COLOR, DECAY, TRIG RATE  ·  2 NOTES ...  ·  3 GLIDE ...  ·  4 CUTOFF ... | 1 AMP, LPG COLOR, DECAY, TRIG RATE  ·  2-4 unchanged |
| ENV | unchanged: 1 ENV 1 ADSR  ·  2 ENV 2 ADSR | |
| LFO | 1 LFO1 RATE, DIV, PHASE  ·  2 LFO2 RATE, DIV, PHASE  ·  3 RISE, FALL  ·  4 RND RATE, RND DIV, SLEW | 1 LFO1 SHAPE, RATE, DIV, PHASE  ·  2 LFO2 SHAPE, RATE, DIV, PHASE  ·  3 CYC SHAPE, RISE, FALL  ·  4 RND MODE, RND RATE, RND DIV, SLEW |
| MOD, ASSIGN | unchanged: one column per matrix column (destination), sources top to bottom | |

The cost on PLAITS: the four macro knobs are on two columns (1 and 3), not one, because the artwork puts them in the
corners; moving them would mean rebuilding poloq's panel art. The other way round (upstream's map) is one line in
`layout.conf` if the outline matters less than having the four together.

On LFO, RATE and DIV share a place on the screen (SYNC picks which shows); both stay on the Q-Links, as upstream had
them, so the knob for the hidden one still turns it.

Touch boxes (the skin generator here makes a knob's box 130 px and a switch's 120 px, with its name under it): on MOD
and ASSIGN each knob's box ran 41 px into its on/off LED's, and on LFO the LED switches (80-100 px apart) into each
other's and DIV into PHASE. Narrowed, positions unchanged: the matrix knobs to 84 px (the width poloq's own skin gives
them) and their LEDs to 84, the LFO LEDs to their spacing (80 or 100), PHASE and SLEW to 96, the DIV pop-ups 150 → 136
px. (`bw=` on a picture switch is new in the framework for this.)

Checked offline: `check_skin.py` (bindings, Q-Links, touch boxes, options) and `qlink_overlay.py` (no two column
outlines overlap on any page).

## To check on the device
- Synced LFOs restarting with MPC's transport (`HAS_TRANSPORT`, new in this repo's framework, from poloq's fork).
- The model buttons and the model list; MPC's sidebar over HARMONICS and MORPH while a Q-Link is touched (it shows their
  values).
- The 31 presets (2026-10-05, levels evened out offline): how they sound on the Live II.
