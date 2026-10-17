# Incipience

**Drum synth** · Eleven instruments laid out like the Jomox AlphaBase Mk II analog drum machine, with its controls and
0-127 ranges; plays the AlphaBase's own sample set. · maker in MPC: Steve A · written for this repo · licence: MIT
([`LICENSE`](LICENSE))

<img src="screenshots/page_0.png" width="760" alt="Incipience's KICK page">

**Status (2026-10-17): in testing.** Played on an MPC Live II since 2026-10-13. New on 2026-10-17, offline so far
(`tools/build.sh`: PASSED, `check_skin.py`: OK, `mpc/selftest.c`: PASSED): every instrument playable chromatically on
a MIDI channel of its own, OUT 1-11 with [FX] Incipience OUT, MIDI MOD lanes and a mod matrix, as on
[Percolator](../percolator/). What's next is in [ROADMAP.md](../../ROADMAP.md#new-plugins).

## What it is

The AlphaBase is an analog drum machine with eleven instruments. Incipience copies that structure and its controls,
and leaves out the sequencer: you play it from MPC's pads and sequencer.

| Instrument | On the AlphaBase | Here |
|---|---|---|
| KICK | Analog kick: VCO with a tune envelope, HARMONICS, a PULSE + NOISE attack shaped by GATE (0.1-15 ms), EQ, COMPRESS, a second pitch envelope, LFO | The same controls; a sine shaped towards pointed peaks, swept by the tune envelope |
| MBRANE | Two coupled filter oscillators (the Mod.Brane 11's), noise with its own envelope, two metal noises, pitch envelopes, two LFOs; the Mk II's 9 SCENES | Two resonant band-pass filters struck by the trigger pulse and the noise; DAMPEN up to self-oscillation; CPL 1>2 and CPL 2>1 (either sign) feed each other: same signs make them scream, opposite signs damp them |
| CL HAT, OP HAT | Sample + metal noise through the 2-pole hi-hat filter (LP, HP, peak), VCA ADSR, filter ADSR, LFO | The same chain; HH CHOKE (the Mk II): the two cut each other, and on the same step the open one wins |
| CLAP, RIM, CRASH, RIDE | Sample + metal noise through a 12 dB LP / HP / BP that self-oscillates from RESONANCE 85 | The same; with no sample it is an analog voice of its own (toms, claves) |
| X1, X2 | Digital sample players (and the AlphaBase's sampling input) | Sample players; no audio input in a plugin |
| FM | Four operators, a full 4x4 matrix with feedback, an attack-decay envelope per operator, up to six voices | The same; its own note plays CHORD |
| FX | Reverb and delay, a send per instrument | An 8-line reverb that rings on its own above ROOM 100; a ping-pong delay, free or synced to MPC's tempo |

It is not an emulation: the AlphaBase's circuits and firmware aren't published, so everything here is written from its
manual (what each control is said to do, its block diagrams and ranges), and it won't sound exactly like the hardware.
The metal noise patterns are this plugin's own; so are the MBrane's nine scenes and LFO waves 16-63 (with the
AlphaBase's `ABWV0300.raw` in the data folder, its own 64 LFO waves are used).

## The screen

Designed in Google Stitch to look like the AlphaBase Mk II's panel (cream, black knobs with green LEDs, wooden cheeks,
the OLED, the red band and the instrument keys). A tab per instrument, then FX and MIX. Each instrument has three
Q-Link sub-pages: the hardware's two pages (KICK A, KICK B), with the controls of its 4 x 4 encoder matrix in the same
places (a row of the matrix is a panel and a Q-Link column; the hardware's display shows one row at a time), and a
third (KICK MIX ...) whose Q-Links turn the panels on the right: the instrument's MIX (pan, reverb and delay sends,
mute) and its extras (the Mk II's synced LFO clock). On FM the third is FM VOICE: voices, chord, spatial and level
first, then its MIX and accent. FX has REVERB and DELAY (all the sends), MIX has PAN. Then MOD (its LFOs and 16
slots), MIDI & OUT (each instrument's NOTE, KEYS CH and OUT) and MIDI MOD (each instrument's four lanes): below.

SAMPLE is a box with arrows: one sample a tap (a Q-Link still turns through them). PITCH and CENTS on the sample
channels tune their metal noise as well as the sample (on the AlphaBase they move the sample alone), so a hat or a
cymbal without a sample can be tuned too; a tom from the self-oscillating filter is tuned with CUTOFF.

| | A | B | MIX / VOICE |
|---|---|---|---|
| KICK | <img src="screenshots/page_0.png" width="250"> | <img src="screenshots/page_0_1.png" width="250"> | <img src="screenshots/page_0_2.png" width="250"> |
| MBRANE | <img src="screenshots/page_1.png" width="250"> | <img src="screenshots/page_1_1.png" width="250"> | <img src="screenshots/page_1_2.png" width="250"> |
| CL HAT | <img src="screenshots/page_2.png" width="250"> | <img src="screenshots/page_2_1.png" width="250"> | <img src="screenshots/page_2_2.png" width="250"> |
| CLAP (RIM, CRASH, RIDE alike) | <img src="screenshots/page_4.png" width="250"> | <img src="screenshots/page_4_1.png" width="250"> | <img src="screenshots/page_4_2.png" width="250"> |
| X1 (X2 alike) | <img src="screenshots/page_8.png" width="250"> | <img src="screenshots/page_8_1.png" width="250"> | <img src="screenshots/page_8_2.png" width="250"> |
| FM | <img src="screenshots/page_10.png" width="250"> | <img src="screenshots/page_10_1.png" width="250"> | <img src="screenshots/page_10_2.png" width="250"> |
| FX | <img src="screenshots/page_11.png" width="250"> | <img src="screenshots/page_11_1.png" width="250"> | |
| MIX | <img src="screenshots/page_12.png" width="250"> | | |
| MOD (LFO, MOD 1-4 ... 13-16) | <img src="screenshots/page_13.png" width="250"> | <img src="screenshots/page_13_1.png" width="250"> | |
| MIDI & OUT (NOTE, KEYS CH, OUT) | <img src="screenshots/page_14.png" width="250"> | <img src="screenshots/page_14_1.png" width="250"> | <img src="screenshots/page_14_2.png" width="250"> |
| MIDI MOD (WHEEL, GP 1-3) | <img src="screenshots/page_15.png" width="250"> | <img src="screenshots/page_15_1.png" width="250"> | |

How it's made: the Stitch design is one page, KICK A (`design/stitch-kick.html`, as Stitch wrote it); `mpc/design.py`
writes every page in its markup (`design/stitch.html`), the repo's `convert.py` turns that into the layout, and
`mpc/merge_ab.py` folds each tab's two pages into Q-Link sub-pages. `mpc/screen.sh` runs it all, through the build,
`check_skin.py` and these screenshots.

## Playing it from MIDI tracks (as Percolator)

- **Pads:** each instrument plays on its NOTE as tuned (36-46 to start with: pads 1-11 of bank B on the plugin's own
  track); the FM synth's NOTE plays its CHORD. NOTE is on MIDI & OUT now (it was on MIX).
- **Chromatically (KEYS CH):** every note on an instrument's KEYS CH plays it pitched: C3 as tuned, an octave up at
  C4, and pitch bend on that channel bends it +-2 semitones. The kick and the MBrane transpose their oscillators, the
  sample channels their sample and their metal noise, X1 / X2 their sample; the FM synth plays a voice a note (up to
  its VOICES), with no chord. KEYS CH is channel 2 for KICK up to 12 for FM to start with; OFF; ALL (any note no NOTE
  takes). MPC hands the plugin's own track every note on channel 1, so the channels come through **Incipience MIDI
  In**, Incipience's own MIDI port: on a MIDI track, set MIDI OUTPUT PORT to Incipience MIDI In and the track's channel
  to the instrument's KEYS CH. (KEYS CH 1 makes the plugin's own track play that instrument from every pad.)
- **MIDI MOD lanes:** per instrument, the mod wheel and Gen Purpose 1-3 (CC 1 and 16-18; in a MIDI track's lane
  menu, MIDI 1 MOD WHEEL and MIDI 16-18 GEN PURPOSE 1-3) each set the control you pick for it, any of the instrument's
  own (all of them, its pan and sends too). On the instrument's KEYS CH, through Incipience MIDI In; KEYS CH ALL: on
  any channel. MPC's screen and Q-Links follow the control as the lane moves it.

## Outputs: OUT 1-11

MPC plays only a plugin's first stereo pair, so each instrument has an OUT: MAIN (Incipience's own mix) or OUT 1-11,
and **[FX] Incipience OUT** ([incipienceout](incipienceout/)), inserted on another track (an audio track will do),
plays one OUT there with that track's fader, inserts and output. To start with each instrument's OUT is its own (KICK
OUT 1 ... FM OUT 11), and an OUT nothing plays stays in the mix, so nothing changes until you insert the effect. An
instrument on a played OUT leaves Incipience's mix (its reverb and delay sends stay), panned and at the main VOLUME,
a block (2.9 ms) late, and Incipience holds its own output back by the same block.

## Modulation: the MOD tab

Four LFOs (WAVE: the AlphaBase's 64 shapes, 4 a sine; RATE; CLOCK free or synced to MPC's tempo; RESTART: free running,
or from the start at each hit of an instrument) and 16 slots. A slot moves one control: **SOURCE** (an LFO; RANDOM, a
new value at each hit of the slot's DEST; VELOCITY, of its DEST's last hit), **DEST** (an instrument, or FX: the reverb,
the delay and VOLUME), **CONTROL** (any of the DEST's controls, by name: the box with arrows) and **DEPTH** (+-100 % of
that control's range). Slots on one control add up, and a control never leaves its range. What the screen shows and the
project saves is the control as set; the slots move what the sound uses, every 32 samples. Controls a hit reads as it
starts (SAMPLE, START, STOP, LOOP, REVERSE, GATE, the FM envelopes) take the value of that moment; VOLUME, ACCENT, pan
and sends follow continuously. The LFOs and slots aren't part of a kit: a new kit keeps them, as it keeps the MIDI
settings.

What it costs: the instruments work out their settings every 32 samples anyway, so a slot adds a few multiplications;
offline, 60 s of all eleven instruments busy rendered in 0.48 s without slots and 0.46 s with all 16 on.

## Samples

The sample channels play the AlphaBase's samples by number, as its SAMPLE control does: sample `n` is `ABWV<nnnn>.raw`
(16-bit, 48 kHz, mono, no header) or `ABWV<nnnn>.wav` in the data folder. Jomox gives its sample set away
(`AlphaBaseWaves.zip` and `ABmk2Waves.zip` on jomox.de: the same 254 samples); they aren't shipped here. Without them
the built-in kits still play: they use the kick, the MBrane, metal noise through the filters, the self-oscillating
filters and the FM synth.

## Kits

In MPC's PRESET menu: **Init**, **Analog Basics**, **Low and Slow** (808-style kick, filter toms), **Hard Drive**
(909-style) and **Membranes**, which use no samples; with the AlphaBase's sample set in the data folder also **Sampled
House**, **Sampled Techno** and **Synth Filters** (its synth notes through the resonant filters), the kick and MBrane
still analog. The AlphaBase's own 63 factory kits (its `ABPR0000.BIN`) need its kit format decoded: not yet.

## Rendering offline

```
cc -O2 -Isrc mpc/render.c src/*.c -lm -lpthread -ldl -o build/render
build/render <data folder or ""> build/renders kit           # every kit: each instrument alone, then a groove
build/render <data folder or ""> build/renders script <file>  # one WAV per line: name;key=value;...;hits=kd@0,ch@0.5
```

`python3 mpc/gen.py` writes `params.json`, `src/params_def.h` and the page plan from one list (702 parameters; the
first 414 are as they were before 2026-10-17: MPC saves projects and Q-Links by number).

`mpc/selftest.c` checks KEYS CH and pitch bend, the lanes, the matrix, the saved state's size (at most 6.5 KB of the
wrapper's 8 KB) and OUT 1-11 with Incipience OUT loaded as a library of its own (the comment at its top says how to
build and run it; `bench` times the matrix).
