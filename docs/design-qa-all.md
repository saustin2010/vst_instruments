# Design QA: all plugins

Every plugin's `DESIGN-QA.md` in one file, in the README's catalogue order (put together 2026-10-04 from the
`<plugin>/DESIGN-QA.md` files, which stay the originals). The ticks are the README's Design QA column.


**Synths (22)**

- ✅ [303](#design-qa-303)
- ✅ [Aphex](#design-qa-aphex)
- ✅ [Braids](#design-qa-braids)
- ✅ [Chordism](#design-qa-chordism)
- ✅ [Denis](#design-qa-denis)
- ✅ [Elements](#design-qa-elements)
- ✅ [Fizzik](#design-qa-fizzik)
- ✅ [Hank](#design-qa-hank)
- ✅ [Helm](#design-qa-helm)
- ✅ [Hera](#design-qa-hera)
- ✅ [Hush One](#design-qa-hush-one)
- ✅ [MonkSynth](#design-qa-monksynth)
- ✅ [Mono Voice](#design-qa-mono-voice)
- ✅ [Moog](#design-qa-moog)
- ✅ [Mr Hyde](#design-qa-mr-hyde)
- ✅ [Noisemaker](#design-qa-noisemaker)
- ✅ [NuSaw](#design-qa-nusaw)
- ✅ [OB-Xd](#design-qa-ob-xd)
- ✅ [Plaits](#design-qa-plaits)
- ✅ [Rings](#design-qa-rings)
- ✅ [Tablor](#design-qa-tablor)
- ✅ [Wurl](#design-qa-wurl)

**Drum machines (2)**

- ✅ [Libpo32](#design-qa-libpo32)
- ✅ [Mr Drums](#design-qa-mr-drums)

**MIDI sequencers and generators (9)**

- ☐ [Eucalypso](#design-qa-eucalypso)
- ☐ [Grids](#design-qa-grids)
- ☐ [Groove Bank](#design-qa-groovebank)
- ☐ [Marbles](#design-qa-marbles)
- ☐ [Maze Lite](#design-qa-mazelite)
- ☐ [MIDI Player](#design-qa-midi-player)
- ☐ [Pixel Walkers](#design-qa-pixelwalkers)
- ☐ [Rampage](#design-qa-rampage)
- ☐ [Super Arp](#design-qa-superarp)

**Audio effects (3)**

- ✅ [Rings FX](#design-qa-rings-fx)
- ✅ [Verglas](#design-qa-verglas)
- ✅ [Warps](#design-qa-warps)

---

## Design QA: 303

Status: **passed on the device** (2026-10-03, ✅ in the README).

### Owner's notes
- Align the knobs with the Q-Links.
- Are there presets? Expose them in the PRESET menu.
- The DEVILFISH button is a bit sticky.
- The scope/waveform is static.

### What changed
**303 MAIN**, Q-Link columns (one per panel; "-" = an empty slot):

| | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| before | WAVEFORM, TUNING, CUTOFF, RESONANCE | ENV MOD, DECAY, ACCENT, VOLUME | DRIVE MODEL, DRIVE, DRIVE MIX, SHAPER DRIVE | - |
| after | WAVEFORM, TUNING (VCO) | CUTOFF, RESONANCE, ENV MOD, DECAY (VCF) | ACCENT, VOLUME | DRIVE MODEL, DRIVE, DRIVE MIX, SHAPER DRIVE |

The VCF was split across two columns, so its highlight took in the VCO and ACCENT panels. Now each outline frames
one panel. **DEVILFISH MOD** already had one column per panel; unchanged.

### Presets
Open303 has none, so the port brings 13 (`presets.json`, the wrapper's own presets): Init, Classic Acid, Squelch
Lead, Deep Sub, Rubber Square, Plucky Saw, Long Sweep, Rat Acid, Distorted Square, Acid Screamer, Devilfish Snap,
Devilfish Growl, Soft Attack Bass. Each sets every control (the Devilfish switch first, since it changes the decay
range); offline, all 13 play, with levels within about 4.5 dB of each other.

### Left as is
- The waveform display stays a still picture that follows SAW / SQUARE (animation is too costly for MPC's screen).
- DEVILFISH "sticky": not reproduced offline. While DEVILFISH is off, the engine ignores the knobs on that page,
  which can feel like the page isn't responding.

### Checked
Offline test PASSED; check_skin OK; Q-Link outlines don't overlap (qlink_overlay.py); every preset probed.

Files: [`schwung/instruments/303/`](../schwung/instruments/303/)

---

## Design QA: Aphex

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
- Expose the presets in the PRESET menu.
- The patch controls (random, mutate, gate, reset) should all be in their own box.
- Some confusion with the Q-Links; check them.
- Align the Q-Links on the VCO page better: not grouped well. Same with MIXER.
- ENVELOPES is good, but the curve should be on the left and the controls on the right.
- The controls on ESP need adjusting.

### What changed
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

### Presets
41 built-in presets, in MPC's PRESET menu (VST programs).

### For the owner
- ESP: its Q-Links were already clean. Say what to change there (spacing, the empty lower half, the AUD MIX row).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).

Files: [`schwung/instruments/aphex/`](../schwung/instruments/aphex/)

---

## Design QA: Braids

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
- Better Q-Link alignment.
- Fix the sliders so the animation isn't silly.
- Move the waveforms (envelope curves) to the left and the sliders to the right.

### What changed
| Page | before | after |
|---|---|---|
| BRAIDS | 1 TIMBRE, COLOR, CUTOFF, RESONANCE  ·  2 FILTER ENV, FM, OCTAVE, VOLUME  ·  3 PATCH, ALGORITHM | 1 ALGORITHM, TIMBRE, COLOR  ·  2 CUTOFF, RESONANCE, FILTER ENV, FM  ·  3 OCTAVE, VOLUME  ·  4 PATCH |
| ENVELOPES | 1 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  2 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE | 1 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  2 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE (unchanged) |

- **BRAIDS**: one Q-Link column per panel: OSCILLATOR (algorithm, timbre, color) | FILTER + MOD (the two panels
  sit side by side) | OUTPUT | PROGRAM. Each highlight now frames its own panel; before, columns cut across three.
- **ENVELOPES**: the curve displays on the left, the sliders on the right (in the design: the row is reversed).
- **Sliders**: their filmstrips were 160 x 20480 px, taller than MPC draws correctly (knob strips work up to 12288),
  which is the likely cause of the odd thumb movement. The skin builder now makes slider strips the way Akai's own
  are made: frames of the slider's size, 76 of them here (44 x 12160 px). The same fix reaches every plugin with
  sliders (Noisemaker, Libpo32, Hush One, Hera, NuSaw).

All made in the design (convert.py MAPS) and re-converted.

### Presets
10, in MPC's PRESET menu.

### For the owner
- The slider fix is built the way the envelope displays were fixed (that one was confirmed on the Live II), but
  this exact change hasn't been seen on a device yet: check that the thumbs now track smoothly.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).

Files: [`schwung/instruments/braids/`](../schwung/instruments/braids/)

---

## Design QA: Chordism

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
- Are there presets? (Yes: 57, now in the PRESET menu.)
- Q-Links are good on page one, but the OSCILLATORS page is messy and some controls are grouped together.
- The FILTER ENV screen has funny buttons and controls; needs better alignment.

### What changed
| Page | before | after |
|---|---|---|
| MAIN | 1 CHORD, TUNING, SCALE, ROOT  ·  2 DETUNE, WIDTH, SPREAD, ROTATION  ·  3 CUTOFF, RESONANCE, FILTER MODE, SLOPE  ·  4 ATTACK, RELEASE, VCA MODE, VOLUME | 1 CHORD, TUNING, SCALE, ROOT  ·  2 DETUNE, WIDTH, SPREAD, ROTATION  ·  3 CUTOFF, RESONANCE, FILTER MODE, SLOPE  ·  4 ATTACK, RELEASE, VCA MODE, VOLUME (unchanged) |
| OSCILLATORS | 1 WAVE 1, MIX 1, WAVE 2, MIX 2  ·  2 WAVE 3, MIX 3, WAVE 4, MIX 4  ·  3 SHAPE, SHAPE 1, SHAPE 2, SHAPE 3  ·  4 SHAPE 4, LFO PHASE 1, LFO PHASE 2, LFO MODE | 1 WAVE 1, MIX 1, SHAPE 1, LFO PHASE 1  ·  2 WAVE 2, MIX 2, SHAPE 2, LFO PHASE 2  ·  3 WAVE 3, MIX 3, SHAPE 3, LFO PHASE 3  ·  4 WAVE 4, MIX 4, SHAPE 4, LFO PHASE 4 |
| SHAPE | 1 LFO PHASE 3, LFO PHASE 4, PAN MORPH, PAN MORPH IN  ·  2 FM MOD, FM AMT, MORPH INDEX, MORPH INT  ·  3 FM AMT 1, FM AMT 2, FM AMT 3, FM AMT 4  ·  4 FM POSITION | 1 SHAPE, LFO MODE, PAN MORPH, PAN MORPH IN  ·  2 FM MOD, FM AMT, MORPH INDEX, MORPH INT  ·  3 FM AMT 1, FM AMT 2, FM AMT 3, FM AMT 4  ·  4 FM POSITION |
| FILTER ENV | 1 ENV A, ENV D, ENV AMT, DRIVE  ·  2 FLT ENV MODE, FENV RESET, LOFI POS, FLT LFO RATE  ·  3 FLT LFO DPTH, FLT LFO SPRD, FLT LFO WAVE, FLT LFO MODE  ·  4 SHP LFO WAVE, SHP LFO RATE, SHP LFO DPTH | 1 ENV A, ENV D, ENV AMT, DRIVE  ·  2 FLT LFO RATE, FLT LFO DPTH, FLT LFO SPRD, FLT LFO WAVE  ·  3 SHP LFO WAVE, SHP LFO RATE, SHP LFO DPTH  ·  4 FLT ENV MODE, FENV RESET, LOFI POS |
| VIBRATO | 1 VIB DEPTH, VIB SPEED, VIB DELAY, SWEEP  ·  2 SWEEP RATE, VIBRAT STRAY, VIB OSCS, SWEEP OSCS  ·  3 LVL LFO RATE, LVL LFO DPTH, LVL LFO WAVE, LVL LFO MODE  ·  4 PAN LFO RATE, PAN LFO DPTH, PAN LFO WAVE, PAN LFO MODE | 1 VIB DEPTH, VIB SPEED, VIB DELAY, SWEEP  ·  2 SWEEP RATE, VIBRAT STRAY, VIB OSCS, SWEEP OSCS  ·  3 LVL LFO RATE, LVL LFO DPTH, LVL LFO WAVE, LVL LFO MODE  ·  4 PAN LFO RATE, PAN LFO DPTH, PAN LFO WAVE, PAN LFO MODE (unchanged) |
| TREMOLO | 1 TREM RATE, TREM DEPTH, GLIDE, TREMOLO WAVE  ·  2 GLIDE LEGATO, VCA RESET, DRONE, GRIND  ·  3 BIT SHIFT, DECIMATOR | 1 TREM RATE, TREM DEPTH, GLIDE  ·  2 TREMOLO WAVE, GLIDE LEGATO, VCA RESET, DRONE  ·  3 GRIND, BIT SHIFT, DECIMATOR |
| DELAY | 1 DELAY MIX, DLY TIME, DLY FBK, DLY TONE  ·  2 DLY MOD DPTH, DELAY MODE, DLY TONE HI, DLY TONE LO  ·  3 DLY MOD RATE | 1 DELAY MIX, DLY TIME, DLY FBK, DLY TONE  ·  2 DELAY MODE, DLY TONE HI, DLY TONE LO  ·  3 DLY MOD DPTH, DLY MOD RATE |
| REVERB | 1 REVERB MIX, REV DECAY, REV DAMPING, SHIMMER  ·  2 ROOM SIZE, REV LOW CUT, REV MOD RATE, REV MOD DPTH | 1 REVERB MIX, REV DECAY, REV DAMPING, SHIMMER  ·  2 ROOM SIZE  ·  3 REV LOW CUT, REV MOD RATE, REV MOD DPTH |
| CHORD MAP 1 | 1 C, C#, D, D#  ·  2 E, F, F#, G  ·  3 G#, A, A#, B  ·  4 INTERVAL 1, INTERVAL 2, INTERVAL 3, CTRL SRC | 1 C, C#, D, D#  ·  2 E, F  ·  3 F#, G, G#, A  ·  4 A#, B |
| CHORD MAP 2 | 1 CTRL CC, CTRL>CUTOFF, CTRL>MORPH, CTRL>VIBRATO  ·  2 CTRL>SHAPE, CTRL>FM | 1 INTERVAL 1, INTERVAL 2, INTERVAL 3  ·  2 CTRL SRC, CTRL CC, CTRL>CUTOFF, CTRL>MORPH  ·  3 CTRL>VIBRATO, CTRL>SHAPE, CTRL>FM |
| ARPEGGIATOR | 1 EUCLID STEPS, EUCLID BEATS, ARP TEMPO, VAR COUNT  ·  2 ARP STATUS, ARP HOLD, ARP DIRECTIO, ARP VAR INT  ·  3 CLOCK SYNC, CLOCK DIVISI | 1 EUCLID STEPS, EUCLID BEATS, ARP TEMPO, VAR COUNT  ·  2 ARP HOLD, ARP DIRECTIO, ARP VAR INT  ·  3 CLOCK SYNC, CLOCK DIVISI  ·  4 ARP STATUS |

- **OSCILLATORS**: one Q-Link column per voice: its WAVE, MIX, SHAPE and LFO PHASE. Each voice's SHAPE and LFO PHASE
  knobs now sit directly under its WAVE / MIX box, so each column's outline is a clean strip down the page. LFO
  PHASE 3 and 4 moved here from SHAPE; the global SHAPE and LFO MODE moved to SHAPE.
- **SHAPE**: the first panel is now SHAPE & PAN: the global SHAPE, LFO MODE and the pan-morph pair (one column),
  then the FM rows (MOD / AMOUNT / MORPH, the four per-voice FM amounts, FM POSITION).
- **FILTER ENV**: the envelope panel's knobs and their options packed together at the top (they floated with a big
  gap). Columns: ENV (A, D, amount, drive) | filter LFO (rate, depth, spread, wave) | SHAPE LFO | ENV options.
  FLT LFO MODE is touch only (no Q-Link slot left in that group).
- **TREMOLO / DELAY / REVERB / ARPEGGIATOR**: a column per row; the delay and reverb modulation pairs get their
  own column (DELAY: MOD DEPTH above MOD RATE).
- **CHORD MAP**: bank 1 = the 12 notes in order, split so no outline crosses a row (C..D# | E F | F#..A | A# B),
  with more space between the two rows; bank 2 = INTERVALS | CONTROL (source, CC, to cutoff, to morph) | to vibrato,
  shape, FM. (Bank 2's outlines meet edge to edge: those panels sit side by side.)
- **MAIN**, **VIBRATO**: unchanged (already one column per group).

All made in the design (convert.py MAPS) and re-converted.

### Presets
57 built-in presets, in MPC's PRESET menu (there's no PRESET control on the pages).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (CHORD MAP's bank 2 had two that crossed: its CONTROL row is two groups with a gap now,
CC / TO CUTOFF / TO MORPH with SOURCE, and TO VIBRATO / TO SHAPE / TO FM).

Files: [`schwung/instruments/chordism/`](../schwung/instruments/chordism/)

---

## Design QA: Denis

Status: **passed on the device** (2026-10-04, ✅ in the README). No owner's notes yet.

### What changed
| Page | before | after |
|---|---|---|
| DENIS | 1 PRESET, RANDOM ALL, OSC1 FREQ, OSC1 TIMBRE  ·  2 OSC2 FREQ, HARMONICS, OSC MIX, FOLD DEPTH  ·  3 FOLD TYPE, CUTOFF, Q (RESO), FILTER TYPE  ·  4 VEL>FILTER, PORTAMENTO, LEGATO | 1 OSC1 FREQ, OSC1 TIMBRE  ·  2 OSC2 FREQ, HARMONICS, OSC MIX  ·  3 FOLD DEPTH, FOLD TYPE, CUTOFF, Q (RESO)  ·  4 VEL>FILTER, PORTAMENTO, LEGATO |
| ENV / MOD | 1 ATTACK, DECAY, SUSTAIN, RELEASE  ·  2 NOISE MIX, NOISE TYPE, RANDOM SOUND, RANDOM MOD  ·  3 RESET MATRIX, LFO RATE, S&H RATE, ENV DEPTH  ·  4 NOISE DEPTH | 1 ATTACK, DECAY, SUSTAIN, RELEASE  ·  2 NOISE MIX, NOISE TYPE  ·  3 LFO RATE, S&H RATE, ENV DEPTH, NOISE DEPTH |
| ENV LFO | 1 ENV>PITCH1, ENV>TIMBRE, ENV>PITCH2, ENV>HARM  ·  2 ENV>FOLD, ENV>FTYPE, ENV>CUTOFF, ENV>LEVEL  ·  3 LFO>PITCH1, LFO>TIMBRE, LFO>PITCH2, LFO>HARM  ·  4 LFO>FOLD, LFO>FTYPE, LFO>CUTOFF, LFO>LEVEL | 1 ENV>PITCH1, ENV>TIMBRE, ENV>PITCH2, ENV>HARM  ·  2 ENV>FOLD, ENV>FTYPE, ENV>CUTOFF, ENV>LEVEL  ·  3 LFO>PITCH1, LFO>TIMBRE, LFO>PITCH2, LFO>HARM  ·  4 LFO>FOLD, LFO>FTYPE, LFO>CUTOFF, LFO>LEVEL (unchanged) |
| S&H NOISE | 1 S&H>PITCH1, S&H>TIMBRE, S&H>PITCH2, S&H>HARM  ·  2 S&H>FOLD, S&H>FTYPE, S&H>CUTOFF, S&H>LEVEL  ·  3 NOISE>PITCH1, NOISE>TIMBRE, NOISE>PITCH2, NOISE>HARM  ·  4 NOISE>FOLD, NOISE>FTYPE, NOISE>CUTOFF, NOISE>LEVEL | 1 S&H>PITCH1, S&H>TIMBRE, S&H>PITCH2, S&H>HARM  ·  2 S&H>FOLD, S&H>FTYPE, S&H>CUTOFF, S&H>LEVEL  ·  3 NOISE>PITCH1, NOISE>TIMBRE, NOISE>PITCH2, NOISE>HARM  ·  4 NOISE>FOLD, NOISE>FTYPE, NOISE>CUTOFF, NOISE>LEVEL (unchanged) |

- **DENIS**: one Q-Link column per panel: OSC 1 (freq, timbre) | OSC 2 (freq, harmonics, mix) | WAVEFOLDER + FILTER
  side by side (fold depth, fold type, cutoff, Q) | VOICE (vel to filter, portamento, legato). The preset pop-up,
  RANDOM ALL and FILTER TYPE are touch only: columns used to cut across panels to fit them.
- **ENV / MOD**: ENVELOPE | NOISE (mix, type) | MODULATORS; the RANDOM SOUND / RANDOM MOD / RESET MATRIX buttons are
  touch only (turning a knob to fire a button).
- **ENV LFO**, **S&H NOISE** (the mod matrix): unchanged, already half a row per column.

Made in the design (convert.py MAPS) and re-converted.

### Presets
30, in MPC's PRESET menu.

### For the owner
- ENV / MOD shows the envelope curve at the bottom right, diagonally from its knobs. Say if it should move.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap.

Files: [`schwung/instruments/denis/`](../schwung/instruments/denis/)

---

## Design QA: Elements

Status: **passed on the device** (2026-10-04, ✅ in the README). No owner's notes yet.

### What changed
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

### Presets (new)
Elements has none of its own, so the port brings 12 (`presets.json`, in MPC's PRESET menu): Init, Struck Bell,
Marimba, Bowed String, Breathy Pipe, Plucked String, Glass Harmonics, Ominous Drone, Twelve Strings, Metal Plate,
Wooden Drum, Wind Chime. Each sets every control; volumes matched offline (all within about 2 dB). How they sound is
the owner's call.

### Checked
Offline test PASSED (12 programs named and selectable); check_skin OK; no Q-Link outlines overlap; every preset
plays (probe).

Files: [`mutable-instruments/elements/`](../mutable-instruments/elements/)

---

## Design QA: Fizzik

Status: **passed on the device** (2026-10-04, ✅ in the README). No owner's notes yet.

### What changed
| Page | before | after |
|---|---|---|
| MAIN | 1 PRESET, RANDOM ALL, RND EXCITER, MODEL A  ·  2 MODEL B, COUPLE, BALANCE, CUTOFF  ·  3 RESONANCE, FILTER TYPE, VOICING, RND RESON  ·  4 DRIVE, WIDTH, LEVEL | 1 MODEL A, MODEL B, COUPLE, BALANCE  ·  2 CUTOFF, RESONANCE, FILTER TYPE, VOICING  ·  3 DRIVE, WIDTH, LEVEL  ·  4 PRESET |
| EXCITER | 1 EXC MIX, CRACKLE, COLOR, ATTACK  ·  2 DECAY, EXC RESO, VEL LEVEL, VEL COLOR  ·  3 STRUCTURE A, DECAY A, DAMP A, POSITION A  ·  4 TONE A, TUNE A, TENSION A | 1 EXC MIX, CRACKLE, COLOR, ATTACK  ·  2 DECAY, EXC RESO, VEL LEVEL, VEL COLOR  ·  3 STRUCTURE A, DECAY A, DAMP A, POSITION A  ·  4 TONE A, TUNE A, TENSION A (unchanged) |
| RESONATOR B | 1 STRUCTURE B, DECAY B, DAMP B, POSITION B  ·  2 TONE B, TUNE B, TENSION B, GLIDE  ·  3 AMP ATK, AMP REL, SPREAD, REVERB  ·  4 REV SIZE, REV DAMP | 1 STRUCTURE B, DECAY B, DAMP B, POSITION B  ·  2 TONE B, TUNE B, TENSION B  ·  3 GLIDE, AMP ATK, AMP REL, SPREAD  ·  4 REVERB, REV SIZE, REV DAMP |
| DELAY / FX | 1 DELAY MIX, DLY TIME, DLY FBK, DLY TONE  ·  2 TONE, BODY, CHORUS, CHO RATE  ·  3 CHO DEPTH, GLUE, LIM DRIVE, LIM CEIL | 1 DELAY MIX, DLY TIME, DLY FBK, DLY TONE  ·  2 TONE, BODY, CHORUS, CHO RATE  ·  3 CHO DEPTH, GLUE, LIM DRIVE, LIM CEIL (unchanged) |
| LFO / AT | 1 LFO1 RATE, LFO1 DEPTH, LFO1 SHAPE, LFO1 TARGET  ·  2 LFO2 RATE, LFO2 DEPTH, LFO2 SHAPE, LFO2 TARGET  ·  3 AT PRESET, AT BRIGHT, AT BOW, AT CUTOFF  ·  4 AT VIB, AT BEND, AT VIB RATE, AT CURVE | 1 LFO1 RATE, LFO1 DEPTH, LFO1 SHAPE, LFO1 TARGET  ·  2 LFO2 RATE, LFO2 DEPTH, LFO2 SHAPE, LFO2 TARGET  ·  3 AT PRESET, AT BRIGHT, AT BOW, AT CUTOFF  ·  4 AT VIB, AT BEND, AT VIB RATE, AT CURVE (unchanged) |

- **MAIN**: one Q-Link column per panel: RESONATORS (model A, model B, couple, balance) | FILTER (cutoff, resonance,
  type, voicing) | OUT (drive, width, level) | PATCH (preset). The three RANDOM buttons are touch only and now sit
  together in PATCH, stacked beside a narrower scope (RND RESON was alone in OUT).
- **RESONATOR B**: the row of seven splits 4 + 3 instead of running into the next panel; VOICE & ENVELOPE and
  REVERB get a column each.
- **EXCITER**, **DELAY / FX**, **LFO / AT**: unchanged (already a half row per column).

Made in the design (convert.py MAPS) and re-converted.

### Presets
31, in MPC's PRESET menu.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap.

Files: [`schwung/instruments/fizzik/`](../schwung/instruments/fizzik/)

---

## Design QA: Hank

Status: **passed on the device** (2026-10-04, ✅ in the README). No owner's notes yet.

### What changed
| Page | before | after |
|---|---|---|
| HANK | 1 PATCH, RATIO, BRIGHT, BITE (MOD)  ·  2 TONE, ATTACK, DECAY, SUSTAIN  ·  3 NOISE, GLIDE, VOICES, TRANSPOSE  ·  4 VOLUME | 1 RATIO, BRIGHT, BITE (MOD), TONE  ·  2 ATTACK, DECAY, SUSTAIN  ·  3 NOISE, GLIDE, VOICES  ·  4 TRANSPOSE, VOLUME |

- One Q-Link column per panel: OPERATOR (ratio, bright, bite, tone) | MOD ENVELOPE (attack, decay, sustain) | VOICE,
  whose five knobs split 3 + 2 (noise, glide, voices | transpose, volume). Before, the columns ran from the patch
  display across the operator and envelope panels. The PATCH stepper is touch only (its arrows, and MPC's PRESET menu).

Made in the design (convert.py MAPS) and re-converted.

### Presets
32, in MPC's PRESET menu.

### For the owner
- The RATIO pop-up overhangs the top edge of its panel (as before). Say if it should move.

### Checked
Offline test PASSED; check_skin OK; Q-Link outlines don't overlap (the two VOICE halves meet edge to edge).

Files: [`schwung/instruments/hank/`](../schwung/instruments/hank/)

---

## Design QA: Helm

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; groups of up to four, 3-2-4-3 style where a group is smaller).

### What changed
Seven of the twelve pages had Q-Link outlines running across panels: their rows of six to eight controls filled
columns of four in page order.

Q-Link columns before → after ("-" = an empty slot; names as they are now):

| Page | before | after |
|---|---|---|
| MAIN | 1 PATCH, VOLUME, POLYPHONY, OCTAVE  ·  2 LEGATO, CUTOFF, RESONANCE, FILTER TYPE  ·  3 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE | 1 VOLUME, POLYPHONY, OCTAVE, LEGATO  ·  2 CUTOFF, RESONANCE, FILTER TYPE  ·  3 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  4 PATCH |
| OSC | 1 OSC1 TRANSP, OSC1 TUNE, OSC1 DETUNE, OSC1 VOICES  ·  2 OSC1 VOLUME, OSC1 WAVE, OSC1 HARMON, OSC2 TRANSP  ·  3 OSC2 TUNE, OSC2 DETUNE, OSC2 VOICES, OSC2 VOLUME  ·  4 OSC2 WAVE, OSC2 HARMON | 1 OSC1 WAVE, OSC1 TRANSP, OSC1 TUNE, OSC1 VOLUME  ·  2 OSC1 VOICES, OSC1 DETUNE, OSC1 HARMON  ·  3 OSC2 WAVE, OSC2 TRANSP, OSC2 TUNE, OSC2 VOLUME  ·  4 OSC2 VOICES, OSC2 DETUNE, OSC2 HARMON |
| OSC MIX | 1 CROSS MOD, FBK AMOUNT, FBK TRANSP, FBK TUNE  ·  2 OSC MIX, NOISE VOL, SUB OCT, SUB SHUF  ·  3 SUB VOL, SUB OSC WAVE | 1 CROSS MOD, FBK AMOUNT, FBK TRANSP, FBK TUNE  ·  2 OSC MIX  ·  3 NOISE VOL  ·  4 SUB OCT, SUB SHUF, SUB VOL, SUB OSC WAVE |
| FILTER | 1 FLT ENV AMT, FILTER BLEND, FILTER DRIVE, FILTER ON  ·  2 SATURATION, FILTER SHELF, FILTER STYLE, FLT KEYTRACK  ·  3 FORMANT ON, FORMANT X, FORMANT Y, FLT ATTACK  ·  4 FLT DECAY, FLT RELEASE, FLT SUSTAIN | 1 FILTER ON, FILTER STYLE, FILTER SHELF, FILTER BLEND  ·  2 FILTER DRIVE, SATURATION, FLT ENV AMT, FLT KEYTRACK  ·  3 FORMANT ON, FORMANT X, FORMANT Y  ·  4 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE |
| MOD ENV | 1 MOD ATTACK, MOD DECAY, MOD RELEASE, MOD SUSTAIN  ·  2 LFO1 AMOUNT, LFO1 FREQ, LFO1 RETRIG, LFO1 SYNC  ·  3 LFO1 TEMPO, LFO1 WAVE | 1 MOD ATTACK, MOD DECAY, MOD SUSTAIN, MOD RELEASE  ·  2 LFO1 WAVE, LFO1 AMOUNT, LFO1 RETRIG  ·  3 LFO1 SYNC, LFO1 FREQ, LFO1 TEMPO |
| MONO LFO | 1 LFO2 AMOUNT, LFO2 FREQ, LFO2 RETRIG, LFO2 SYNC  ·  2 LFO2 TEMPO, LFO2 WAVE, PLFO AMOUNT, PLFO FREQ  ·  3 PLFO SYNC, PLFO TEMPO, PLFO WAVE | 1 LFO2 WAVE, LFO2 AMOUNT, LFO2 RETRIG  ·  2 LFO2 SYNC, LFO2 FREQ, LFO2 TEMPO  ·  3 PLFO WAVE, PLFO AMOUNT  ·  4 PLFO SYNC, PLFO FREQ, PLFO TEMPO |
| STEP SEQ | 1 NUM STEPS, STEP FREQ, STEP RETRIG, STEP SYNC  ·  2 STEP TEMPO, STEP SMOOTH, STEP 1, STEP 2  ·  3 STEP 3, STEP 4, STEP 5, STEP 6  ·  4 STEP 7, STEP 8 | 1 NUM STEPS, STEP SMOOTH, STEP RETRIG  ·  2 STEP SYNC, STEP FREQ, STEP TEMPO  ·  3 STEP 1, STEP 2, STEP 3, STEP 4  ·  4 STEP 5, STEP 6, STEP 7, STEP 8 |
| STEPS | 1 STEP 9, STEP 10, STEP 11, STEP 12  ·  2 STEP 13, STEP 14, STEP 15, STEP 16  ·  3 ARP FREQ, ARP GATE, ARP OCTAVES, ARP ON  ·  4 ARP PATTERN, ARP SYNC, ARP TEMPO | 1 STEP 9, STEP 10, STEP 11, STEP 12  ·  2 STEP 13, STEP 14, STEP 15, STEP 16  ·  3 ARP ON, ARP PATTERN, ARP OCTAVES, ARP GATE  ·  4 ARP SYNC, ARP FREQ, ARP TEMPO |
| DISTORTION | 1 DIST DRIVE, DIST MIX, DIST ON, DIST TYPE  ·  2 DELAY MIX, DELAY FBK, DELAY FREQ, DELAY ON  ·  3 DELAY SYNC, DELAY TEMPO | 1 DIST ON, DIST TYPE, DIST DRIVE, DIST MIX  ·  2 DELAY ON, DELAY MIX, DELAY FBK  ·  3 DELAY SYNC, DELAY FREQ, DELAY TEMPO |
| REVERB | 1 REV DAMPING, REVERB MIX, REVERB FBK, REVERB ON  ·  2 STUT FREQ, STUTTER ON, RESAMP FREQ, RESAMP SYNC  ·  3 RESAMP TEMPO, STUT SOFT, STUT SYNC, STUT TEMPO | 1 REVERB ON, REVERB MIX, REVERB FBK, REV DAMPING  ·  2 STUTTER ON, STUT SOFT  ·  3 STUT SYNC, STUT FREQ, STUT TEMPO  ·  4 RESAMP SYNC, RESAMP FREQ, RESAMP TEMPO |
| PLAYING | 1 BPM, AUTO BPM, BEND RANGE, PORTAMENTO  ·  2 PORTA TYPE, VEL TRACK, MOD 1 AMT, MOD 2 AMT  ·  3 MOD 3 AMT, MOD 4 AMT, MOD 5 AMT, MOD 6 AMT  ·  4 MOD 7 AMT, MOD 8 AMT | 1 AUTO BPM, BPM, VEL TRACK, BEND RANGE  ·  2 PORTA TYPE, PORTAMENTO  ·  3 MOD 1 AMT, MOD 2 AMT, MOD 3 AMT, MOD 4 AMT  ·  4 MOD 5 AMT, MOD 6 AMT, MOD 7 AMT, MOD 8 AMT |
| MOD AMOUNTS | 1 MOD 9 AMT, MOD 10 AMT, MOD 11 AMT, MOD 12 AMT  ·  2 MOD 13 AMT, MOD 14 AMT, MOD 15 AMT, MOD 16 AMT | 1 MOD 9 AMT, MOD 10 AMT, MOD 11 AMT, MOD 12 AMT  ·  2 MOD 13 AMT, MOD 14 AMT, MOD 15 AMT, MOD 16 AMT (unchanged) |

Layout (each row of more than four split into groups with a gap, a Q-Link column each):
- **MAIN**: VOICE | FILTER | AMP ENVELOPE | PATCH (the patch last, as on Aphex). No controls moved.
- **OSC**: per oscillator, WAVE / TRANSPOSE / TUNE / VOLUME and its unison (VOICES, DETUNE, HARMONIZE).
- **OSC MIX** (the designed page): CROSS MOD + the feedback trio | OSC MIX / NOISE | the sub oscillator's four. The
  "Q-LINK ROW" tags (Q-Links go by column) and a dead "TEST OSC BUS" line went.
- **FILTER**: the filter's switch / style / shelf / blend and its drive / saturation / envelope amount / keytrack;
  FORMANT; FILTER ENV now in A D S R order (it was A D R S).
- **MOD ENV**: MOD ENV in A D S R order (it was A D R S); MONO LFO 1 as wave / amount / retrigger and sync / rate /
  tempo.
- **MONO LFO**: MONO LFO 2 and the POLY LFO the same way.
- **STEP SEQ / STEPS**: the sequencer's steps / smoothing / retrigger and sync / rate / tempo; steps in fours; the
  arpeggiator as on / pattern / octaves / gate and sync / rate / tempo.
- **DISTORTION**: DISTORTION's four; DELAY as on / mix / feedback and sync / rate / tempo.
- **REVERB**: REVERB's four; STUTTER (on, softness) | STUTTER RATE | RESAMPLE RATE as panels of their own.
- **PLAYING / MOD AMOUNTS**: tempo / velocity / bend and the glide pair; the sixteen mod amounts in fours.
- **Names**: 61 parameter names that Helm cut at 12 characters now read properly (MON LFO 1 RE → LFO1 RETRIG,
  STUTTE TEM 1 → RESAMP TEMPO, REVER DAMPIN → REV DAMPING, PIT BEN RANG → BEND RANGE...). Only the names changed:
  the parameters' order (what projects and Q-Link assignments save) is the same.

OSC MIX is made in the design (convert.py MAPS `augment` / `qlinks`); the other pages in the port's page plan
(`layout.grid.conf`); the names in MAPS `names`. A rerun keeps all three.

### Presets
Its patches are in MPC's PRESET menu (unchanged).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).

Files: [`schwung/instruments/helm/`](../schwung/instruments/helm/)

---

## Design QA: Hera

Status: **passed on the device** (2026-10-04, ✅ in the README). No owner's notes yet.

### What changed
| Page | before | after |
|---|---|---|
| MAIN | 1 PATCH, CHORUS I, CHORUS II, OCTAVE  ·  2 VOLUME, HPF, VCF FREQ, RESONANCE  ·  3 VCF ENV, VCF LFO, ATTACK, DECAY  ·  4 SUSTAIN, RELEASE | 1 VCF FREQ, RESONANCE, VCF ENV, VCF LFO  ·  2 ATTACK, DECAY, SUSTAIN, RELEASE  ·  3 OCTAVE, VOLUME, HPF  ·  4 PATCH, CHORUS I, CHORUS II |
| DCO / LFO | 1 RANGE, DCO LFO, PWM DEPTH, PWM MODE  ·  2 PULSE, SAW, SUB, NOISE  ·  3 LFO RATE, LFO DELAY, LFO TRIG, VCA MODE  ·  4 VCF KYBD, VCF BEND, VCA LEVEL | 1 RANGE, DCO LFO, PWM DEPTH, PWM MODE  ·  2 PULSE, SAW, SUB, NOISE  ·  3 LFO RATE, LFO DELAY, LFO TRIG  ·  4 VCA MODE, VCF KYBD, VCF BEND, VCA LEVEL |

- **MAIN**: one Q-Link column per panel: VCF (freq, resonance, env, LFO) | ENV (the four faders) | MASTER (octave,
  volume, HPF) | PROGRAM + CHORUS (patch, chorus I, chorus II; the two panels sit side by side). The VCF panel's
  cutoff-envelope display moved to the left of its knobs (the owner's "displays left, controls right").
- **DCO / LFO**: DCO | the source mixer faders | LFO GENERATOR (rate, delay, trigger) | VCA & HPF (mode, keyboard,
  bend, level); before, VCA MODE was in the LFO's column.
- The envelope faders get the rebuilt slider filmstrips (see Braids' DESIGN-QA.md: smaller strips MPC draws right).

Made in the design (convert.py MAPS) and re-converted.

### Presets
56, in MPC's PRESET menu.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap.

Files: [`schwung/instruments/hera/`](../schwung/instruments/hera/)

---

## Design QA: Hush One

Status: **passed on the device** (2026-10-04, ✅ in the README). No owner's notes yet (its presets were the
first fix of the day: your TAL-BassLine-101 presets show after the 11 built-in ones).

### What changed
| Page | before | after |
|---|---|---|
| MAIN | 1 PATCH, CUTOFF, RESONANCE, ENV AMT  ·  2 KEY TRACK, ATTACK, DECAY, SUSTAIN  ·  3 RELEASE, VCA MODE, OCTAVE, VELO SENS  ·  4 MASTER VOL | 1 CUTOFF, RESONANCE, ENV AMT, KEY TRACK  ·  2 ATTACK, DECAY, SUSTAIN, RELEASE  ·  3 VCA MODE, OCTAVE, VELO SENS, MASTER VOL  ·  4 PATCH |
| SOURCE | 1 SAW WAVE, PULSE / SQR, SUB OSC, NOISE  ·  2 TRANSPOSE, FINE TUNE, WHITE NOISE, FLT ATTACK  ·  3 FLT DECAY, FLT SUSTAIN, FLT RELEASE, SUB MODE  ·  4 PULSE WIDTH, PWM SOURCE, PWM LFO, PWM ENV | 1 SAW WAVE, PULSE / SQR, SUB OSC, NOISE  ·  2 TRANSPOSE, FINE TUNE, WHITE NOISE  ·  3 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  4 PULSE WIDTH, PWM SOURCE, PWM LFO, PWM ENV |
| MODULATOR | 1 LFO RATE, LFO WAVE, LFO RETRIG, LFO SYNC  ·  2 LFO INVERT, PITCH SNAP, LFO PITCH, LFO FILTER  ·  3 LFO PWM, VEL FILTER, ENV POLARITY, ENV FULL  ·  4 VOL CORRECT, DECLICK | 1 LFO RATE, LFO WAVE  ·  2 LFO PITCH, LFO FILTER, LFO PWM  ·  3 VEL FILTER, ENV POLARITY, ENV FULL  ·  4 VOL CORRECT, DECLICK |
| PERFORM | 1 GLIDE, PORTA MODE, PORTA CURVE, BEND RANGE  ·  2 RETRIGGER, PRIORITY, HOLD, SAME NOTE  ·  3 GATE MODE, VEL MODE | 1 GLIDE, PORTA MODE, PORTA CURVE, BEND RANGE  ·  2 RETRIGGER, PRIORITY, HOLD, SAME NOTE  ·  3 GATE MODE, VEL MODE (unchanged) |

- **MAIN**: laid out like Moog's: VCF | ENV (the four faders) | OUTPUT (VCA mode, octave, velo sens, master vol) |
  PATCH. The VCF's response-curve display moved to the left of its knobs.
- **SOURCE**: the mixer faders | transpose, fine tune, white noise | the filter envelope | PWM (width, source, LFO,
  env). SUB MODE is touch only (alone at the top of the other panel).
- **MODULATOR**: LFO rate and wave | LFO AMOUNT | FILTER EXTRAS split 3 + 2. The four LFO switches (retrig, sync,
  invert, pitch snap) are touch only.
- **PERFORM**: unchanged (already one column per panel).
- The faders get the rebuilt slider filmstrips (see Braids' DESIGN-QA.md).

Made in the design (convert.py MAPS) and re-converted.

### Presets
11 built-in + the TAL-BassLine-101 files in /sdcard/vst/hush1/presets (124 installed), all in MPC's PRESET menu.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (SOURCE's TRANSPOSE / FINE TUNE / WHITE NOISE moved a
little left, clear of the FLT envelope's column: their outlines had crossed).

Files: [`schwung/instruments/hush1/`](../schwung/instruments/hush1/)

---

## Design QA: MonkSynth

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| SINGER | 1 SINGER, VOWEL, HEAD SIZE, BREATH  ·  2 LEVEL, ATTACK, DECAY, SUSTAIN  ·  3 RELEASE, GLIDE, VIBRATO, VIB RATE  ·  4 BEND RNG | 1 VOWEL, HEAD SIZE, BREATH, LEVEL  ·  2 ATTACK, DECAY, SUSTAIN, RELEASE  ·  3 GLIDE, VIBRATO, VIB RATE, BEND RNG  ·  4 SINGER |
| CHOIR | 1 UNISON, DETUNE, SPREAD, DELAY  ·  2 DELAY RATE, PRESSURE TO, PRES DEPTH | 1 UNISON, DETUNE, SPREAD  ·  2 DELAY, DELAY RATE  ·  3 PRESSURE TO, PRES DEPTH |

- **SINGER**: the columns ran across panels (SINGER with the VOICE knobs, LEVEL with the envelope, RELEASE with
  EXPRESSION), so each outline took in two panels. Now column 1 = VOICE, 2 = the ADSR, 3 = EXPRESSION, and the SINGER
  stepper (top left) has column 4 to itself, as Aphex's PRESET.
- **CHOIR**: UNISON, ECHO and PRESSURE get a column each (ECHO's DELAY had been in UNISON's column).
- The screen itself was already displays on the left, controls on the right; unchanged.

Made in the design (convert.py MAPS `qlinks`) and re-converted, so a rerun keeps it.

### Presets
The 12 singers are VST programs: they show in MPC's PRESET menu by name (since the PRESET-menu change).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).

Files: [`schwung/instruments/monksynth/`](../schwung/instruments/monksynth/)

---

## Design QA: Mono Voice

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

### What changed
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

### Presets
Its built-in patch library (Chrome Bass, Wide Current, Hollow Wire, PWM Basin, Glass Choir, Just Fifths, Arcade Lead, Dust Pulse, Scan Bell, Circuit Reed, Metal Key, Soft Operator) is now in MPC's PRESET menu after an Init, and on a new PATCH selector under MACHINE
(Q-Link column 1). That took a small patch to the plugin shell (see the README's "Changes for the MPC").

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).

Files: [`schwung/instruments/monovoice/`](../schwung/instruments/monovoice/)

---

## Design QA: Moog

Status: **passed on the device** before this batch (MAIN is the batch's Q-Link reference); the fix since
(2026-10-03) passed too (2026-10-04, ✅ in the README).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 CUTOFF, EMPHASIS, CONTOUR, KEY TRACK  ·  2 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  3 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  4 PATCH | 1 CUTOFF, EMPHASIS, CONTOUR, KEY TRACK  ·  2 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  3 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  4 PATCH (unchanged) |
| OSCILLATORS | 1 NOISE, OSC1 WAVE, OSC1 RANGE, OSC1 LEVEL  ·  2 OSC2 WAVE, OSC2 RANGE, OSC2 DETUNE, OSC2 LEVEL  ·  3 OSC3 WAVE, OSC3 RANGE, OSC3 DETUNE, OSC3 LEVEL  ·  4 OSC4 WAVE, OSC4 RANGE, OSC4 DETUNE, OSC4 LEVEL | 1 NOISE, OSC1 WAVE, OSC1 RANGE, OSC1 LEVEL  ·  2 OSC2 WAVE, OSC2 RANGE, OSC2 DETUNE, OSC2 LEVEL  ·  3 OSC3 WAVE, OSC3 RANGE, OSC3 DETUNE, OSC3 LEVEL  ·  4 OSC4 WAVE, OSC4 RANGE, OSC4 DETUNE, OSC4 LEVEL (unchanged) |
| MODULATION | 1 LFO RATE, LFO PITCH, LFO FILTER, WHEEL FILTER  ·  2 WHEEL PITCH, GLIDE, BEND RANGE, VELOCITY  ·  3 VOLUME | 1 LFO RATE, LFO PITCH, LFO FILTER  ·  2 WHEEL FILTER, WHEEL PITCH  ·  3 GLIDE, BEND RANGE, VELOCITY  ·  4 VOLUME |

- **MODULATION**: four panels of 3, 2, 3 and 1 controls, but the Q-Links ran in fours, so column 1's outline took in
  the LFO and half of MOD WHEEL. Now LFO | MOD WHEEL | PLAYING | OUTPUT, a column each (the 3-2-4-3 kind of layout).
- **MAIN** and **OSCILLATORS**: unchanged (the reference).

Moog's screen doesn't reproduce exactly from a rerun of convert.py, so the change is made in `layout.conf` by hand
(see its header) and also written in convert.py MAPS `qlinks`.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page now (qlink_overlay.py).

Files: [`schwung/instruments/moog/`](../schwung/instruments/moog/)

---

## Design QA: Mr Hyde

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right; groups of up to four, 3-2-4-3 style where a group is smaller).

### What changed
Every page had its Q-Links in plain page order, so columns ran across panels: on MAIN, column 2 was MORPH + the LPG
+ FILTER MODE; on the modulation pages, rows of six spilled into the next row.

Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 MODEL, PITCH, HARMONICS, TIMBRE  ·  2 MORPH, LPG DECAY, LPG COLOR, FILTER MODE  ·  3 CUTOFF FREQ, RESONANCE, FM AMOUNT, AUX MIX  ·  4 VOLUME, PAN | 1 MODEL, FM AMOUNT, AUX MIX  ·  2 PITCH, HARMONICS, TIMBRE, MORPH  ·  3 FILTER MODE, CUTOFF FREQ, RESONANCE  ·  4 LPG DECAY, LPG COLOR, VOLUME, PAN |
| LFO ENV | 1 LFO SHAPE, LFO RATE, LFO SYNC, LFO RETRIG  ·  2 LFO PHASE, VEL CURVE, AT CURVE, ENV ATTACK  ·  3 ENV DECAY, ENV SUSTAIN, ENV RELEASE, ENV RETRIG | 1 LFO SHAPE, LFO RATE, LFO PHASE, LFO SYNC  ·  2 VEL CURVE, AT CURVE  ·  3 ENV ATTACK, ENV DECAY, ENV SUSTAIN, ENV RELEASE  ·  4 LFO RETRIG, ENV RETRIG |
| CYC RAND | 1 CYC ATTACK, CYC DECAY, CYC SHAPE, CYC SYNC  ·  2 CYC RETRIG, CYC BIPOLAR, RND MODE, RND RATE  ·  3 RND SYNC, RND SLEW, RND RETRIG | 1 CYC ATTACK, CYC DECAY, CYC SHAPE  ·  2 CYC SYNC, CYC RETRIG, CYC BIPOLAR  ·  3 RND MODE, RND RATE, RND SLEW  ·  4 RND SYNC, RND RETRIG |
| ASSIGN | 1 A1 TARGET, A1 LFO, A1 ENV, A1 CYCLE  ·  2 A1 RANDOM, A1 VEL, A1 AT, A2 TARGET  ·  3 A2 LFO, A2 ENV, A2 CYCLE, A2 RANDOM  ·  4 A2 VEL, A2 AT | 1 A1 TARGET, A1 LFO, A1 ENV, A1 CYCLE  ·  2 A1 RANDOM, A1 VEL, A1 AT  ·  3 A2 TARGET, A2 LFO, A2 ENV, A2 CYCLE  ·  4 A2 RANDOM, A2 VEL, A2 AT |
| PITCH HARM | 1 PITCH LFO, PITCH ENV, PITCH CYCLE, PITCH RANDOM  ·  2 PITCH VEL, PITCH AT, HARM LFO, HARM ENV  ·  3 HARM CYCLE, HARM RANDOM, HARM VEL, HARM AT | 1 PITCH LFO, PITCH ENV, PITCH CYCLE  ·  2 PITCH RANDOM, PITCH VEL, PITCH AT  ·  3 HARM LFO, HARM ENV, HARM CYCLE  ·  4 HARM RANDOM, HARM VEL, HARM AT |
| TIMB CUT | 1 TIMB LFO, TIMB ENV, TIMB CYCLE, TIMB RANDOM  ·  2 TIMB VEL, TIMB AT, CUT LFO, CUT ENV  ·  3 CUT CYCLE, CUT RANDOM, CUT VEL, CUT AT | 1 TIMB LFO, TIMB ENV, TIMB CYCLE  ·  2 TIMB RANDOM, TIMB VEL, TIMB AT  ·  3 CUT LFO, CUT ENV, CUT CYCLE  ·  4 CUT RANDOM, CUT VEL, CUT AT |
| VOICE | 1 VOICE MODE, POLYPHONY, UNISON, DETUNE  ·  2 SPREAD, GLIDE | 1 VOICE MODE, POLYPHONY, GLIDE  ·  2 UNISON, DETUNE, SPREAD |

Layout:
- **MAIN**: five panels became four, one per Q-Link column, in a 2 × 2 grid as Moog's MAIN:
  - **PLAITS ENGINE** (top left): the model scope on the left, MODEL, FM AMOUNT and AUX MIX on the right (FM and AUX
    are the Plaits engine's own inputs).
  - **OSCILLATOR** (top right): PITCH, HARMONICS, TIMBRE, MORPH.
  - **FILTER** (bottom left): unchanged.
  - **LPG / OUTPUT** (bottom right): the LPG's two knobs and MASTER OUT's VOLUME and PAN in one row. The made-up
    stereo meter went.
- **LFO ENV**: LFO | CURVES on top, ENVELOPE | RETRIG below. The two RETRIG switches share a small panel, so the
  LFO and the envelope are four controls each.
- **CYC RAND**: each half split into its settings (CYCLE ENVELOPE, RANDOM) and its switches (CYCLE OPTIONS,
  RANDOM OPTIONS).
- **ASSIGN, PITCH HARM, TIMB CUT**: each row of six sources split into two groups with a gap: LFO / ENV / CYCLE
  (with ASSIGN's target in front) | RANDOM / VEL / AT.
- **VOICE**: VOICE (mode, polyphony, glide) | UNISON (unison, detune, spread), and a MR HYDE wordmark fills the
  empty lower half.

MAIN is made in the design (convert.py MAPS `augment`, using a new `move` op in extract.py that moves the design's
own controls between panels); the other pages in the port's page plan (`layout.grid.conf`). A rerun keeps both.

### Presets
Upstream has none, so the port brings 19 (`presets.json`, in MPC's PRESET menu): Init, Freak Bass, Wobble Bass, Sync Lead, Terrain Pad, String Machine, Supersaw Stack, Folded Pluck, FM Keys, Formant Choir, Drawbar Organ, Wavetable Sweep, Chord Memory, Speech Synth, Swarm, Noise Sweep, Particle Rain, Glass String, Modal Bells. Each sets all 81 controls;
offline all 19 play. VOLUME drives the low-pass gate (it saturates above about 1.3), so it evens the levels out only so far:
the organ, string machine, inharmonic string and particle presets stay quieter by nature.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).

Files: [`schwung/instruments/mrhyde/`](../schwung/instruments/mrhyde/)

---

## Design QA: Noisemaker

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

### What changed
Every page had its Q-Links in plain page order, so most columns ran across two or three panels (on OSC, column 4
was OSC2 PHASE + OSC1 WAVE + OSC SYNC + OSC2 WAVE, from three corners of the page).

Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 CUTOFF, RESONANCE, FILTER ENV, VOLUME  ·  2 VOICES, PORTAMENTO, PORTA MODE, AMP ATTACK  ·  3 AMP DECAY, AMP SUSTAIN, AMP RELEASE, PATCH  ·  4 FILTER TYPE, BANK | 1 FILTER TYPE, CUTOFF, RESONANCE, FILTER ENV  ·  2 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  3 VOLUME, VOICES, PORTAMENTO, PORTA MODE  ·  4 PATCH, BANK |
| OSC | 1 OSC1 TUNE, OSC1 FINE, OSC1 PW, OSC2 TUNE  ·  2 OSC2 FINE, OSC2 FM, OSC1 LEVEL, OSC2 LEVEL  ·  3 SUB LEVEL, RING MOD, MASTER TUNE, OSC1 PHASE  ·  4 OSC2 PHASE, OSC1 WAVE, OSC SYNC, OSC2 WAVE | 1 OSC1 WAVE, OSC1 TUNE, OSC1 FINE, OSC1 PW  ·  2 OSC2 WAVE, OSC2 TUNE, OSC2 FINE, OSC2 FM  ·  3 OSC1 LEVEL, OSC2 LEVEL, SUB LEVEL, RING MOD  ·  4 OSC SYNC, MASTER TUNE, OSC1 PHASE, OSC2 PHASE |
| FILTER | 1 KEY TRACK, DRIVE, HIGH PASS, VEL CUTOFF  ·  2 DETUNE, VINTAGE, BITCRUSH, FLT TIME  ·  3 AMP TIME, VEL VOLUME, VEL ENV, FLT ATTACK  ·  4 FLT DECAY, FLT SUSTAIN, FLT RELEASE | 1 KEY TRACK, DRIVE, HIGH PASS, VEL CUTOFF  ·  2 DETUNE, VINTAGE, BITCRUSH  ·  3 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  4 FLT TIME, AMP TIME, VEL VOLUME, VEL ENV |
| LFO | 1 LFO1 RATE, LFO1 AMOUNT, LFO1 PHASE, LFO2 RATE  ·  2 LFO2 AMOUNT, LFO2 PHASE, LFO1 SYNC, LFO1 KEYTRIG  ·  3 LFO2 SYNC, LFO2 KEYTRIG, LFO1 WAVE, LFO1 DEST  ·  4 LFO2 WAVE, LFO2 DEST | 1 LFO1 WAVE, LFO1 RATE, LFO1 AMOUNT, LFO1 DEST  ·  2 LFO1 SYNC, LFO1 KEYTRIG, LFO1 PHASE  ·  3 LFO2 WAVE, LFO2 RATE, LFO2 AMOUNT, LFO2 DEST  ·  4 LFO2 SYNC, LFO2 KEYTRIG, LFO2 PHASE |
| MOD | 1 ENV3 ATTACK, ENV3 DECAY, ENV3 AMOUNT, DRAW AMOUNT  ·  2 WHEEL CUTOFF, BEND RANGE, CHORUS I, CHORUS II  ·  3 ENV3 DEST, DRAW SPEED, DRAW DEST | 1 ENV3 ATTACK, ENV3 DECAY, ENV3 AMOUNT, ENV3 DEST  ·  2 DRAW AMOUNT, DRAW SPEED, DRAW DEST  ·  3 WHEEL CUTOFF, BEND RANGE  ·  4 CHORUS I, CHORUS II |
| FX | 1 REVERB WET, REV DECAY, REV PREDELAY, REV HI CUT  ·  2 REV LO CUT, DELAY WET, DELAY TIME, DLY FEEDBACK  ·  3 DLY HI CUT, DLY LO CUT, DELAY SYNC, DELAY 2X L  ·  4 DELAY 2X R | 1 REVERB WET, REV DECAY, REV PREDELAY  ·  2 REV HI CUT, REV LO CUT  ·  3 DELAY WET, DLY FEEDBACK, DLY HI CUT, DLY LO CUT  ·  4 DELAY TIME, DELAY SYNC, DELAY 2X L, DELAY 2X R |

Layout:
- **MAIN**: FILTER | AMP ENVELOPE | VOICE & MASTER | PATCH + BANK (the patch last, as on Aphex and MonkSynth).
  No controls moved; the curve and scope displays were already on the left.
- **OSC**: RING MOD moved into MIXER GAIN (it's a level in the mix), so the four panels have four controls each.
- **FILTER**: a column per panel as drawn.
- **LFO**: each LFO split into WAVE / RATE / AMOUNT / DEST and SYNC / KEYTRIG / PHASE (they already sat apart).
- **MOD**: ENVELOPE 3 | ENVELOPE DRAW | WHEEL / BEND | CHORUS.
- **FX**: reverb split into WET / DECAY / PREDELAY and its HI / LO CUT; delay into level and tone (WET, FEEDBACK,
  HI CUT, LO CUT) and time (TIME, SYNC, 2X L, 2X R), with the controls moved to match.
- The slider strips were rebuilt with the slider fix (the "silly" slider animation seen on Braids).

Noisemaker's screen is a hand-edited `layout.conf` (a rerun of convert.py doesn't reproduce it; see its header), so
these changes are in `layout.conf` too.

### Presets
Its factory presets and any banks you add are in MPC's PRESET menu (unchanged).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).

Files: [`schwung/instruments/noisemaker/`](../schwung/instruments/noisemaker/)

---

## Design QA: NuSaw

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 PATCH, SAWS, DETUNE, SPREAD  ·  2 SUB LEVEL, CUTOFF, RESONANCE, ENV MOD  ·  3 ATTACK, DECAY, SUSTAIN, RELEASE | 1 SAWS, DETUNE, SPREAD, SUB LEVEL  ·  2 CUTOFF, RESONANCE, ENV MOD  ·  3 ATTACK, DECAY, SUSTAIN, RELEASE  ·  4 PATCH |
| MORE | 1 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  2 SUB OCTAVE, VELOCITY, BEND RANGE, VOLUME  ·  3 CHORUS, CHORUS DEPTH, DELAY, DELAY TIME  ·  4 DELAY FBK, DELAY TONE | 1 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  2 SUB OCTAVE, VELOCITY, BEND RANGE, VOLUME  ·  3 CHORUS, CHORUS DEPTH  ·  4 DELAY, DELAY TIME, DELAY FBK, DELAY TONE |

- **MAIN**: column 1's outline took in the PROGRAM and SAWS panels, column 2's SAWS and FILTER. Now SAWS | FILTER |
  AMP ENVELOPE | PATCH (the patch last, as on Aphex and MonkSynth). The scope moved to the left of the PROGRAM panel
  and the filter curve to the left of the FILTER panel, with the controls on the right.
- **MORE**: CHORUS and DELAY get a column each (the delay had been split over two). A NUSAW wordmark fills the empty
  corner.
- The ADSR slider strips were rebuilt with the slider fix (the "silly" slider animation seen on Braids).

Made in the design (convert.py MAPS `augment` / `qlinks`, and an `art` plate) and re-converted, so a rerun keeps it.

### Presets
27 built-in patches, in MPC's PRESET menu (unchanged).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).

Files: [`schwung/instruments/nusaw/`](../schwung/instruments/nusaw/)

---

## Design QA: OB-Xd

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right). Known before: MAIN's and ENVELOPES' outlines overlapped.

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 PATCH, VOLUME, TUNE, VOICES  ·  2 SPREAD, UNISON, AS PLAYED, LEGATO  ·  3 PORTAMENTO, BEND 12, BEND OSC2, BANK | 1 VOLUME, TUNE  ·  2 VOICES, SPREAD, UNISON, AS PLAYED  ·  3 PORTAMENTO, LEGATO, BEND 12, BEND OSC2  ·  4 PATCH, BANK |
| OSCILLATORS | 1 OSC1 PITCH, OSC1 SAW, OSC1 PULSE, OSC2 PITCH  ·  2 OSC2 DETUNE, OSC2 SAW, OSC2 PULSE, OSC2 SYNC  ·  3 PULSE WIDTH, PW OFFSET, PW ENV, PW ENV BOTH  ·  4 X-MOD, BRIGHTNESS, OSC2 STEP | 1 OSC1 PITCH, OSC1 SAW, OSC1 PULSE  ·  2 OSC2 PITCH, OSC2 DETUNE, OSC2 SAW, OSC2 PULSE  ·  3 PULSE WIDTH, PW OFFSET, PW ENV, PW ENV BOTH  ·  4 X-MOD, BRIGHTNESS, OSC2 SYNC, OSC2 STEP |
| FILTER | 1 OSC1 LEVEL, OSC2 LEVEL, NOISE, CUTOFF  ·  2 RESONANCE, ENV AMOUNT, KEY TRACK, MULTIMODE  ·  3 BANDPASS, 24 dB, SELF OSC, ENV INVERT  ·  4 FILTER VAR, GLIDE VAR, ENV VAR, LEVEL VAR | 1 OSC1 LEVEL, OSC2 LEVEL, NOISE  ·  2 CUTOFF, RESONANCE, ENV AMOUNT, KEY TRACK  ·  3 MULTIMODE, BANDPASS, 24 dB, SELF OSC  ·  4 FILTER VAR, GLIDE VAR, ENV VAR, LEVEL VAR |
| ENVELOPES | 1 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  2 FLT VEL, AMP ATTACK, AMP DECAY, AMP SUSTAIN  ·  3 AMP RELEASE, AMP VEL | 1 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE  ·  2 FLT VEL, ENV INVERT  ·  3 AMP ATTACK, AMP DECAY, AMP SUSTAIN, AMP RELEASE  ·  4 AMP VEL |
| MODULATION | 1 LFO RATE, LFO SYNC, LFO SINE, LFO SQUARE  ·  2 LFO S&H, PITCH ENV, P.ENV BOTH, VIBRATO  ·  3 MOD AMOUNT, MOD OSC1, MOD OSC2, MOD FILTER  ·  4 PWM AMOUNT, PWM OSC1, PWM OSC2 | 1 LFO RATE, LFO SINE, LFO SQUARE, LFO S&H  ·  2 PITCH ENV, P.ENV BOTH, VIBRATO  ·  3 MOD AMOUNT, MOD OSC1, MOD OSC2, MOD FILTER  ·  4 PWM AMOUNT, PWM OSC1, PWM OSC2 |

Layout:
- **MAIN**: MASTER | VOICE & POLYPHONY | GLIDE // BEND | PATCH + BANK (the patch last, as on Aphex and MonkSynth).
  LEGATO MODE moved from VOICE & POLYPHONY into GLIDE // BEND, so VOICE has four controls (VOICES, SPREAD, UNISON,
  AS PLAYED) and GLIDE four (PORTAMENTO, LEGATO, BEND 12, BEND OSC2); the bottom row is now two equal panels like
  the top. The made-up OUTPUT PEAK meter (it never moved) went.
- **OSCILLATORS**: OSC2 SYNC moved from OSC 2 into OSC MOD (with X-MOD, the other oscillator-to-oscillator
  control), so OSC 2 and OSC MOD have four each.
- **FILTER**: MULTIMODE moved into FILTER MODE (with BANDPASS, 24 dB, SELF OSC); ENV INVERT moved to ENVELOPES.
- **ENVELOPES**: each envelope's A / D / S / R is one column and its velocity another; ENV INVERT (it flips the
  filter envelope) sits with FLT VEL.
- **MODULATION**: 15 controls, so one had to be touch only: **LFO SYNC** (it's set once per patch). It sits to the
  right of the LFO's column (RATE, SINE, SQUARE, S&H) so that column's outline doesn't take it in.

MAIN is made in the design (convert.py MAPS `augment`, with the new `move` op); the other pages in the port's page
plan (`layout.grid.conf`). A rerun keeps both.

### Presets
Factory patches and any .fxb banks you add are in MPC's PRESET menu, with BANK on MAIN (unchanged).

### For the owner
- LFO SYNC is the one OB-Xd control without a Q-Link. If you'd rather lose a different one (P.ENV BOTH, say), it's a
  one-line change.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).

Files: [`schwung/instruments/obxd/`](../schwung/instruments/obxd/)

---

## Design QA: Plaits

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| PLAITS | 1 MODEL, HARMONICS, TIMBRE, MORPH  ·  2 LPG DECAY, LPG COLOUR, ATTACK, FM  ·  3 TIMBRE MOD, MORPH MOD, AUX MIX, OCTAVE | 1 MODEL, OCTAVE  ·  2 HARMONICS, TIMBRE, MORPH  ·  3 LPG DECAY, LPG COLOUR, ATTACK  ·  4 FM, TIMBRE MOD, MORPH MOD, AUX MIX |
| PLAY | 1 FM PATCH, LEGATO, VELOCITY | 1 FM PATCH  ·  2 LEGATO, VELOCITY, VOLUME |

- **PLAITS**: five panels for twelve controls, so columns ran across them (column 1 took in MODEL and SOUND MACROS;
  column 2 LOW PASS GATE and MODULATION). Now four panels, a column each:
  - **MODEL**: MODEL and OCTAVE (OCTAVE moved here from OUTPUT; the made-up "16 ALGORITHMS / PITCH" footer went).
  - **SOUND MACROS**: HARMONICS, TIMBRE, MORPH.
  - **LOW PASS GATE**: LPG DECAY, LPG COLOUR, ATTACK.
  - **MODULATION / AUX**: FM, TIMBRE MOD, MORPH MOD and AUX MIX (moved here; the OUTPUT panel went).
- **PLAY**: 6-OP FM PATCH | PLAYING (LEGATO, VELOCITY, and VOLUME since 2026-10-04), a column each. FM PATCH shows the patch's name while a 6-Op
  FM model is selected, its number otherwise.

Made in the design (convert.py MAPS `augment`, with the new `move` op, and `qlinks`), so a rerun keeps it.

### Presets
Upstream ships only Move "chain patches" (whole Move chains), so the port brings 24 (`presets.json`, in MPC's PRESET
menu): Init, Analog Bass, Phase Lead, E-Piano, Mallets, Tubular Bells, Drawbar Organ, Glass Pad, Wave Terrain, String Machine, Chip Arp, Wavefolder, FM Bell, Grain Cloud, Additive Organ, Wavetable Sweep, Chord Stab, Talking Synth, Swarm Pad, Plucked String, Modal Bell, Kick, Snare, Hi-Hat. Plaits' models differ by about 30 dB and it had no level control, so a VOLUME parameter was added (appended
at the end, default = the old level) on the PLAY page, beside LEGATO and VELOCITY, and each preset sets it; offline all
24 play within a few dB (the plucked string varies from note to note).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).

Files: [`schwung/instruments/plaits/`](../schwung/instruments/plaits/)

---

## Design QA: Rings

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| RINGS | 1 MODEL, POLYPHONY, STRUCTURE, BRIGHTNESS  ·  2 DAMPING, POSITION, VELOCITY, OCTAVE  ·  3 BEND RANGE, SYNTH FX, WIDTH, VOLUME | 1 MODEL, POLYPHONY  ·  2 STRUCTURE, BRIGHTNESS, DAMPING, POSITION  ·  3 VELOCITY, OCTAVE, BEND RANGE, SYNTH FX  ·  4 WIDTH, VOLUME |

- The columns ran across panels (column 1 took in MODEL and half of RESONATOR; column 2 the rest of RESONATOR and
  half of PLAYING). Now MODEL | RESONATOR | PLAYING | OUTPUT, a column each.
- **MODEL**: the dispersion display moved to the left, MODEL and POLYPHONY to the right; its made-up readouts
  (FUNDAMENTAL, DISPERSION, 64 MODES) went.
- **OUTPUT**: the made-up peak meter (it never moved) went; WIDTH and VOLUME are centred.

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

### Presets
The module has none, so the port brings 14 (`presets.json`, the wrapper's own presets, in MPC's PRESET menu): Init, Glass Marimba, Tubular Bell, Wood Block, Sympathetic Sitar, Chord Harp, Nylon String, Steel String, Dulcimer, FM Tines, FM Gong, Verb String, Synth Strings, Choir Pad. Each sets every control; offline all 14 play, levels evened out with VOLUME (the short, percussive ones a little lower).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).

Files: [`mutable-instruments/rings/`](../mutable-instruments/rings/)

---

## Design QA: Tablor

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right; groups of up to four, 3-2-4-3 style where a group is smaller).

### What changed
Every page had its Q-Links in plain page order, so on all but VOICE the columns ran from one panel into the next.

Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 WT1 TABLE, WT1 POSITION, WT1 LEVEL, WT1 TUNE  ·  2 WT1 UNISON, WT2 TABLE, WT2 POSITION, WT2 LEVEL  ·  3 WT2 TUNE, WT2 UNISON | 1 WT1 TABLE  ·  2 WT1 POSITION, WT1 LEVEL, WT1 TUNE, WT1 UNISON  ·  3 WT2 TABLE  ·  4 WT2 POSITION, WT2 LEVEL, WT2 TUNE, WT2 UNISON |
| FILTER | 1 CUTOFF, RESONANCE, FILTER TYPE, FILTER ENV  ·  2 KEY TRACK, VEL TRACK, SUB LEVEL, SUB WAVE  ·  3 SUB TUNE, NOISE LEVEL, NOISE TYPE | 1 CUTOFF, RESONANCE, FILTER TYPE  ·  2 FILTER ENV, KEY TRACK, VEL TRACK  ·  3 SUB LEVEL, SUB WAVE, SUB TUNE  ·  4 NOISE LEVEL, NOISE TYPE |
| SHAPE | 1 WT1 DETUNE, WT1 SPREAD, WT1 PAN, WT2 DETUNE  ·  2 WT2 SPREAD, WT2 PAN, WT1 BEND, WT1 FORMANT  ·  3 WT2 BEND, WT2 FORMANT | 1 WT1 DETUNE, WT1 SPREAD, WT1 PAN  ·  2 WT2 DETUNE, WT2 SPREAD, WT2 PAN  ·  3 WT1 BEND, WT1 FORMANT  ·  4 WT2 BEND, WT2 FORMANT |
| ENVELOPES | 1 VCA ATTACK, VCA DECAY, VCA SUSTAIN, VCA RELEASE  ·  2 VELOCITY, FLT ATTACK, FLT DECAY, FLT SUSTAIN  ·  3 FLT RELEASE | 1 VCA ATTACK, VCA DECAY, VCA SUSTAIN, VCA RELEASE  ·  2 VELOCITY  ·  3 FLT ATTACK, FLT DECAY, FLT SUSTAIN, FLT RELEASE |
| MOD ENVS | 1 EG1 A, EG1 D, EG1 S, EG1 R  ·  2 EG1 DST, EG1 AMT, EG2 A, EG2 D  ·  3 EG2 S, EG2 R, EG2 DST, EG2 AMT | 1 EG1 A, EG1 D, EG1 S, EG1 R  ·  2 EG1 DST, EG1 AMT  ·  3 EG2 A, EG2 D, EG2 S, EG2 R  ·  4 EG2 DST, EG2 AMT |
| VOICE | 1 VOICE MODE, VOICES, GLIDE, GLIDE MODE  ·  2 LEGATO, BEND RANGE, VOLUME | 1 VOICE MODE, VOICES, LEGATO  ·  2 GLIDE, GLIDE MODE  ·  3 BEND RANGE, VOLUME  ·  4 PRESET |

Layout:
- **MAIN**: each oscillator's TABLE (with its display under it, on the left) is a column, and its four knobs (on the
  right) another. No controls moved.
- **FILTER**: CUTOFF / RESONANCE / FILTER TYPE and FILTER ENV / KEY TRACK / VEL TRACK as two groups with a gap; SUB
  / NOISE split into a SUB panel and a NOISE panel.
- **SHAPE**: a panel per oscillator and job: WT1 UNISON | WT2 UNISON / WT1 SHAPE | WT2 SHAPE.
- **ENVELOPES**: AMP ENV's four and its VELOCITY in panels of their own; FILTER ENV's four; a TABLOR wordmark in the
  empty corner.
- **MOD ENVS**: per envelope, A / D / S / R and DST / AMT as two groups with a gap.
- **VOICE**: VOICE (mode, voices, legato) | GLIDE (glide, glide mode) | MASTER (bend range, volume), and (2026-10-04) a
  PRESET panel in the lower half.

MAIN is made in the design (convert.py MAPS `qlinks`); the other pages in the port's page plan (`layout.grid.conf`,
plus an `art` plate for the wordmark). A rerun keeps both.

### Presets
Its 9 factory presets (upstream's `factory.tbl`: Init, First Contact, Neu Bass, Formant Keys, Dust Pad, Sub Punch, Glass Bells, Res Bass, E Piano) are now in MPC's PRESET menu, and on the VOICE page's new
PRESET panel (Q-Link column 4), which takes the lower half where the wordmark was. That took a small engine patch (see
the README's "Changes for the MPC").

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).

Files: [`schwung/instruments/tablor/`](../schwung/instruments/tablor/)

---

## Design QA: Wurl

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| WURL | 1 PRESET, BRIGHT, DARKEN, BARK  ·  2 TUNE, ATTACK, DECAY, VOLUME  ·  3 TREMOLO, SPEAKER, REVERB | 1 BRIGHT, DARKEN, BARK, TUNE  ·  2 ATTACK, DECAY, VOLUME  ·  3 TREMOLO, SPEAKER, REVERB  ·  4 PRESET |

- **Q-Links**: the columns ran across panels (column 1 was PRESET + three TONE MATRIX knobs; column 2 took in TUNE
  and the whole amplifier). Now TONE MATRIX | SOLID STATE AMPLIFIER | CABINET | PRESET (the preset last, as on
  Aphex), a column each.
- **Displays on the left**: the reed-harmonics display moved to the left of TONE MATRIX and the photo-cell display
  to the left of CABINET, with the knobs on the right.
- **No maker badges** (the repo's rule): the emblem plate read "THE ORIGINAL / Wurlitzer / ELECTRONIC PIANO / 200 A"
  with a serial number, the factory's town and "AKAI PROFESSIONAL STANDALONE ENGINE", and the preset panel said
  "Akai MPC Standalone DSP Core". The plate now reads WURL / ELECTRIC PIANO / 200 A (naming the model it emulates
  is fine); the rest went.

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

### Presets
Its built-in presets are in MPC's PRESET menu (unchanged).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).

Files: [`schwung/instruments/wurl/`](../schwung/instruments/wurl/)

---

## Design QA: Libpo32

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; utility buttons in their own place and off the Q-Links).

### What changed
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

### Presets
Its kits are in MPC's PRESET menu (unchanged).

### For the owner
- EDIT PAD has no Q-Link; touch it to pick the pad to edit.

### Checked
Offline test PASSED; check_skin OK (two small touch overlaps between EDIT's stacked knobs, as before);
no Q-Link outlines overlap on any page (qlink_overlay.py).

Files: [`schwung/instruments/libpo32/`](../schwung/instruments/libpo32/)

---

## Design QA: Mr Drums

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| KIT | 1 KIT, MASTER VOL, POLYPHONY, VEL CURVE  ·  2 HUMANIZE, AUTO SELECT, EDIT PAD, RAND LOOP | 1 MASTER VOL, POLYPHONY, VEL CURVE, HUMANIZE  ·  2 EDIT PAD, AUTO SELECT  ·  3 RAND LOOP  ·  4 KIT |
| PAD | 1 EDIT PAD, PAD VOLUME, PAD PAN, PAD TUNE  ·  2 PAD START, PAD MODE, CHOKE GROUP, PAD ATTACK  ·  3 PAD DECAY, RAND VOLUME, RAND PAN, RAND DECAY  ·  4 CHANCE | 1 EDIT PAD  ·  2 PAD VOLUME, PAD PAN, PAD TUNE, PAD START  ·  3 PAD MODE, CHOKE GROUP, PAD ATTACK, PAD DECAY  ·  4 RAND VOLUME, RAND PAN, RAND DECAY, CHANCE |

- **KIT**: MASTER ENGINE | 16-PAD MATRIX (EDIT PAD, AUTO SELECT) | RAND LOOP | KIT (the kit last, as a preset), a
  column each. AUTO SELECT moved from the panel header's corner to just above EDIT PAD, so its outline stays inside
  the panel.
- **PAD**: PAD (EDIT PAD) | SOUND (VOLUME, PAN, TUNE, START) / PLAYBACK (PAD MODE, CHOKE GROUP, ATTACK, DECAY) |
  RANDOM (VOLUME, PAN, DECAY, CHANCE). SOUND had six controls, so PAD MODE and CHOKE GROUP joined the envelope in a
  PLAYBACK panel (how a hit plays and ends).

KIT is made in the design (convert.py MAPS `augment`, with the new `move` op, and `qlinks`); PAD in the port's page
plan (`layout.grid.conf`). A rerun keeps both.

### Presets
Its kits are in MPC's PRESET menu (unchanged).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).

Files: [`schwung/instruments/mrdrums/`](../schwung/instruments/mrdrums/)

---

## Design QA: Eucalypso

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel or group, as Moog; outlines that frame
one panel; displays on the left, controls on the right).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 SYNC, RATE, BPM, SWING  ·  2 PLAY MODE, RETRIGGER, VOICES, RAND CYCLE  ·  3 VELOCITY, VEL RANDOM, GATE, GATE RANDOM | 1 SYNC, RATE, BPM, SWING  ·  2 PLAY MODE, RETRIGGER, VOICES, RAND CYCLE  ·  3 VELOCITY, VEL RANDOM, GATE, GATE RANDOM (unchanged) |
| LANE 1 | 1 L1 ON, L1 STEPS, L1 PULSES, L1 ROTATE  ·  2 L1 DROP, L1 DROP SEED, L1 VELOCITY, L1 GATE  ·  3 L1 NOTE, L1 NOTE RND, L1 NOTE SEED, L1 OCTAVE  ·  4 L1 OCT RND, L1 OCT SEED, L1 OCT RANGE | 1 L1 ON, L1 STEPS, L1 PULSES, L1 ROTATE  ·  2 L1 DROP, L1 DROP SEED, L1 VELOCITY, L1 GATE  ·  3 L1 NOTE, L1 NOTE RND, L1 NOTE SEED  ·  4 L1 OCTAVE, L1 OCT RND, L1 OCT SEED, L1 OCT RANGE |
| LANE 2 | 1 L2 ON, L2 STEPS, L2 PULSES, L2 ROTATE  ·  2 L2 DROP, L2 DROP SEED, L2 VELOCITY, L2 GATE  ·  3 L2 NOTE, L2 NOTE RND, L2 NOTE SEED, L2 OCTAVE  ·  4 L2 OCT RND, L2 OCT SEED, L2 OCT RANGE | 1 L2 ON, L2 STEPS, L2 PULSES, L2 ROTATE  ·  2 L2 DROP, L2 DROP SEED, L2 VELOCITY, L2 GATE  ·  3 L2 NOTE, L2 NOTE RND, L2 NOTE SEED  ·  4 L2 OCTAVE, L2 OCT RND, L2 OCT SEED, L2 OCT RANGE |
| LANE 3 | 1 L3 ON, L3 STEPS, L3 PULSES, L3 ROTATE  ·  2 L3 DROP, L3 DROP SEED, L3 VELOCITY, L3 GATE  ·  3 L3 NOTE, L3 NOTE RND, L3 NOTE SEED, L3 OCTAVE  ·  4 L3 OCT RND, L3 OCT SEED, L3 OCT RANGE | 1 L3 ON, L3 STEPS, L3 PULSES, L3 ROTATE  ·  2 L3 DROP, L3 DROP SEED, L3 VELOCITY, L3 GATE  ·  3 L3 NOTE, L3 NOTE RND, L3 NOTE SEED  ·  4 L3 OCTAVE, L3 OCT RND, L3 OCT SEED, L3 OCT RANGE |
| LANE 4 | 1 L4 ON, L4 STEPS, L4 PULSES, L4 ROTATE  ·  2 L4 DROP, L4 DROP SEED, L4 VELOCITY, L4 GATE  ·  3 L4 NOTE, L4 NOTE RND, L4 NOTE SEED, L4 OCTAVE  ·  4 L4 OCT RND, L4 OCT SEED, L4 OCT RANGE | 1 L4 ON, L4 STEPS, L4 PULSES, L4 ROTATE  ·  2 L4 DROP, L4 DROP SEED, L4 VELOCITY, L4 GATE  ·  3 L4 NOTE, L4 NOTE RND, L4 NOTE SEED  ·  4 L4 OCTAVE, L4 OCT RND, L4 OCT SEED, L4 OCT RANGE |
| NOTES | 1 REGISTER, NOTE ORDER, MISSING NOTE, SCALE  ·  2 ROOT, SCALE RANGE, OCTAVE, ORDER SEED  ·  3 MISSING SEED, RANDOM SEED | 1 REGISTER, NOTE ORDER, MISSING NOTE  ·  2 SCALE, ROOT, SCALE RANGE, OCTAVE  ·  3 ORDER SEED, MISSING SEED, RANDOM SEED |

- **MAIN**: already a column per panel. The wheel display moved to the left of the top row, CLOCK & TRANSPORT to the
  right.
- **LANE 1-4**: the notes row split by meaning: NOTE / NOTE RND / NOTE SEED and OCTAVE / OCT RND / OCT SEED / OCT
  RANGE (it had been NOTE ... OCTAVE | the rest), with a gap between the groups. The rhythm row's two columns are
  unchanged.
- **NOTES**: REGISTER / NOTE ORDER / MISSING NOTE | SCALE / ROOT / SCALE RANGE / OCTAVE | the three SEEDS, a column
  each (the columns had run from one panel into the other).

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).

Files: [`schwung/sequencers/eucalypso/`](../schwung/sequencers/eucalypso/)

---

## Design QA: Grids

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right; 3-2-4-3 style groups).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| GRIDS | 1 MAP X, MAP Y, CHAOS, BD FILL  ·  2 SD FILL, HH FILL, MODE, SWING  ·  3 BD LEN, SD LEN, HH LEN | 1 MAP X, MAP Y, CHAOS  ·  2 BD FILL, SD FILL, HH FILL  ·  3 MODE, SWING  ·  4 BD LEN, SD LEN, HH LEN |
| NOTES | 1 BD NOTE, SD NOTE, HH NOTE, ACCENT VEL  ·  2 NORMAL VEL, CHANNEL, RESOLUTION | 1 BD NOTE, SD NOTE, HH NOTE  ·  2 ACCENT VEL, NORMAL VEL  ·  3 CHANNEL, RESOLUTION |

- **GRIDS**: MAP | DENSITY | ENGINE MODE | PATTERN LENGTHS, a column each (3-3-2-3); the fills had been split across
  two columns, and the second's outline took in the ENGINE panel. The rhythm display moved to the left of the
  lower row, PATTERN LENGTHS to the right.
- **NOTES**: DRUM NOTE MAP | VELOCITY | MIDI PORT, a column each (3-2-2); the first column had taken in an
  accent knob from the next panel.

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).

Files: [`mutable-instruments/grids/`](../mutable-instruments/grids/)

---

## Design QA: GrooveBank

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel or group, as Moog; outlines that frame
one panel).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 GROOVE, VARIANT, SWING, GATE  ·  2 STRUM, ACCENT, LATCH | 1 VARIANT, SWING, GATE  ·  2 STRUM, ACCENT, LATCH  ·  3 GROOVE |

- Column 1 had been GROOVE plus three FEEL knobs, so its outline took in both panels. Now FEEL's six controls are
  two columns (VARIANT / SWING / GATE and STRUM / ACCENT / LATCH) and GROOVE has the third (last, as a preset).
- A "HOLD SW" caption under the LATCH switch (it sat under MPC's own LATCH name) went, and the emblem's "MPC
  EMBEDDED DSP" reads "SCHWUNG MIDI FX".

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).

Files: [`schwung/sequencers/groovebank/`](../schwung/sequencers/groovebank/)

---

## Design QA: Marbles

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MARBLES | 1 T MODEL, CLOCK DIV, T BIAS, JITTER  ·  2 GATE LEN, DEJA VU, LENGTH, T DEJA VU  ·  3 SCALE, X DEJA VU, SPREAD, X BIAS  ·  4 STEPS, CHANNELS | 1 T MODEL, CLOCK DIV, T BIAS, JITTER  ·  2 DEJA VU, LENGTH, T DEJA VU, X DEJA VU  ·  3 SCALE, SPREAD, X BIAS, STEPS  ·  4 CHANNELS, GATE LEN |
| SETUP | 1 RATE BASE, T RANGE, GATE RAND, X RANGE  ·  2 X MODE, BASE NOTE, VELOCITY | 1 RATE BASE, T RANGE, GATE RAND  ·  2 X RANGE, X MODE  ·  3 BASE NOTE, VELOCITY |

- **MARBLES**: T RHYTHM and X PITCH had five Q-Link controls each, so every column ran into the next panel. Now
  T RHYTHM | DEJA VU | X PITCH | MIDI OUT, a column each:
  - X DEJA VU moved from X PITCH's title bar into the DEJA VU panel, beside T DEJA VU (where Marbles has it).
  - GATE LEN moved from T RHYTHM into MIDI OUT, beside CHANNELS (it sets the length of the notes sent).
  - The T and X displays moved to the left of their panels, the knobs to the right and up, so their values sit
    inside the panel.
  - The T1 > X1 / T2 > X2 / T3 > X3 routing switches stay touch only, as before.
- **SETUP**: CLOCK | X | MIDI, a column each; the empty OUTPUTS panel (its switches are on MARBLES) went.

Made in the design (convert.py MAPS `augment`, with the new `move` op, and `qlinks`) and the page plan
(`layout.grid.conf`), so a rerun keeps it.

### Checked
Offline test PASSED; check_skin OK (three small touch overlaps < 12 px); no Q-Link outlines overlap on either page
(qlink_overlay.py). The outlines of the two title-bar pop-ups (T MODEL / CLOCK DIV, SCALE) reach a little above their
panels: MPC leaves room above a pop-up.

Files: [`mutable-instruments/marbles/`](../mutable-instruments/marbles/)

---

## Design QA: MazeLite

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel or group, as Moog; outlines that frame
one panel; no maker badges).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAZE | 1 SCALE, NOTE RATE, NOTE LENGTH, RESET BOTH  ·  2 S1 RESET, S2 RESET, S1 CORRUPT, S1 RANGE  ·  3 S1 LENGTH, S1 TRIG MIX, S2 CORRUPT, S2 RANGE  ·  4 S2 LENGTH, S2 TRIG MIX | 1 SCALE, NOTE RATE, NOTE LENGTH, RESET BOTH  ·  2 S1 CORRUPT, S1 RANGE, S1 LENGTH, S1 TRIG MIX  ·  3 S2 CORRUPT, S2 RANGE, S2 LENGTH, S2 TRIG MIX  ·  4 S1 RESET, S2 RESET |

- Columns 2-4 had mixed the two RESET pop-ups (MIDI OUT panel) with the S1 and S2 knob rows (bottom strip), so the
  outlines covered half the page. Now OUTPUT | S1 | S2 | the RESETs, a column each.
- The S1 / S2 knob rows were packed so tight that MPC's names and values ran into each other: they're spaced out
  now, and their "S1 PARAMS:" / "S2 PARAMS:" captions went (MPC already names each knob S1 / S2 ...).
- The centre plate read "AKAI MPC LIVE II": now "SCHWUNG MIDI FX".
- FLIP / STEP stay touch only (they're triggers), as before.

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).

Files: [`schwung/sequencers/mazelite/`](../schwung/sequencers/mazelite/)

---

## Design QA: MIDI Player

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| PLAYER | 1 FILE, TRACK, LOOP | 1 TRACK, LOOP  ·  2 FILE |

- The one column (FILE, TRACK, LOOP) took in the FILE panel and PLAYBACK ENGINE. Now PLAYBACK ENGINE (TRACK,
  LOOP) | FILE, a column each (the file last, as a preset).

Made in the design (convert.py MAPS `qlinks`), so a rerun keeps it.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).

Files: [`schwung/sequencers/midiplayer/`](../schwung/sequencers/midiplayer/)

---

## Design QA: PixelWalkers

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; utility buttons in their own box and off the Q-Links).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 BIRTH NOTE, BIRTH LEVEL, HIT LEVEL, HIT DECAY  ·  2 BOUNCE, HARDNESS, TOMBOLA, RANDOMIZE  ·  3 KILL ALL | 1 BIRTH NOTE, BIRTH LEVEL, HIT LEVEL, HIT DECAY  ·  2 BOUNCE, HARDNESS, TOMBOLA |

- RANDOMIZE and KILL ALL were on the Q-Links (column 2 and 3), so turning a knob fired them. They're touch only now,
  in their ACTIONS box, as on Aphex.
- WALKERS | WORLD DYNAMICS, a column each; the WALKERS row moved in from the edges so its outline stays inside the
  panel (it reached into MIDI OUT).

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).

Files: [`schwung/sequencers/pixelwalkers/`](../schwung/sequencers/pixelwalkers/)

---

## Design QA: Rampage

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; displays on the left, controls on the right; trigger buttons off the Q-Links).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| RAMPAGE | 1 A RANGE, A RISE, A FALL, A SHAPE  ·  2 B RANGE, B RISE, B FALL, B SHAPE  ·  3 CYCLE A, A TRIGGER, BALANCE, CYCLE B  ·  4 B TRIGGER, AUDIO, VOLUME | 1 A RANGE, A RISE, A FALL, A SHAPE  ·  2 B RANGE, B RISE, B FALL, B SHAPE  ·  3 CYCLE A, BALANCE, CYCLE B  ·  4 AUDIO, VOLUME |
| MIDI | 1 A NOTES, B NOTES, A KEY TRACK, B KEY TRACK  ·  2 CC CHANNEL, CC OUT A, CC OUT B, CC MIN  ·  3 CC MAX, EOC NOTES, EOC NOTE A, EOC NOTE B | 1 A NOTES, B NOTES, A KEY TRACK, B KEY TRACK  ·  2 CC CHANNEL, CC OUT A, CC OUT B  ·  3 CC MIN, CC MAX  ·  4 EOC NOTES, EOC NOTE A, EOC NOTE B |

- **RAMPAGE**:
  - The channel scopes moved to the left of CHANNEL A and CHANNEL B, the knobs to the right.
  - CHANNEL A CONTROLS, LOGIC / COMP and CHANNEL B CONTROLS became one CYCLE / TRIGGER / BALANCE panel: TRIG A |
    CYCLE A, BALANCE, CYCLE B | TRIG B. The middle three are one Q-Link column (its outline had covered three
    panels); TRIG A / TRIG B are touch only (turning a knob fired them, as on Aphex).
  - The made-up output meters went.
- **MIDI**: NOTES IN | CC OUT (CHANNEL, OUT A, OUT B) / CC RANGE (MIN, MAX) | END OF CYCLE, a column each. CC OUT's
  five controls were split in two, and the empty MIDI OUT panel (its info plate never rendered) is now CC OUT.

RAMPAGE is made in the design (convert.py MAPS `augment`, with the new `move` op, and `qlinks`); MIDI in the port's
page plan (`layout.grid.conf`). A rerun keeps both.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py). The two RANGE
selectors sit in their panels' title bars, so those outlines reach a little above the panels.

Files: [`vcv-rack/rampage/`](../vcv-rack/rampage/)

---

## Design QA: SuperArp

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel or group, as Moog; outlines that frame
one panel; displays on the left, controls on the right).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| MAIN | 1 SYNC, RATE, TRIPLET, TEMPO (BPM)  ·  2 LATCH STATE, OCTAVES, GATE, VELOCITY  ·  3 SWING, VOICES | 1 SYNC, RATE, TRIPLET, TEMPO (BPM)  ·  2 LATCH STATE, OCTAVES  ·  3 GATE, VELOCITY, SWING, VOICES |
| PATTERN | 1 MODE, PATTERN, MODE TRIGGER, MISSING NOTE  ·  2 MODE SEED, RHYTHM, RHY TRIGGER, RND LENGTH  ·  3 RND CHORDS, RND CH SEED | 1 MODE, PATTERN  ·  2 MODE TRIGGER, MISSING NOTE, MODE SEED  ·  3 RHYTHM, RHY TRIGGER  ·  4 RND LENGTH, RND CHORDS, RND CH SEED |
| MODIFY | 1 MOD LOOP, MOD TRIGGER, DROP, DROP SEED  ·  2 VEL RANDOM, VEL SEED, GATE RANDOM, GATE SEED  ·  3 OCT RANDOM, OCT RANGE, OCT SEED, NOTE RANDOM  ·  4 NOTE SEED | 1 MOD LOOP, MOD TRIGGER, DROP, DROP SEED  ·  2 VEL RANDOM, VEL SEED, GATE RANDOM, GATE SEED  ·  3 OCT RANDOM, OCT RANGE, OCT SEED  ·  4 NOTE RANDOM, NOTE SEED |

- **MAIN**: CLOCK & TIMING | LATCH + OCTAVES | GATE / VELOCITY / SWING / VOICES (the groups the design draws dividers
  between). The MIDI event display moved to the left of the top row, CLOCK & TIMING to the right.
- **PATTERN**: MODE + PATTERN | the progression's MODE TRIGGER / MISSING NOTE / MODE SEED | RHYTHM | RANDOM PATTERN,
  a column each, with a gap between the progression's two groups (the columns had run across all three panels).
- **MODIFY**: the modifiers' two fours as before, then RANDOM OCTAVE | RANDOM NOTE (NOTE RANDOM had been in the
  octave column).

MAIN is made in the design (convert.py MAPS `augment` / `qlinks`); PATTERN and MODIFY in the port's page plan
(`layout.grid.conf`). A rerun keeps both.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on any page (qlink_overlay.py).

Files: [`schwung/sequencers/superarp/`](../schwung/sequencers/superarp/)

---

## Design QA: Rings FX

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| RINGS FX | 1 MODEL, POLYPHONY, STRUCTURE, BRIGHTNESS  ·  2 DAMPING, POSITION, NOTE, FINE  ·  3 INPUT, MIX, WIDTH, VOLUME | 1 MODEL, POLYPHONY  ·  2 STRUCTURE, BRIGHTNESS, DAMPING, POSITION  ·  3 NOTE, FINE  ·  4 INPUT, MIX, WIDTH, VOLUME |

- The columns ran across panels (column 1 took in MODEL SELECTION and half of RESONATOR; column 2 the rest of
  RESONATOR and PITCH / TUNING). Now MODEL SELECTION | RESONATOR | PITCH / TUNING | INPUT & OUTPUT, a column each.
- The made-up peak meter in INPUT & OUTPUT (it never moved) went; the four knobs spread across the panel.

Made in the design (convert.py MAPS `augment` / `qlinks`), so a rerun keeps it.

### Presets
Upstream has none, so the port brings 12 (`presets.json`, in MPC's PRESET menu): Init, Resonant Body, Metal Plate, Bright Bell, Low Drone, Sympathetic Strings, Sitar Drone, Chord Resonator, Plucked String, FM Ring, String Reverb, Shimmer Wash. Each sets every control;
levels evened out to within about 0.5 dB with a test signal through it.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).

Files: [`mutable-instruments/ringsfx/`](../mutable-instruments/ringsfx/)

---

## Design QA: Verglas

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel; groups of up to four).

### What changed
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

### Presets
Upstream has none, so the port brings 12 (`presets.json`, in MPC's PRESET menu): Init, Grain Cloud, Shimmer, Octave Down Haze, Ambient Wash, Stretch Time, Looper Delay, Spectral Smear, Lo-Fi Grains, Stutter, Dark Tail, Warm Tape. Each sets every control;
levels evened out to within about 3 dB with a test signal through it (the quieter ones turn on TONE's limiter for a
little make-up gain). None uses FREEZE: it would hold an empty buffer.

### For the owner
- The filter is one page further away now. If you'd rather keep it on the main page, the alternative is to take
  FREEZE (or QUALITY) off the Q-Links instead.

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap on either page (qlink_overlay.py).

Files: [`schwung/effects/verglas/`](../schwung/effects/verglas/)

---

## Design QA: Warps

Status: **passed on the device** (2026-10-04, ✅ in the README).

### Owner's notes
None specific; checked against the batch rules (one Q-Link column per panel, as Moog; outlines that frame one
panel).

### What changed
Q-Link columns before → after ("-" = an empty slot):

| Page | before | after |
|---|---|---|
| WARPS | 1 MODE, ALGORITHM, TIMBRE, SHIFT  ·  2 CARRIER, NOTE, FINE, CARRIER LVL  ·  3 MOD LEVEL, OUTPUT, MIX, VOLUME | 1 MODE, ALGORITHM, TIMBRE, SHIFT  ·  2 CARRIER, NOTE, FINE, CARRIER LVL  ·  3 MOD LEVEL  ·  4 OUTPUT, MIX, VOLUME |

- MODULATION and CARRIER were already a column each. INPUT's MOD LEVEL shared a column with OUTPUT, so that outline
  took in both panels: now INPUT | OUTPUT, a column each.
- OUTPUT's OUT / OUT + AUX selector moved from the panel's title bar to just under it, so OUTPUT's outline no longer
  reaches up into MODULATION. Its made-up meters (they never moved) went.

Made in the design (convert.py MAPS `augment` / `qlinks` / `nudge`), so a rerun keeps it.

### Presets
Upstream has none, so the port brings 12 (`presets.json`, in MPC's PRESET menu): Init, Ring Mod, Parallel Ring, Digital Ring, Cross Fold, XOR Crush, Comparator Grit, Robot Vocoder, Pulse Vocoder, Shift Up, Shift Down, Stereo Shift. Each sets every control;
levels evened out with a test signal through it (all within about 1 dB, Init a little louder at its defaults).

### Checked
Offline test PASSED; check_skin OK; no Q-Link outlines overlap (qlink_overlay.py).

Files: [`mutable-instruments/warps/`](../mutable-instruments/warps/)
