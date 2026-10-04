# Design QA: Elements

Status: **passed on the device** (2026-10-04, ✅ in the README). No owner's notes yet.

## What changed
| Page | before | after |
|---|---|---|
| ELEMENTS 1 | 1 BOW, BOW TIMBRE, BLOW, FLOW  ·  2 BLOW TIMBRE, STRIKE, MALLET, STRK TIMBRE  ·  3 CONTOUR, MODEL, GEOMETRY, BRIGHTNESS  ·  4 DAMPING, POSITION, SPACE | 1 BOW, BOW TIMBRE  ·  2 BLOW, FLOW, BLOW TIMBRE  ·  3 STRIKE, MALLET, STRK TIMBRE  ·  4 GEOMETRY, BRIGHTNESS, DAMPING, POSITION |
| ELEMENTS 2 | - | 1 CONTOUR  ·  2 MODEL  ·  3 SPACE |
| PLAY | 1 OCTAVE, FINE, BEND RANGE, LEGATO  ·  2 VELOCITY, SIGNATURE, VOLUME | 1 OCTAVE, FINE, BEND RANGE  ·  2 LEGATO, VELOCITY, SIGNATURE  ·  3 VOLUME |

- **ELEMENTS**: six panels of 1 to 5 controls don't fit four columns, so two Q-Link banks. Bank 1 = BOW | BLOW |
  STRIKE | the RESONATOR's four knobs (geometry, brightness, damping, position); bank 2 = CONTOUR | MODEL | SPACE.
  Each outline now frames one panel; before, the columns ran across them.
- **PLAY**: PITCH (octave, fine, bend range) | PERFORMANCE (legato, velocity, signature) | MASTER (volume).

Made in the design (convert.py MAPS) and re-converted.

## Presets (new)
Elements has none of its own, so the port brings 12 (`presets.json`, in MPC's PRESET menu): Init, Struck Bell,
Marimba, Bowed String, Breathy Pipe, Plucked String, Glass Harmonics, Ominous Drone, Twelve Strings, Metal Plate,
Wooden Drum, Wind Chime. Each sets every control; volumes matched offline (all within about 2 dB). How they sound is
the owner's call.

## Checked
Offline test PASSED (12 programs named and selectable); check_skin OK; no Q-Link outlines overlap; every preset
plays (probe).
