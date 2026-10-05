# Vendored / first-party DSP sources

## First-party (this project's own code) — `src/` and `src/dsp/*.c`, `src/dsp/*.cpp`
`engine.cpp` (the mpc_engine adapter) and the DSP glue cores — `rings_c.cpp`, `plaits_c.cpp`,
`filter_c.c`, `reverb_c.c`, `delay_tape_c.c`, `sat_c.c`, `chorus_c.c` — are written for this project
(they first appeared in the author's Mac VST3 build of the same instrument) and are covered by this
repo's MIT LICENSE. They are not third-party and are maintained here directly.

## Third-party — `src/dsp/eurorack/` (Mutable Instruments, MIT)
The Rings, Plaits and stmlib DSP are vendored from the Mutable Instruments **eurorack** sources:

- Upstream: https://github.com/pichenettes/eurorack  (Copyright 2012-2016 Emilie Gillet, MIT — see
  `src/dsp/eurorack/stmlib/LICENSE` and this repo's `LICENSE`).

They are committed here as plain files (not a submodule and not fetched at build time) so the port
builds fully self-contained offline and in CI. Only the subset the build needs is kept:
`eurorack/rings/dsp/`, `eurorack/plaits/dsp/`, each engine's `resources.cc`/`.h`,
`plaits/user_data.h`, and `eurorack/stmlib/` (its `dsp/` + `utils/`). The firmware-only parts of the
upstream tree (`bootloader/`, `drivers/`, `test/`, `hardware_design/`, the `resources/` Python
generators, the `*.cc` device mains, makefiles, linker scripts and ROM blobs) and
`stmlib/third_party/` (the STM32 HAL) are **not** vendored — nothing in the compiled path includes
them, and `-DTEST=1` compiles out the hardware references.

### Local changes vs upstream
- `plaits/user_data.h`: added `#include <cstdio>` inside the `#ifdef TEST` block. The mock
  `FLASH_ErasePage`/`FLASH_ProgramWord` there call `printf` without including it; host clang/gcc pull
  `<cstdio>` in transitively, but `arm32v7/gcc:12` does not, so the device build failed with
  "'printf' was not declared". Upstream shortcoming; fix applied to the vendored copy.
- The DSP cores carry the author's per-voice velocity handling (the exciter strength is set per voice
  on each strum) used by `rings_c.cpp` / `plaits_c.cpp`; this lives in the glue, not in the eurorack
  engines, so the eurorack content is otherwise unmodified.

### Re-vendoring from a newer upstream
Clone `pichenettes/eurorack`, copy the subset listed above into `src/dsp/eurorack/`, re-apply the
`user_data.h` one-liner, then `tools/test_port.sh vst/vst.json` must print PASSED.
