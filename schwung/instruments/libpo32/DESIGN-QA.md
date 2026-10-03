# Design QA: Libpo32

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; utility buttons in their own place and off the Q-Links).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| KIT | 1 KIT, LEVEL, DECAY SCALE, RANDOM KIT  ·  2 01: KICK, 02: SNARE, 03: CLAVE, 04: TOM  ·  3 05: HAT CL, 06: HAT OP, 07: CYMB, 08: NOISE | 1 LEVEL, DECAY SCALE  ·  2 01: KICK, 02: SNARE, 03: CLAVE, 04: TOM  ·  3 05: HAT CL, 06: HAT OP, 07: CYMB, 08: NOISE  ·  4 KIT |
| EDIT | 1 EDIT PAD, WAVE, BASE PITCH, OSC DECAY  ·  2 MOD MODE, MOD AMOUNT, NOISE FILTER, NOISE MIX  ·  3 NOISE ENV, DISTORTION, PAD LEVEL | 1 WAVE, BASE PITCH, OSC DECAY  ·  2 MOD MODE, MOD AMOUNT  ·  3 NOISE FILTER, NOISE MIX, NOISE ENV  ·  4 DISTORTION, PAD LEVEL |
| TUNE | 1 PAD1 PITCH, PAD1 DECAY, PAD2 PITCH, PAD2 DECAY  ·  2 PAD3 PITCH, PAD3 DECAY, PAD4 PITCH, PAD4 DECAY  ·  3 PAD5 PITCH, PAD5 DECAY, PAD6 PITCH, PAD6 DECAY  ·  4 PAD7 PITCH, PAD7 DECAY, PAD8 PITCH, PAD8 DECAY | 1 PAD1 PITCH, PAD1 DECAY, PAD2 PITCH, PAD2 DECAY  ·  2 PAD3 PITCH, PAD3 DECAY, PAD4 PITCH, PAD4 DECAY  ·  3 PAD5 PITCH, PAD5 DECAY, PAD6 PITCH, PAD6 DECAY  ·  4 PAD7 PITCH, PAD7 DECAY, PAD8 PITCH, PAD8 DECAY (unchanged) |

- **KIT**: MASTER (LEVEL, DECAY SCALE) | PADS 1-4 | PADS 5-8 | KIT (the kit last, as a preset). RANDOM KIT is a
  utility button: it moved into the KIT panel beside the kit display, and off the Q-Links (turning a knob fired it).
- **EDIT**: OSCILLATOR | MODULATION | NOISE / VCF | AMP & DRIVE, a column each. The EDIT PAD selector (the bar across
  the top) is touch only: as a Q-Link its column would have taken in the whole page. MOD MODE's three options are
  on one row now (the third sat on top of MOD AMOUNT), and NOISE ENV's are a row too.
- **TUNE**: unchanged: eight narrow pad strips, so each column is two pads (pitch and decay of each); the outlines
  don't overlap.
- The slider strips were rebuilt with the slider fix (the "silly" slider animation seen on Braids).

Made in the design (convert.py MAPS `augment`, with the new `move` op, and `qlinks`), so a rerun keeps it.

## Presets
Its kits are in MPC's PRESET menu (unchanged).

## For the owner
- EDIT PAD has no Q-Link; touch it to pick the pad to edit.

## Checked
Offline test PASSED; check_skin OK (two small touch overlaps between EDIT's stacked knobs, as before);
no Q-Link outlines overlap on any page (qlink_overlay.py).
