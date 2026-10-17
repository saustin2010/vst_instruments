# Tests: background work and idle CPU

This file covers what each plugin does behind the scenes (threads, memory, open files, calls into MPC) and what it costs
while it's silent. The plugins are checked in a simulator on the Mac first, then on the MPC. The first run (2026-10-09)
found the problems in section 1. This file is the plan for repairing them and how to repeat the tests. The tools are in
[`dev-tools/procwatch/`](dev-tools/procwatch/); the results they're compared against are in
[`dev-tools/procwatch/baseline/`](dev-tools/procwatch/baseline/).

1. [Status](#1-status)
2. [How it was tested](#2-how-it-was-tested)
3. [What was found](#3-what-was-found-2026-10-09)
4. [Repairs](#4-repairs)
5. [Testing on the MPC](#5-testing-on-the-mpc)
6. [Running the tests again](#6-running-the-tests-again)

## 1. Status

The issues are ranked by impact for the cost of repair. Update the Status column as each one is fixed, checked on the
device and (for plugins in sd88me's catalogue) released, with the date.

| # | Issue | Plugins | Impact | Cost | Status |
|---|---|---|---|---|---|
| 1 | Never goes back to sleep after its first note | 303 | Medium | Very low | open |
| 2 | Sets `HOME` and `XDG_DATA_HOME` for the whole MPC process | Helm | Unlikely, but serious if it happens | Low | open |
| 3 | Keeps working while silent: an opt-in "sleep when silent" switch in the wrapper | 9 always on, 5 partly on ([4.3](#43-sleep-when-silent-in-the-wrapper)) | High in total | Medium, mostly release work | open |
| 4 | Runs all 6 voices every sample, silent or not | Mono Voice | High, for one plugin | Medium–high | open |
| 5 | Runs at full cost with nothing to do | Rampage | Medium | Medium | open |
| 6 | Leaks about 1 KB per instance; this blocks its catalogue entry | Noisemaker | Low (but blocks a release) | Low–medium | open |
| 7 | Leaks 180–220 KB per insert and remove | Helm | Low | Medium; cause unknown | open |
| 8 | Always on | Marbles | Low | Medium | open |
| 9 | Its loader thread may run on MPC's isolated audio cores | Tablor | Unknown, probably low | Low, but provable only on the device | open |
| 10 | Reads the transport position 2–3 times per block | MPC Plaits (any plugin with both `HAS_LFO_BPM` and `HAS_TRANSPORT`) | Negligible | Low; do it with #3 | open |

Fixes 1, 3, 4, 5 and 8 free CPU only on tracks that are silent at that moment. When every track plays at once they
change nothing. In a song where parts drop in and out, that's much of the time.

## 2. How it was tested

No emulator runs MPC OS, and `dev-tools/fake-mpc/` only tests the installer. So the tests use a stand-in for MPC's
plugin host, built from the framework's `tools/bench.c`. It loads a plugin's `.so` the way MPC does:

- `VSTPluginMain`, then open, set the sample rate to 44100 Hz, set the block size to 128 frames, resume, start processing.
- `processReplacing` once per 128-frame block, paced to real time: one block every 2.9 ms.
- MIDI through `effProcessEvents`.
- `audioMasterGetTime` answered with a 120 BPM 4/4 transport, stopped or playing.

The test input:
- **Instruments:** 8-note chords (MIDI notes 36 + 7k mod 36, velocity 100), struck again every 0.5 s.
- **Effects:** white noise at ±0.25.

### 2.1 procwatch: what a plugin does behind the scenes

`procwatch.c` runs one instance through these stages:

| Stage | Length | What's happening |
|---|---|---|
| load | | `dlopen`, open one instance |
| warm | 0.5 s | silence |
| idle | 2 s | transport stopped, no notes |
| idle_playing | 2 s | transport running, no notes |
| notes | 2 s | chords, or noise into an effect |
| release | 4 s (`RELEASE=`) | notes off, then silence |
| after | 2 s | silence again, to compare with idle |
| second instance | | open, 0.5 s of audio, close. `CYCLES=n` repeats it n more times with a heap reading after each, so a leak shows as steady growth. |
| close | | close the first instance, then check what's left |

For each stage it records:
- **The plugin's threads** (from `/proc/self/task`): name, new or gone, CPU, and wakeups per second (voluntary
  context switches). Threads that existed before `dlopen`, such as the emulator's own, are ignored.
- **Open files** (`/proc/self/fd`) and **malloc'd heap** (`mallinfo2`).
- **Host callbacks** by opcode, and whether any came from a thread other than the audio thread.
- **Audio-thread CPU** per block (median and p99) and the **output peak**.
- **The process environment** (added 2026-10-10). A plugin's `setenv` changes it for all of MPC.

It ran on the **shipped 32-bit ARM `.so` files** (`deploy/vst`), under QEMU in the `arm32v7/gcc:12` container. A copy
of the MPC's `/sdcard/vst` (all 40 `.so` files plus `presets/`) is mounted at `/sdcard`, so the plugins load their
libraries as they do on the device.

Two extra runs:
- `FTZ=1` sets flush-to-zero on the audio thread, to rule out denormal slowdowns.
- `MIDIFX_DEBUG=1` makes each sequencer print its MIDI out, since ALSA isn't available in the container.

`summarise.py` turns the runs into one table and a list of flags.

### 2.2 abwatch: idle cost that can be trusted

CPU timings under emulation mean nothing. Even native timings on the Mac swing about 2× between runs, because the Linux
VM lands on fast or slow Apple cores. So the idle cost comes from a different setup:
- Each plugin is built natively for 64-bit ARM, from the same sources and flags as the framework's `build_port.sh`.
- `abwatch.c` processes **two instances in turn each block**, so both are timed on the same core at the same moment.

It has three phases:
- **play** (3 s): instance A plays and B never has. `idle/play` = B's median ÷ A's median. This is what a
  never-played instance costs, as a share of its playing cost.
- **release** (6 s, `RELEASE=`): untimed.
- **after** (3 s): both silent. `after/fresh` = A's median ÷ B's median. This shows whether an instance that has played
  goes back to sleep.

There were two trials, and they agreed within about 0.05 on `idle/play` and about 10% on `after/fresh`. Absolute
microseconds on the Mac only rank the plugins; they say nothing about the MPC's CPU (see the framework's
`docs/BENCH.md`).

### 2.3 What the simulator can't see

- **Percentages on the MPC.** There's no conversion from Mac figures. `device.sh` ([section 5](#5-testing-on-the-mpc))
  measures on the MPC itself.
- **MPC's own threads**, such as the screen thread.
- **Real ALSA MIDI, real-time scheduling, and core isolation** (MPC runs with `isolcpus=2-3`).
- **Host calls MPC's JUCE host makes that the stand-in doesn't.**

## 3. What was found (2026-10-09)

### Clean

- **Threads:** only two plugins start one, Libpo32 (its render worker) and Tablor (`tablor-wtload`). Each starts one
  per instance, uses 0% CPU with no wakeups when idle, and stops it on close. Nothing is left running after close, and
  nothing polls. The wrapper and Helm's bundled JUCE start no threads.
- **Calls into MPC:** no `audioMasterAutomate` or `audioMasterUpdateDisplay` while idle, so nothing costs MPC's screen
  thread in the background. All host calls come from the audio thread.
- **Files:** none are left open, and none are opened while playing.
- **Sequencers:** quiet with the transport stopped and no input, with two by-design exceptions:
  - Pixel Walkers keeps playing after Stop until its walkers leave the screen (silent within 2 minutes; `tick()` in
    `src/dsp.c`).
  - MIDI Player sends a few messages while stopped when it's fed notes, then stops.
- **Denormals:** `FTZ=1` changed nothing.
- **Environment** (2026-10-10, all 40): only Helm changes it.

### Problems

Idle cost, from `ab_report.py` on the two trials. "idle µs" is the Mac-native median, for ranking only.

| Plugin | idle/play | after/fresh | idle µs | Flag | Repair |
|---|---|---|---|---|---|
| Mono Voice | 0.94 | 1.02 | 48–190 | ALWAYS ON | [4.4](#44-mono-voice-skip-silent-voices) |
| Mutable Vibe | 0.41 | 0.96 | 49 | PARTLY ON | [4.3](#43-sleep-when-silent-in-the-wrapper) |
| Verglas | 1.02 | 0.97 | 41 | ALWAYS ON | 4.3 |
| Wurl | 0.42 | 0.99 | 38 | PARTLY ON | 4.3 |
| Helm | 0.29 | 1.00 | 38 | PARTLY ON | 4.3 |
| Rampage | 0.92 | 0.99 | 34 | ALWAYS ON | [4.5](#45-rampage-rest-when-both-channels-are-idle) |
| Elements | 0.97 | 1.02 | 33 | ALWAYS ON | 4.3 |
| Rings | 1.00 | 0.99 | 29 | ALWAYS ON | 4.3 |
| Aphex | 0.92 | 1.00 | 26 | ALWAYS ON | 4.3 |
| Hera | 0.28 | 1.00 | 24–45 | PARTLY ON | 4.3 |
| Chordism | 0.27 | 0.99 | 19 | PARTLY ON | 4.3 |
| Warps | 1.02 | 0.98 | 18 | ALWAYS ON | 4.3 |
| Moog | 1.02 | 0.98 | 18 | ALWAYS ON | 4.3 |
| Rings FX | 0.99 | 1.02 | 15 | ALWAYS ON | 4.3 |
| Hush One | 1.01 | 0.97 | 12 | ALWAYS ON | 4.3 |
| Marbles | 0.98 | 0.98 | 10 | ALWAYS ON | [4.8](#48-marbles) |
| Plaits | 1.00 | 1.02 | 5 | ALWAYS ON | 4.3 |
| MonkSynth | 0.59 | 0.99 | 3–5 | ALWAYS ON (tiny) | 4.3, optional |
| **303** | 0.08 | **10.9** | 2 fresh, 24 after playing | NO SLEEP | [4.1](#41-303-go-back-to-sleep) |

Fizzik (0.15), OB-Xd (0.09) and Noisemaker (0.22) keep a little work going while silent, but not enough to flag. These
plugins already go to sleep when silent: Tablor, NuSaw, Hank, MPC Plaits, Mr Hyde, Braids and Denis. MPC Plaits does it
per voice (`kSilenceLevel` in `mpc-ports/mpcplaits/src/dsp/plaits_move_engine.cpp`).

Other findings:
- **Helm sets `HOME` and `XDG_DATA_HOME`** to `/data/UserData/schwung/helm-config`, a Move path.
- **Heap left per insert and remove** (`CYCLES=6`):
  - Helm: 180–220 KB, growing steadily. That's a leak.
  - Tablor: 15–21 KB the first time, then about 300 B each time.
  - Noisemaker: 6–8 KB the first time, then about 850 B each time.
- **Tablor's loader thread** is pinned to cores 0–2, a Move layout ("leave core 3 to SPI"). MPC isolates cores 2 and 3
  for its audio workers.
- **MPC Plaits calls `audioMasterGetTime` twice per block.**

Every number is in `dev-tools/procwatch/baseline/2026-10-09/`. The 2026-10-10 emulated run, which added the
environment check, is in `baseline/2026-10-10/`.

## 4. Repairs

The scripts use **`.so` names** (`acid303`, `hushone`); `tools/build.sh` uses **folder names** (`303`, `hush1`). After
any change to a plugin, run these steps:

1. `tools/build.sh <folder>` must print PASSED (the ARM `.so` plus the offline x86 ASan/UBSan test).
2. `dev-tools/procwatch/setup.sh` copies the new `.so` into the simulator's `/sdcard`.
3. `dev-tools/procwatch/emulated.sh <so>` should show no new flags.
4. `dev-tools/procwatch/native.sh <so> && dev-tools/procwatch/ab.sh <so>`: the plugin's flag should now read
   `(fixed)`, and the play peak should match the baseline (`peak play` in the AB line).
5. Update the plugin's README. If the change is under `src/`, keep it under `MPC_PORT` and add it to
   `upstream-changes.diff`. Update ROADMAP.md and the Status column above.
6. Test on the MPC ([section 5](#5-testing-on-the-mpc)).
7. If the plugin is in sd88me's catalogue, release it as described in `docs/catalogue-migration.md`:
   `dev-tools/catalogue/check.sh <folder>` must say PASSED and "screen: same", then `tools/publish.sh <folder>`, a new
   release, and an update to its catalogue entry.

### 4.1 303: go back to sleep

**Problem:** Open303's own sleep test is commented out, and every sample sets `idle = false`. A 303 that has played once
keeps its full playing cost until it's removed.

**Where:** `schwung/instruments/303/src/dsp/open303/rosic_Open303.h`, about line 404. The 303's build already defines
`MPC_PORT`. `AnalogEnvelope::endIsReached()` (`rosic_AnalogEnvelope.cpp:175`) returns true once the note is off and
the envelope output is below 1e-6, so the test works. Every note event sets `idle = false` again
(`rosic_Open303.cpp`: 166, 202, 245, 264).

**Change:**

```cpp
    // find out whether we may switch ourselves off for the next call:
#ifdef MPC_PORT   // sleep once the note has ended and the output is silent (upstream's test, re-enabled)
    idle = (sequencer.getSequencerMode() == AcidSequencer::OFF && ampEnv.endIsReached()
            && fabs(tmp) < 0.000001);
#else
    idle = false;
    //idle = (sequencer.getSequencerMode() == AcidSequencer::OFF && ampEnv.endIsReached()
    //        && fabs(tmp) < 0.000001); // ampEnvOut < 0.000001;
#endif
```

**Check offline:** `ab.sh acid303` shows `after/fresh` ≤ 1.5 (was 10.9) and `NO SLEEP (fixed)`. `idle/play` stays
about 0.08 and `peak play` stays 0.616.

**Check on the MPC:**
1. `device.sh <host> acid303` shows the same ratios.
2. Install, then play a pattern with rests, slides and accents.
3. Listen to the first note after a rest: it should have no click, and slides should start from the previous pitch as
   before.

**Then:** release v1.0.1. It's in the catalogue.

### 4.2 Helm: leave HOME alone

**Problem:** `v2_create_instance` calls `setenv("HOME", "/data/UserData/schwung/helm-config")` and the same for
`XDG_DATA_HOME`. These are Ableton Move paths. On MPC they change the environment for the whole MPC process every
time Helm is inserted. Any part of MPC that resolves paths through `HOME` would then point at a folder that doesn't
exist. `setenv` isn't thread-safe either: another MPC thread reading the environment at that moment can crash MPC.
Nobody has seen either happen; it's a risk.

**Where:** `schwung/instruments/helm/src/dsp/helm_plugin.cpp:794-798`. Helm's build doesn't define `MPC_PORT` yet, and
nothing in Helm's source uses it, so adding it changes nothing else. Helm finds its patches through
`LoadSave::setCustomBankDirectory(patches_path)` (line 823), not through `HOME`. Only its config file
(`LoadSave::getConfigFile`) uses `HOME`, and when that file is missing Helm uses its defaults.

**Change:** add `"-DMPC_PORT"` to the `cflags` in `schwung/instruments/helm/vst.json`, then:

```cpp
#ifndef MPC_PORT   /* Move's config folder. On MPC this would set HOME for all of MPC (and setenv isn't thread-safe) */
  char helm_home_path[512];
  snprintf(helm_home_path, sizeof(helm_home_path),
           "/data/UserData/schwung/helm-config");
  setenv("HOME", helm_home_path, 1);
  setenv("XDG_DATA_HOME", helm_home_path, 1);
#endif
```

**Check offline:**
1. `emulated.sh helm` no longer prints "changed the host process's environment".
2. In the output, the notes stage's peak is still about 0.74.
3. `tools/build.sh helm` still passes, and its offline test loads the presets.

**Check on the MPC:**
1. `device.sh <host> helm` shows no environment change.
2. Install, insert Helm, step through presets in the PRESET menu, and play.

**Then:** no release is needed. Helm in the catalogue is Lewinator56's, so this is local.

### 4.3 Sleep when silent, in the wrapper

**Problem:** 14 plugins keep working while silent. Most are Mutable Instruments modules, which run continuously like
the hardware. Each needs the same thing: when there's nothing to do, skip the engine. The wrapper
(`framework/mpc-vst-plugins/wrapper/vst2_wrap.c`) already sits between MPC and every engine, so one opt-in switch
there covers them all.

**Design:**
- **When it sleeps:** no note held, the input (effects only) is exactly zero, and the output has been exactly zero for
  half a second. The engines render 16-bit samples, so "exactly zero" is a precise test. An engine with a faint noise
  floor simply never sleeps: it saves nothing, but nothing breaks.
- **While asleep:** the wrapper outputs zeros and skips the engine.
- **When it wakes:** on any MIDI, any parameter change, or any input sample, before the engine's next block. So nothing
  is lost or delayed.
- **Opt-in per plugin**, as a `defines` entry in `vst.json`. A plugin that starts sounding on its own would stay
  asleep. That rules out anything with an internal sequencer, clock or arpeggiator: Mono Voice, Rampage, Marbles and
  every sequencer.

**Sketch** (not yet compiled):

```c
/* vst.json "defines": {"SLEEP_WHEN_SILENT": 1}: skip the engine while nothing is held, the input is silent and its
 * output has been exactly zero for half a second. Any MIDI, parameter change or input wakes it for its next block. */
#ifndef SLEEP_WHEN_SILENT
#define SLEEP_WHEN_SILENT 0
#endif
enum { SLEEP_AFTER_BLOCKS = 44100 / 2 / DSP_BLOCK };
static int all_zero(const int16_t *b, int n) { for (int i = 0; i < n; i++) if (b[i]) return 0; return 1; }

/* wrap_t gains:  int held, quiet, asleep; */

/* render_frames(), at the block boundary, in place of the input + render calls: */
        if (w->pos >= DSP_BLOCK) {
            engine_lock(w);
            int in_silent = !PLUG_EFFECT || all_zero(w->inblock, DSP_BLOCK * 2);
            if (SLEEP_WHEN_SILENT && w->asleep && in_silent) {
                memset(w->block, 0, sizeof w->block);
            } else {
                w->asleep = 0;
                if (PLUG_EFFECT && mpc_engine_input) mpc_engine_input(w->dsp, w->inblock, DSP_BLOCK);
                g_api->render(w->dsp, w->block, DSP_BLOCK);
                if (SLEEP_WHEN_SILENT) {
                    w->quiet = w->held == 0 && in_silent && all_zero(w->block, DSP_BLOCK * 2) ? w->quiet + 1 : 0;
                    w->asleep = w->quiet >= SLEEP_AFTER_BLOCKS;
                }
            }
            engine_unlock(w);
            w->pos = 0;
        }

/* effProcessEvents, for every MIDI message (before the Q-Link/NRPN check, so those wake it too): */
                w->asleep = w->quiet = 0;
                int st = d[0] & 0xF0;
                if (st == 0x90 && d[2]) w->held++;
                else if ((st == 0x80 || st == 0x90) && w->held > 0) w->held--;
                else if (st == 0xB0 && (d[1] == 120 || d[1] == 123)) w->held = 0;

/* set_parameter_locked(): */
    w->asleep = w->quiet = 0;
```

If `held` drifts out of step (a note-off that never arrives), the plugin just doesn't sleep. That's safe. With the
sustain pedal down, the output isn't zero, so it doesn't sleep either.

**Steps:**
1. Make the change in the framework clone (`framework/mpc-vst-plugins`). Refresh `framework/mpc-vst-plugins.patch`
   from its `git diff`, as CLAUDE.md describes, and fold in [#10](#410-transport-read-once-per-block).
2. Test the switch on its own:
   1. Turn it on for Plaits and run `native.sh plaits && ab.sh plaits`. Expect `idle/play` ≤ 0.15, `after/fresh` about
      1, and `peak play` unchanged (0.327).
   2. Run `RELEASE=30 ab.sh elements rings`. Their tails ring for longer than 6 s (the `after` peak was 0.005 for
      Elements and 0.0002 for Rings), so check they sleep once the tail has gone.
3. Roll it out in three groups:
   - **a. Not released by you:** Helm, Mutable Vibe, Hush One, Plaits. No release is needed, so prove it here first.
   - **b. The heaviest released plugins:** Verglas, Wurl, Elements, Rings, Aphex, Hera.
   - **c. The rest:** Chordism, Warps, Moog, Rings FX, and optionally MonkSynth, Fizzik, OB-Xd and Noisemaker.
4. For each plugin:
   1. Read its engine first. Can it ever start sounding with no MIDI, no input and no parameter change (a drone, a
      free-running LFO that opens an envelope, an internal clock)? If so, leave it out.
   2. Add the define and run the common steps above. For an effect, also run `procwatch` and confirm the notes-stage
      peak with noise in is unchanged.
5. Each released plugin needs the framework change released too:
   - put the commit on `steve-features` of `saustin2010/mpc-vst-plugins` (and offer it to sd88me as a PR);
   - pin it in `framework/setup-release.sh` and the plugins' `release.yml`;
   - release v1.0.1 of each plugin.

**Check on the MPC:**
1. `device.sh <host> <so...>` shows `idle %` close to 0 and `play %` unchanged.
2. In a project, put several instances on tracks that stay silent for a while.
3. Run `dev-tools/screengrab/cpuwatch.sh 60 5 <host>` before and after the update, and compare MPC's audio threads.
4. Listen for a missing or late first note after a long silence. There shouldn't be one.

### 4.4 Mono Voice: skip silent voices

**Problem:** `mono_render()` (`schwung/instruments/monovoice/src/mono_core.c:2530`) calls `render_track()` for all 6
tracks on every sample, and only checks mute and solo afterwards. Each call also runs sequence events, retriggers, LFOs
and parameter smoothing, and copies three parameter arrays.

**Why not the wrapper switch:** Mono Voice has its own sequencer, song mode and arpeggiators, which play with no MIDI
coming in.

**Approach:** inside `render_track()` (line 1906 onwards, about 600 lines):
1. Keep the timing parts running every sample: `process_sequence_events`, retriggers, `arp_tick`, LFO phases.
2. Skip the oscillators, filter and amp once the track's amp envelope has finished and its output has been below about
   1e-6 for a short while; return 0 instead.
3. Wake the track when it's triggered (`track_trigger`).

Each track feeds the next one's modulation (`neighbor_l` and `neighbor_r`). A silent track outputs 0 anyway, so
skipping it changes nothing for its neighbour. Check that no track reads its neighbour's oscillator rather than its
output. Keep the change under `MPC_PORT` (the build already defines it).

**Check offline:**
1. Run `ab.sh monovoice`: `idle/play` should be well under 0.5.
2. Render a pattern and song with the internal sequencer before and after the change, using a small host script or the
   offline test, and compare the outputs. Free-running oscillator phase may differ after a silence, so compare levels
   and timing rather than samples.

**Check on the MPC:** `device.sh`, then play a song using all 6 tracks, mutes, solos and cross-modulation.

### 4.5 Rampage: rest when both channels are idle

**Problem:** `render` (`vcv-rack/rampage/mpc/rampage_engine.cc:164`) runs the Rack module's `process()` on every sample,
even when nothing is triggered or cycling and its outputs are at rest. It costs as much silent as playing.

**Approach:**
1. Skip `r.process()` for the block while all of these hold:
   - no note is held (`in->nheld == 0`);
   - nothing is pending (`fire` and `kick` are false on both channels);
   - neither channel is cycling;
   - both outputs ended the last block where they started;
   - the DC blocker has settled.
2. While it rests, output zeros and send no CCs (nothing changed).
3. Any MIDI or parameter change ends the rest.

It's a sequencer, so the wrapper switch doesn't apply.

**Check offline:** `ab.sh rampage`, plus `MIDIFX_DEBUG=1 emulated.sh rampage` to compare its CC and end-of-cycle output
before and after the change.

**Check on the MPC:** run `device.sh` with `ALLOW_SEQ=1`, then use Rampage driving a synth.

### 4.6 Noisemaker: leak (already on the catalogue list)

GitHub's LeakSanitizer found about 1 KB per instance (`FilterHandler`'s filters, the preset spline points). See
`docs/catalogue-migration.md`. After fixing it, `CYCLES=6 emulated.sh noisemaker` should show the heap flat after the
first cycle. Its release run must pass before it gets its own catalogue entry.

### 4.7 Helm: leak

**Problem:** each insert and remove leaves 180–220 KB, growing steadily (`CYCLES=6 emulated.sh helm`). The cause is
not found yet. `v2_destroy_instance` is at `helm_plugin.cpp` about line 866.

**Approach** (not tried yet): build Helm natively with LeakSanitizer and let it name the allocations.
1. Temporarily add `-fsanitize=address -fno-omit-frame-pointer` to the compile and link lines in `native.sh`, and build
   `native.sh helm`.
2. Run `procwatch-native` with `CYCLES=6`, `ASAN_OPTIONS=detect_leaks=1`,
   `LD_PRELOAD=$(gcc -print-file-name=libasan.so)`, and `docker run --cap-add SYS_PTRACE` (LeakSanitizer needs ptrace).

Likely suspects are JUCE singletons and caches that are never deleted, and patch objects held by `inst->patches`.

**Impact:** about 20 MB per 100 inserts, on a device with 2 GB. That's low.

### 4.8 Marbles

Same approach as [4.5](#45-rampage-rest-when-both-channels-are-idle). Marbles costs about a third of Rampage's idle, so
do it last.

### 4.9 Tablor: keep the loader off MPC's audio cores

**Problem:** `rtStartWorker` (`schwung/instruments/tablor/src/dsp/rt.h:51-57`) pins `tablor-wtload` to cores 0–2. MPC
isolates cores 2 and 3 for its `AudioWorker` threads (framework `docs/NOTES.md`). The loader only runs while a wavetable
loads, so the worst case is a glitch during a load.

**Change** (Tablor's build already defines `MPC_PORT`):

```c
#ifdef MPC_PORT   /* MPC isolates cores 2-3 for its audio workers (isolcpus=2-3) */
    CPU_SET(0, &set);
    CPU_SET(1, &set);
#else
    /* Leave core 3 to the SPI callback. */
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
#endif
```

**Check on the MPC** (the simulator can't show this):
1. Load wavetables repeatedly while a project plays.
2. Read the loader thread's CPU (field 39 of `/proc/$(pidof MPC)/task/<tid>/stat`), or watch `cpuwatch.sh`.
3. Listen for glitches before and after the change.

If the loader never lands on cores 2–3 even before the change, close this as not needed.

### 4.10 Transport: read once per block

**Problem:** `run_block()` in `vst2_wrap.c` calls `audioMasterGetTime` separately in `update_tempo()` (`HAS_LFO_BPM`),
`update_transport()` (`HAS_TRANSPORT`) and for `mpc_engine_transport`.

**Change:** fetch it once at the top of `run_block()` and pass the pointer to all three. This is wrapper code, so do it
in the same framework change as [4.3](#43-sleep-when-silent-in-the-wrapper).

**Check offline:** `emulated.sh mpcplaits` should report "getTime/blk" as 1.00 (it was 2).

## 5. Testing on the MPC

The MPC is the live setup. Pick a time when nothing is being recorded or backed up.

### 5.1 The same tests on the device, without installing

```
dev-tools/procwatch/setup.sh                                   # builds the harness for 32-bit ARM too
dev-tools/procwatch/device.sh <mpc-address> acid303 helm   # any .so names
```

What `device.sh` does:
- Copies the 32-bit ARM `abwatch` and `procwatch`, and the repo's built `.so` (`deploy/vst`), to `/tmp/procwatch` on
  the MPC.
- Runs them pinned to core 1 at normal priority, the way the framework's `tools/bench.sh` does. MPC's real-time audio
  threads always win, and nothing is installed or restarted.
- Deletes `/tmp/procwatch` afterwards, and writes the results to `dev-tools/procwatch/work/device-<date>/`.

Each plugin takes about 30 s. The plugins read their libraries from the installed `/sdcard/vst/<name>/`.

Options:
- `INSTALLED=1` tests the installed `.so` instead of the repo's build.
- `ONLY=ab` or `ONLY=proc` runs just one of the two tools.
- `MPC_SSH_KEY` and `MPC_SSH_USER` work as they do for `install.sh`.
- Sequencers are skipped unless `ALLOW_SEQ=1`, because each opens its own ALSA MIDI port, which MPC can see while it
  runs.

On the device, the AB lines also give **% of block**: each median as a share of the 2902 µs budget. These are real MPC
figures. Record them in [5.3](#53-device-results).

`device.sh` has so far run only against a stand-in 32-bit ARM container (2026-10-10). On the first real run, check
that `taskset` exists on the MPC (without it the harness runs unpinned) and that `/tmp/procwatch` is gone afterwards.

### 5.2 In real use, after installing

1. Run `./install.sh <mpc-address> <folder> --dry-run`. It must say no restart is
   needed (all plugins are already registered).
2. Install for real, then remove and re-insert the plugin in MPC. Updates load on re-insert.
3. Do the listening checks for each repair in section 4.
4. For CPU, run `dev-tools/screengrab/cpuwatch.sh 60 5 <mpc-address>` with a test project, before and after the
   update. It's read-only; `MPC_KEY` sets the SSH key. Compare MPC's total CPU and its `Audio_Processing` and
   `AudioWorker*` threads.

### 5.3 Device results

| Date | Plugin | Build | idle/play | after/fresh | play % | idle % | after % | Notes |
|---|---|---|---|---|---|---|---|---|
| | | | | | | | | |

## 6. Running the tests again

**When to run them:**
- after changing a plugin's engine or the framework patch;
- before every release;
- after adding a plugin;
- after updating the framework.

**Full run** (about 25 minutes on the Mac):

```
framework/setup.sh                  # once: native.sh builds against the framework
dev-tools/procwatch/setup.sh        # build the harness, copy every deploy/vst .so and presets/ into work/sdcard
dev-tools/procwatch/emulated.sh     # procwatch on every shipped .so under emulation (44), 4 at a time
dev-tools/procwatch/native.sh       # native builds of all of them
dev-tools/procwatch/ab.sh           # side-by-side idle timing, 2 trials, compared with the baseline
```

**One plugin:** `emulated.sh <so>`, then `native.sh <so> && ab.sh <so>`. The companion plugins in a plugin's folder (Percolator OUT,
Incipience OUT) are included since 2026-10-17 (`common.sh`; before that the harness looked one folder deep and missed
them): `emulated.sh percolatorout incipienceout`.

**Environment settings:**

| Variable | For | Effect |
|---|---|---|
| `RELEASE=secs` | emulated, ab, device | Wait longer before "after" (long tails: Elements, Rings, reverbs) |
| `CYCLES=n` | emulated, device | n extra inserts and removes with a heap reading after each (leaks) |
| `FTZ=1` | emulated | Flush denormals to zero on the audio thread |
| `MIDIFX_DEBUG=1` | emulated | Print a sequencer's MIDI out |
| `TRIALS=n` | ab | Number of side-by-side trials (default 2) |
| `OUT=name` | all | Results folder under `work/` |

**Reading the results:**
- **`emulated.sh`** prints summarise.py's table and flags. These flags can be trusted: threads left running, busy
  background threads, open files, heap per insert, host calls while idle or from other threads, more than one
  `GetTime` per block, environment changes. Treat its CPU-based flag ("silent after release but costs Nx idle") as a
  hint only: emulated timing is noisy, and Libpo32 once showed 5.5× when the Mac was busy. Confirm it with `ab.sh`.
- **`ab.sh`** prints `ab_report.py`'s table against the newest baseline:
  - `ALWAYS ON`: `idle/play` ≥ 0.5.
  - `PARTLY ON`: ≥ 0.2.
  - `NO SLEEP`: `after/fresh` ≥ 2.
  - Each flag is marked `new` (a regression: look into it before releasing), `still` (known, in section 3) or `fixed`.

  Ratios move by about 0.05 between runs. Plugins under about 5 µs are never flagged, because their ratios are noise.
- **Until the repairs are done**, a full run should show:
  - **`ab.sh`:** only the 19 flags in section 3, all marked `still`.
  - **`emulated.sh`:** Helm's heap and environment flags, MPC Plaits' 2 `GetTime` calls per block, Noisemaker's and
    Tablor's heap per insert, and 303's "costs Nx idle". A few CPU hints that change from run to run are also normal,
    for example Rings FX, Mutable Vibe and Rampage at about 1.5–2.5×. See `baseline/2026-10-10/emulated.txt`.

**Saving a new baseline:** once a run's results are accepted (for example, after a repair), copy them into
`dev-tools/procwatch/baseline/<date>/` and commit them with the change:
- the `ab-trial*.txt` files;
- `summarise.py work/<emulated dir> > emulated.txt`.

`ab_report.py` compares with the newest baseline folder that has `ab-trial` files.

### Files

| File | What it is |
|---|---|
| `dev-tools/procwatch/procwatch.c` | The behind-the-scenes check (threads, files, heap, host calls, environment, CPU per stage) |
| `dev-tools/procwatch/abwatch.c` | Side-by-side idle timing |
| `setup.sh`, `emulated.sh`, `native.sh`, `ab.sh`, `device.sh`, `common.sh` | Building and running them |
| `summarise.py`, `ab_report.py` | Tables and flags |
| `baseline/<date>/` | Accepted results to compare against |
| `work/` | Binaries, the simulated `/sdcard`, native builds and results (ignored by git) |
