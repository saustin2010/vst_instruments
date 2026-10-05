# Vendored DSP source

This port reuses [schwung-mrhyde](https://github.com/handcraftedcc/schwung-mrhyde)'s `plugin_api_v2`
build of Mutable Instruments' Plaits macro oscillator, the same way `mpc-vst-dx7` reuses
`schwung-dx7`: vendor the Schwung module's DSP wholesale, keep its `module.json` (the same
`chain_params` and parameter keys), and link it through `mpc-vst-plugins`' shared
`adapters/schwung` (no engine-bridge code needed here at all -- `move_plugin_init_v2` is Schwung's
`plugin_api_v2`, which is exactly the adapter's contract).

- **Vendored from**: https://github.com/handcraftedcc/schwung-mrhyde, commit
  `3c47f35a69fa82c8554ee62c402064d6bad0a3c7` (`main`).
  - `src/dsp/freak_plugin.cpp`, `src/dsp/plaits_move_engine.{h,cpp}` — the `plugin_api_v2` bridge
    (voice allocation, mod matrix, MIDI, chunk state). The starting point only: v1.1 rewrote most of it (below).
  - `src/module.json` — v1.1 replaced MrHyde's `chain_params` with this port's own parameter set (the five pages'
    controls, in the MPC skin's order, with `"default"`s that match `ppf_default_params`: the host may push them
    to every parameter) plus the two wrapper-only model buttons `model_prev` / `model_next` (`step_of: model`).
    MrHyde's Schwung-only `ui_hierarchy` is gone.
  - `src/dsp/third_party/eurorack/{plaits,stmlib}` — byte-identical to upstream
    https://github.com/pichenettes/eurorack, commit `08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4`
    (confirmed with `diff -rq` against a fresh checkout before vendoring), so treat license/updates
    as coming from upstream Mutable Instruments, not from schwung-mrhyde.
  - **Dropped** (present in schwung-mrhyde, not needed here — confirmed by `grep` that
    `plaits_move_engine.cpp` never references them): `plaits/user_data_receiver.{h,cc}`,
    `plaits/user_data.h`, `stm_audio_bootloader/fsk/` (Move's own audio-based firmware/data transfer
    feature, irrelevant off that hardware). Also dropped: `plaits/drivers/`, `plaits/bootloader/`,
    `plaits/ui.cc`, `plaits/settings.cc`, `plaits/plaits.cc`, `plaits/pot_controller.h`,
    `plaits/hardware_design/`, `plaits/hardware_design/pcb/`, `plaits/test/`, `plaits/resources/`
    (Python/`.syx`/`.bin` *generator inputs* for `resources.cc`, not needed once `resources.cc` is
    already generated) — all STM32-hardware or offline-tooling only. `stmlib/` is trimmed to
    `dsp/`, `utils/`, `algorithms/`, `stmlib.h` (drops `ui/`, `midi/`, `system/`, `programming/`,
    `third_party/`, `linker_scripts/`, `fft/`, `test/`, `tools/` — all STM32/AVR-hardware only).
  - `src/dsp/param_helper.h` also dropped: present in schwung-mrhyde, not `#include`d by either
    `.cpp` (confirmed by `grep`), dead weight.

- **Our one local change**: `plaits/dsp/dsp.h`'s `kSampleRate`/`kCorrectedSampleRate`, from Plaits'
  real hardware rates (48000.0f / 47872.34f, the STM32's I2S-derived rate) to 44100.0f (MPC OS's
  fixed plugin rate, `docs/NOTES.md` in `mpc-vst-plugins`). Both constants are used as plain
  runtime floats throughout `plaits/dsp/*` (`Hz / kSampleRate` for oscillator phase increments,
  filter cutoffs, and every engine's envelope/drum-synthesis time constants) — never baked into a
  sample-rate-specific lookup table — so fixing them here corrects pitch *and* every time constant
  together, consistently. Verified nothing else hardcodes the old rates: `grep -rn
  "kSampleRate\|kCorrectedSampleRate" plaits/dsp` shows only consumers, no other definition.
  - This makes `plaits_move_engine.cpp`'s own `kPitchCompensationSemitones` (`12 *
    log2f(kCorrectedSampleRate / 44100)`, schwung-mrhyde's pitch-only fix for the same problem)
    evaluate to exactly `12 * log2f(1.0) = 0` automatically — a no-op, left in place unmodified
    rather than deleted, since it now does nothing but also breaks nothing. schwung-mrhyde's own
    fix only corrected pitch, not the same time-constant drift (Move also runs at 44100 Hz —
    `plaits_move_engine.h`'s `PPF_SAMPLE_RATE`); this port's fix is a strict improvement at the
    upstream `dsp.h` level, not a criticism of schwung-mrhyde's own (different) bridge-level fix.

- **Build flag needed, not a source change**: `plaits/user_data.h`'s `#ifdef TEST` mock flash
  functions call bare `printf` with no `#include <cstdio>` (relies on some other translation unit
  having pulled it in first, which happened to hold in schwung-mrhyde's own build order but not
  ours) — `vst.json`'s `cflags` carries `-include cstdio` to cover it, matching schwung-mrhyde's own
  `-include stdio.h` in `scripts/build.sh` (same fix, different header spelling for C++). Also:
  `-DTEST` itself is required (not just for schwung-mrhyde's debug accessors) because
  `stmlib/dsp/dsp.h`'s `Sqrt`/`Clip16`/`ClipU16` are `#ifdef TEST` ? a portable `sqrtf()`/software
  clamp : raw ARMv7 VFP inline asm (`vsqrt.f32`, `ssat`, `usat`) otherwise — schwung-mrhyde's own
  build defines `-DTEST` unconditionally for exactly this reason (their target is aarch64, where
  that asm is invalid syntax too); MPC OS's device is armv7 (where the asm *would* assemble), but
  the portable path is still the correct choice here too — it's what was actually exercised by
  upstream Mutable Instruments' own desktop-test builds, and the offline x86 host test
  (`tools/test_port.sh`) cannot use the ARMv7 asm at all, so a build that only worked with `-DTEST`
  undefined for the device would be an untested code path in production. `-std=gnu++14` is also
  needed: the shared `vst2_wrap.c`/`build_port.sh` default to `gnu++11` for `.cpp`/`.cc` sources,
  which isn't quite enough for this vendored code (confirmed by compiling with the plain default
  first).

- **Crash fixes found by fuzzing** (`tests/run_fuzz.sh`: random Q-Link turns and notes under ASan, as 32-bit ARM
  under QEMU to match the device). Plaits reads its tables with unchecked indices; on the module a stray read hits
  harmless flash/RAM, on the MPC it can hit a null pointer. The first device bench segfaulted this way. All marked
  `mpc-vst-plaits:` in the source:
  - `plaits_move_engine.cpp`: the note sent to Plaits is clamped to 12..120 (C0..C9). MrHyde's pitch (+-48) plus
    its pitch mods went far below the module's range, where the slope oscillator blows up and the waveshaper then
    indexes its fold table with the result.
  - `plaits/dsp/engine/wavetable_engine.cc`: smoothed x/y/z landing exactly on 7.0 gave bank -1 (null wave pointer).
  - `plaits/dsp/engine2/wave_terrain_engine.cc`: path coordinates clamped to [-1, 1]; the last wave of a bank no
    longer reads the wave after it.
  - `plaits/dsp/speech/lpc_speech_synth.h` / `lpc_speech_synth_controller.cc`: no read past the last frame of a
    word; a frame index left over from a word bank is reset when switching back to phonemes; no negative index.
  - `stmlib/dsp/dsp.h` `InterpolateWrap`: tolerates negative and non-finite indices (upstream: "safe for >= 0").
  - `plaits/dsp/fm/dx_units.h` `Pow2Fast`: shifts the exponent as unsigned (same bits); upstream left-shifts a
    negative int, which UBSan flags now that the 6-op FM models play.

- **Zeroed engine memory** (`plaits_move_engine.cpp`, `ppf_engine_t` constructor): the voices' Plaits state is built in
  `calloc`ed memory. Plaits relies on its RAM starting zeroed (`.bss` on the module); several engines' `Init()`
  leave state unset (e.g. `FMEngine`'s downsampler taps). With reused heap memory inside MPC that state could
  start as NaN, stick in the LPG filter and silence FM 2-Op and most engines after it. Tests can reproduce it
  with `EXTRA=tests/dirty_heap.cc`.

- **v1.1 bridge rewrite** (`plaits_move_engine.{h,cpp}`, `freak_plugin.cpp`; `tests/modulation.c`):
  - All 24 models in the module's order (MrHyde hid 6-op FM x3, Chiptune and the drums behind a 17-entry table).
  - The module's attenuverters: TIMBRE / FM / MORPH scale Plaits' internal decay envelope, bipolar (MrHyde had
    FM only, 0..1, and TIMBRE / MORPH hardwired to 0).
  - AMP modes: GATE (the LPG follows the key), PING (LEVEL unpatched: struck, rings with DECAY), ENV (ENV 1 drives
    LEVEL, or a post-VCA on self-enveloped engines), DRONE (TRIG and LEVEL unpatched, ENV 1 as a post-VCA).
  - TRIG RATE: synced retriggers while a key is held (Chiptune's arpeggiator, ratchets).
  - Real host tempo (`lfo_bpm`, the wrapper's `HAS_LFO_BPM`; MrHyde assumed 120 BPM) and a phase restart when the
    MPC starts playing (`transport`, the wrapper's `HAS_TRANSPORT`, added to mpc-vst-plugins for this).
  - Pitch bend (0xE0, BEND range), mod wheel (CC 1), channel pressure (0xD0), reset controllers (CC 121).
  - Modulation: 2 LFOs (shared or PER VOICE), 2 ADSRs, MrHyde's cycle and random, feeding a fixed 4x4 grid
    (LFO 1, ENV 2, CYCLE, RANDOM x PITCH, HARMONICS, TIMBRE, MORPH) and an assignable 4x4 grid, every amount on
    Plaits' attenuverter curve. HARMONICS / TIMBRE / MORPH are passed unclamped so Plaits limits once, as the
    module does with knob + CV (MrHyde clamped before Plaits added its envelope).
  - Volume is a post-gain (MrHyde fed it into LEVEL, which also changed the LPG's brightness).
  - Output: MrHyde's `tanhf()` on the whole mix became a soft clip that is clean below -2.5 dBFS.
  - `freak_plugin.cpp` is one parameter table mirroring `module.json`; NOTES shows the notes that can really
    sound when UNISON caps it ("4 (2)").

- **Voice handling** (`plaits_move_engine.cpp`, marked `mpc-vst-plaits:`; `tests/voicing.c`):
  - TRIG is a gate: high while the key is down, dropped for one block when a held voice is restruck so Plaits
    sees a new edge. MrHyde sent a 3-block pulse, so the 6-op FM engines (the only ones that read TRIG's high
    state, as the DX7 envelope gate) released at once and stayed silent. Edge-only engines are unaffected.
  - Voice stealing takes a free voice, then the oldest released one, then the oldest held one, with a ~2 ms fade
    before the stolen voice restarts. MrHyde took the oldest of all, so playing over held keys stole them while
    released voices were still ringing out.
  - Mono/legato keep the held keys: releasing the sounding key goes back to the last one still held. LEGATO no
    longer restrikes Plaits on overlapping keys (it did, so it sounded exactly like MONO).
  - A released voice is freed once it has stayed below -80 dB for 100 ms (30 s cap), instead of after a time
    computed from LPG DECAY, which cut the tails of engines with their own decay (strings, modal, 6-op, drums)
    about 2.7 s after release at the default DECAY.
  - The voice RAM buffer is `alignas(8)`: engines keep pointer tables in it.

- **Sustain pedal** (`freak_plugin.cpp` `on_midi`): MrHyde ignored MIDI CCs. CC 64 now holds released keys until
  the pedal comes up; CC 120/123 (all sound / notes off) release everything. `tests/sustain.c`.

- **License**: MIT throughout (schwung-mrhyde's `LICENSE` notice is kept in this port's `LICENSE`, under third-party components;
  Mutable Instruments' `plaits`/`stmlib` carry their own MIT headers per-file, preserved as-is;
  `stmlib/LICENSE` also copied in place).

## Updating from upstream

1. The bridge (`freak_plugin.cpp`, `plaits_move_engine.{h,cpp}`) and `module.json` are this port's own since
   v1.1; take anything new from schwung-mrhyde by hand.
2. Re-vendor `src/dsp/third_party/eurorack/{plaits,stmlib}` from a fresh upstream
   `pichenettes/eurorack` checkout, re-applying the same trims listed above.
3. Reapply the `dsp.h` sample-rate edit and the fixes marked `mpc-vst-plaits:` (`grep -rn mpc-vst-plaits src`).
4. Update the eurorack commit hash above.
5. `tools/test_port.sh vst/vst.json` and the tests in README.md, then a device smoke test, before releasing.
