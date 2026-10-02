# fuzz: stress-test a port before it reaches the MPC (2026-10-02)

The offline test (`tools/test_port.sh`) checks that a port works. This checks that it **survives**: it does what a
user does on the MPC, fast and at random, under AddressSanitizer and UndefinedBehaviorSanitizer, so a crash shows up
on the Mac with a file and line instead of taking MPC down.

| File | What |
|---|---|
| `fuzz.c` | the stress host: links with the wrapper, the port's sources and its adapter, like the offline test |
| `fuzz_port.sh <port> [seed] [blocks] [threads] [focus]` | builds it in the `gcc:12` container and runs it on one port |
| `fuzz_all.sh [blocks] <port>...` | runs a list, on one thread and on two, one line per run; logs in `<port>/logs/fuzz-*.log` |

What one run does, on three instances at once:
- parameter values at the ends, the middle and anywhere; slow Q-Link-sized ticks that read back in between;
  triggers, steppers and pop-ups fired as a touch does; every name and value display read;
- MIDI: notes and chords, pitch bend, mod wheel, sustain, random CCs, all-notes-off;
- the state saved on one instance and restored on another (project save and load);
- now and then an instance closed and opened again (the plugin removed from a track and inserted).

`threads` 1 runs audio (MIDI + render) on one thread and everything else on another, as MPC does: the screen and
project load on its message thread, audio on the audio thread. `focus` (a parameter key) sends half of the control
steps to that one parameter: the way to chase a reported crash ("it crashes when I change VOICES").

Exit 0 and `SURVIVED` if nothing failed; output blocks with NaN/Inf and each instance's peak are reported too.

## Chasing a crash
```
steve/tools/fuzz/fuzz_port.sh noisemaker 1 2000 0 voices          # reproduce
FUZZ_TRACE=1 steve/tools/fuzz/fuzz_port.sh plaits 1 1500 0 2> t.log  # log every action; the lines before the error say what led to it
FUZZ_CFLAGS=-DMY_CHECK steve/tools/fuzz/fuzz_port.sh plaits ...    # build with an extra define for a temporary check in the engine
```
Objects are reused between runs and rebuilt when a source, header, `vst.json`, the wrapper or `FUZZ_CFLAGS` changes.
A port whose engine reads data from its MODULE_DIR gets its `deploy/vst/<dir>` mounted there.

## What it found (2026-10-02)
- **Noisemaker**: changing VOICES while notes sound, then playing more notes than voices, read an empty list at
  index -1 (uncaught `std::out_of_range`: the crash reported on the device). Patched in its voice manager.
- **All engines**: the screen thread and the audio thread called into the engine at the same time. The wrapper now
  takes a per-instance lock around every engine call (`docs/NOTES.md`).
- **Plaits**: the 6-op FM engine's scratch buffer was a third of what its voice writes, so every render overwrote FM
  patches 0-1 (garbage operators, out-of-range table reads).
- **Rings, Elements, Warps, Marbles, Verglas, Rings FX**: a control at exactly 100% read one past a lookup table
  (`stmlib::Interpolate`, clamped now).
- **Fizzik**: a float rounding edge read one past its delay line.
- **Mr Drums**: a project load (or a pad sample change) freed a sample a voice was still playing; the next block read
  freed memory (ASan heap-use-after-free). Voices reading it are silenced first now.
- **Hank**: the same float rounding edge as Fizzik, one past its sine table. **Braids**: pitch above the module's
  0..16383 read past the flute's filter table (clamped as the firmware does). **303**: Open303 cleared four entries
  past a table at start-up.

All 36 ports then ran clean, on one thread and on two (1500 blocks x 3 instances each).

Each fix is under `#ifdef MPC_PORT` with the original in the port's `bak-upstream/`.

Remaining sanitizer notes that are not bugs on the MPC's compiler: Verglas's looping player and Plaits' DX7 units
left-shift negative numbers (GCC defines this as two's complement). Noisemaker, Hera and Braids rely on signed
wrap-around and are built with `-fwrapv`.
