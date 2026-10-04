# Design QA: Verglas

Status: **passed on the device** (2026-10-04, ✅ in the README).

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; groups of up to four).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| VERGLAS | 1 POSITION, SIZE, PITCH, DENSITY  ·  2 TEXTURE, MODE, FREEZE, QUALITY  ·  3 DRY/WET, FEEDBACK, REVERB, SPREAD  ·  4 HIGH PASS, LOW PASS | 1 POSITION, SIZE, PITCH  ·  2 DENSITY, TEXTURE  ·  3 MODE, FREEZE, QUALITY  ·  4 DRY/WET, FEEDBACK, REVERB, SPREAD |
| TONE | 1 LOW BOOST, LOW FREQ, LOW Q, LIMITER  ·  2 LIM DRIVE, LIM OUTPUT | 1 LOW BOOST, LOW FREQ, LOW Q  ·  2 LIMITER, LIM DRIVE, LIM OUTPUT  ·  3 HIGH PASS, LOW PASS |

- **VERGLAS**: GRAIN has five knobs, so its column of four left TEXTURE to the next column, whose outline then took
  in GRAIN and ENGINE. Now GRAIN is two columns, POSITION / SIZE / PITCH and DENSITY / TEXTURE (Clouds' own top and
  bottom rows), with a gap between them; then ENGINE (MODE, FREEZE, QUALITY) and BLEND (DRY/WET, FEEDBACK, REVERB,
  SPREAD). The knobs sit a little higher so each outline stays inside its panel.
- **COLOR FILTER** (HIGH PASS, LOW PASS) moved to **TONE**, beside LOW SHELF and LIMITER, where it has a column of
  its own; BLEND takes its room on the main page.
- **TONE**: LOW SHELF | LIMITER | COLOR FILTER, a column each (LIMITER's switch had been in LOW SHELF's column).

VERGLAS is made in the design (convert.py MAPS `augment` / `qlinks`); TONE in the port's page plan
(`layout.grid.conf`). A rerun keeps both.

## Presets
Upstream has none, so the port brings 12 (`presets.json`, in MPC's PRESET menu): Init, Grain Cloud, Shimmer, Octave Down Haze, Ambient Wash, Stretch Time, Looper Delay, Spectral Smear, Lo-Fi Grains, Stutter, Dark Tail, Warm Tape. Each sets every control;
levels evened out to within about 3 dB with a test signal through it (the quieter ones turn on TONE's limiter for a
little make-up gain). None uses FREEZE: it would hold an empty buffer.

## For the owner
- The filter is one page further away now. If you'd rather keep it on the main page, the alternative is to take
  FREEZE (or QUALITY) off the Q-Links instead.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).
