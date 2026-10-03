# Design QA: MIDI Player

Status: done offline (2026-10-03), **waiting for the device check**.

## Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel).

## What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| PLAYER | 1 FILE, TRACK, LOOP | 1 TRACK, LOOP  ·  2 FILE |

- The one column (FILE, TRACK, LOOP) took in the FILE panel and PLAYBACK ENGINE. Now PLAYBACK ENGINE (TRACK,
  LOOP) | FILE, a column each (the file last, as a preset).

Made in the design (convert.py MAPS `qlinks`), so a rerun keeps it.

## Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).
