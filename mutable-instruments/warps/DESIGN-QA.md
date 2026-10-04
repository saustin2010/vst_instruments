# Design QA: Warps

Status: **passed on the device** (2026-10-04, ✅ in the README).

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
Upstream has none, so the port brings 12 (`presets.json`, in MPC's PRESET menu): Init, Ring Mod, Parallel Ring, Digital Ring, Cross Fold, XOR Crush, Comparator Grit, Robot Vocoder, Pulse Vocoder, Shift Up, Shift Down, Stereo Shift. Each sets every control;
levels evened out with a test signal through it (all within about 1 dB, Init a little louder at its defaults).

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
