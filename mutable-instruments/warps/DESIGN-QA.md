# Design QA: Warps

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| WARPS | 1 MODE, ALGORITHM, TIMBRE, SHIFT  ·  2 CARRIER, NOTE, FINE, CARRIER LVL  ·  3 MOD LEVEL, OUTPUT, MIX, VOLUME | 1 MODE, ALGORITHM, TIMBRE, SHIFT  ·  2 CARRIER, NOTE, FINE, CARRIER LVL  ·  3 MOD LEVEL  ·  4 OUTPUT, MIX, VOLUME |

- MODULATION and CARRIER were already a column each. INPUT's MOD LEVEL shared a column with OUTPUT, so that outline
  took in both panels: now INPUT | OUTPUT, a column each.
- OUTPUT's OUT / OUT + AUX selector moved from the panel's title bar to just under it, so OUTPUT's outline no longer
  reaches up into MODULATION. Its made-up meters (they never moved) went.

Made in the design (convert.py MAPS `augment` / `qlinks` / `nudge`), so a rerun keeps it.

## Presets
None upstream. See the batch's presets pass.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
