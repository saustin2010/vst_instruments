# Design QA: Mono Voice

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MACHINE | 1 MACHINE, LFO1 DEST, LFO2 DEST, LFO3 DEST | 1 MACHINE, PATCH  ·  2 LFO1 DEST, LFO2 DEST, LFO3 DEST |
| SYNTH, AMP, FILTER, EFFECT, LFO 1-3 | four columns of four, one per row half | unchanged |

- **MACHINE**: the one column took in the whole page (the machine selector and the LFO DESTINATIONS panel). Now
  the machine is column 1 and the three LFO destinations column 2. The display moved to the left and the machine
  selector to the right. The "Q-LINK ROW 1 / ROW 2" tags went (Q-Links go by column, so they misled), and so did a
  "TEST OSC BUS" line that did nothing.
- **SYNTH, AMP, FILTER, EFFECT, LFO 1-3**: each page is two panels (the page and its SHIFT layer) of two groups of
  four, and each group is a column: the outlines already framed one group each. LFO pages have 15 controls, so
  column 4 holds three.

Made in the design (convert.py MAPS `augment` / `qlinks`) and re-converted, so a rerun keeps it.

## Presets
Its built-in patch library (Chrome Bass, Wide Current, Hollow Wire, PWM Basin, Glass Choir, Just Fifths, Arcade Lead, Dust Pulse, Scan Bell, Circuit Reed, Metal Key, Soft Operator) is now in MPC's PRESET menu after an Init, and on a new PATCH selector under MACHINE
(Q-Link column 1). That took a small patch to the plugin shell (see the README's "Changes for the MPC").

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).
