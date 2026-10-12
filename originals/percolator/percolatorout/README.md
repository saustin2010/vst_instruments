# Percolator OUT

**Audio effect** · Plays one of Percolator's OUT 1-4 on its own MPC track (an audio track will do), so a voice gets that track's fader, inserts and outputs. · maker in MPC: Steve A · written for this repo · licence: MIT ([`LICENSE`](LICENSE)) · ships in [Percolator](../)'s release

<img src="screenshots/page_0.png" width="760" alt="Percolator OUT">

MPC plays only a plugin's first stereo pair (sd88me's test on a Force), so Percolator can't hand its voices to MPC's
mixer as separate outputs. Instead, each Percolator voice goes to **MAIN** or to **OUT 1-4** (its OUT, on
Percolator's MIDI & OUT page), and this small effect, inserted on any other track, plays one OUT there. An audio
track will do: it doesn't take an instrument plugin's place the way a second Percolator would, and it costs next to
nothing (it copies a block).

## Using it

1. **Percolator** on its track, as usual (TRACK PLAYS = MAIN KIT). Each voice's **OUT** says where it goes: to start
   with, voice 1 to OUT 1, voice 2 to OUT 2 and so on; MAIN keeps a voice in Percolator's own mix. Several voices can
   share an OUT.
2. On another track (an **audio track** is fine), insert **[FX] Percolator OUT** in an insert slot (MPC lists it
   under VST, with the other effects).
3. Pick the **OUT**. The panel below it says what's coming in: **VOICE 2**, **VOICES 3 + 4**, **NO VOICE SENT TO
   OUT 2**, or **NO PERCOLATOR PLAYING**.

Those voices now play on this track, with its fader, pan, inserts and output (Out 3-4 sends them to the MPC's
outputs 3/4), and leave Percolator's own mix and compressor; their FX SEND still feeds Percolator's delay. Remove the
effect, or pick another OUT, and they go back to Percolator's mix within 16 blocks (46 ms). Until something plays an
OUT, the voices sent there stay in Percolator's mix, so nothing goes quiet by accident.

- **THRU** (on to start with): the track's own audio plays too, so a recording on the audio track still sounds.
  Off: the OUT alone.
- **Timing:** the OUT is a block (2.9 ms) late; while something plays an OUT, Percolator holds its own output back by
  the same block, so the voices stay together. (If MPC always ran this track before Percolator's, the OUT would settle
  two blocks late, one behind the rest; offline both orders and a mix of them work.)
- **Level:** a voice comes out as it would in Percolator's mix at VOLUME 71 (Percolator's VOLUME and compressor don't
  reach it); use the track's fader.
- One Percolator per project feeds the OUTs: with two on MAIN KIT, the first that played. A second Percolator set to
  TRACK PLAYS = OUT n also plays an OUT, through its own VOLUME and compressor (an instrument plugin's place, though).

## On the MPC

- In the insert effect slots: **[FX] Percolator OUT** by **Steve A**
- Files: `/sdcard/vst/percolatorout.so` and its screen in `/sdcard/Synths/Steve A - VST - [FX] Percolator OUT/`
- 3 parameters: OUT, THRU and STATUS (a readout); one page, OUT and THRU on Q-Link column 1
- Needs Percolator 2.0.0 or later in the same project

## How it works

Percolator and this effect are separate libraries, so they share no memory of their own. Both map one small region,
`/dev/shm/percolator-out-v1-<MPC's process id>` (64 KB, made by whichever starts first, removed by the next MPC that
starts after this one has gone), when they're created; Percolator writes each OUT into a ring there and publishes
how far it has written, and this reads the block before that. Nothing is looked up or waited for while playing, and
no file stays open. The layout is in `src/tapbus.h`, which is the same file in both plugins
(`originals/percolator/src/tapbus.h`): change them together. Its version is part of the file's name, so two
different layouts never read each other.

## Status (2026-10-12)

- Written for Percolator 2.0.0 (owner: "instead of a plugin track can we use an audio track? I can only have so many
  plugins running at once"). Build and offline host test PASSED; `check_skin.py` OK.
- Offline, Percolator and this effect as two separately loaded libraries (as MPC loads them), on the Mac (`/tmp`)
  and as 32-bit ARM Linux code (`/dev/shm`, as on the MPC): the effect plays exactly voice 2, a block late, every
  block, whichever runs first; Percolator plays the rest without it; THRU adds the input; with Percolator removed it
  goes quiet and says so.
- On the Live II (2026-10-12): installed (one MPC restart), and on an audio track with nothing recorded it plays its
  OUT, in time with the kit (owner: "ok all works").

## Files

| Path | What |
|---|---|
| `src/percolator_out.c` | the effect |
| `src/tapbus.h` | the OUT 1-4 bus (the same as Percolator's) |
| `mpc/gen.py` | writes `params.json`, `layout.conf` and the artwork's spec (drawn by Percolator's `../mpc/skin.py`) |
| `deploy/` | ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list entry |
| `images/skin/`, `percolatorout.css` | the artwork (generated) |
| `screenshots/` | the page as MPC draws it |

## Building

From the repo root: `tools/build.sh percolatorout` (see [BUILDING.md](../../../BUILDING.md)). It lives in Percolator's
folder because it ships in Percolator's release (two zips, as TR-MPC ships its Tap FX). To change the screen,
also from the repo root (the art container needs the whole repo for the fonts):

```
python3 originals/percolator/percolatorout/mpc/gen.py
docker run --rm -v "$PWD:$PWD" -w "$PWD/originals/percolator/percolatorout" mpc-vst-html-art python3 ../mpc/skin.py build/skin.json
```
