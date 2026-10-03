# Design QA: Aphex

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
- Expose the presets in the PRESET menu.
- The patch controls (random, mutate, gate, reset) should all be in their own box.
- Some confusion with the Q-Links; check them.
- Align the Q-Links on the VCO page better: not grouped well. Same with MIXER.
- ENVELOPES is good, but the curve should be on the left and the controls on the right.
- The controls on ESP need adjusting.

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | PRESET, RANDOM, MUTATE, LPF CUTOFF · LPF PEAK, HPF CUTOFF, HPF PEAK, MG FREQ · MG DEPTH, VOLUME, OCTAVE, PORTAMENTO · TUNE, DRIVE, TRIGGER, RESET | LPF CUTOFF, LPF PEAK, HPF CUTOFF, HPF PEAK · MG FREQ, MG DEPTH, VOLUME · OCTAVE, PORTAMENTO, TUNE, DRIVE · PRESET |
| VCO | VCO1 SCALE, VCO1 WAVE, VCO1 PW, V1 DRIFT · MG/T.EXT, VCO2 SCALE, VCO2 WAVE, VCO2 PITCH · VCO2 DETUNE, EG1/EXT, V2 DRIFT, VCO2 SYNC · VCO2 FM | VCO1 SCALE, VCO1 WAVE · VCO1 PW, V1 DRIFT, MG/T.EXT · VCO2 SCALE, VCO2 WAVE, VCO2 SYNC, VCO2 FM · VCO2 PITCH, VCO2 DETUNE, EG1/EXT, V2 DRIFT |
| MIXER | VCO1..NOISE LEVEL · NOISE COLOR, ESP, FEEDBACK, HPF MG · HPF EG, LPF MG, LPF EG, FILTER MODE · FILTER REV | VCO1..NOISE LEVEL · NOISE COLOR, ESP LEVEL, FEEDBACK · HPF MG, HPF EG, LPF MG, LPF EG · FILTER MODE, FILTER REV |
| ENVELOPES | E1 DELAY, E1 ATK, E1 REL, E2 HOLD · E2 ATK..REL · MG SHAPE, MG PW | E1 DELAY, E1 ATK, E1 REL · E2 ATK, E2 DCY, E2 SUS, E2 REL · E2 HOLD · MG SHAPE, MG PW |

Layout:
- **MAIN**: RANDOM, MUTATE, GATE and RESET together in a new PATCH ACTIONS box under the preset (the made-up "engine
  status" box went), and off the Q-Links: turning a knob fired a button, likely the "confusion".
- **VCO**: each oscillator's selectors on top and its knobs at the bottom; VCO2's SYNC / FM sit with its selectors,
  so no column crosses another (the made-up status box under VCO1 went).
- **MIXER**: 40 px more between the rows, so each row's highlight stays clear of the next.
- **ENVELOPES**: the curve panel on the left, the knobs on the right. EG2 has five controls, so HOLD has a column
  of its own rather than spilling into another group.
- **MODERN**: removed a heading over a switch with no parameter (SUPER-SAW STACK); "EMULATION EMULATION MODEL"
  became "EMULATION MODEL".
- **PATCHBAY**, **ESP**: already one column per row; unchanged.

All made in the design (convert.py MAPS `augment` / `qlinks`) and re-converted, so a rerun keeps them.

## Presets
41 built-in presets, in MPC's PRESET menu (VST programs).

## For the owner
- ESP: its Q-Links were already clean. Say what to change there (spacing, the empty lower half, the AUD MIX row).

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).
