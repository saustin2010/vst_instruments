# Design QA: 303

Status: **passed on the device** (2026-10-03, ✅ in the README).

## Owner's notes
- Align the knobs with the Q-Links.
- Are there presets? Expose them in the PRESET menu.
- The DEVILFISH button is a bit sticky.
- The scope/waveform is static.

## What changed
**303 MAIN**, Q-Link columns (one per panel; "-" = an empty slot):

| | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| before | WAVEFORM, TUNING, CUTOFF, RESONANCE | ENV MOD, DECAY, ACCENT, VOLUME | DRIVE MODEL, DRIVE, DRIVE MIX, SHAPER DRIVE | - |
| after | WAVEFORM, TUNING (VCO) | CUTOFF, RESONANCE, ENV MOD, DECAY (VCF) | ACCENT, VOLUME | DRIVE MODEL, DRIVE, DRIVE MIX, SHAPER DRIVE |

The VCF was split across two columns, so its highlight took in the VCO and ACCENT panels. Now each outline frames
one panel. **DEVILFISH MOD** already had one column per panel; unchanged.

## Presets
Open303 has none, so the port brings 13 (`presets.json`, the wrapper's own presets): Init, Classic Acid, Squelch
Lead, Deep Sub, Rubber Square, Plucky Saw, Long Sweep, Rat Acid, Distorted Square, Acid Screamer, Devilfish Snap,
Devilfish Growl, Soft Attack Bass. Each sets every control (the Devilfish switch first, since it changes the decay
range); offline, all 13 play, with levels within about 4.5 dB of each other.

## Left as is
- The waveform display stays a still picture that follows SAW / SQUARE (animation is too costly for MPC's screen).
- DEVILFISH "sticky": not reproduced offline. While DEVILFISH is off, the engine ignores the knobs on that page,
  which can feel like the page isn't responding.

## Checked
Offline test PASSED; check_skin OK; Q-Link outlines don't overlap (qlink_overlay.py); every preset probed.
