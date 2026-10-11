# procwatch: what the plugins do behind the scenes, and what they cost while silent

A stand-in for MPC's plugin host. It runs the shipped `.so` files under 32-bit ARM emulation, native builds on the Mac,
or the same checks on the MPC itself. How it works, the findings, the repairs and how to re-run everything are in
[TESTS.md](../../TESTS.md) at the repo root.

```
dev-tools/procwatch/setup.sh                     # build the harness, fill work/sdcard from deploy/ and presets/
dev-tools/procwatch/emulated.sh [so...]          # threads, files, heap, host calls, environment (shipped ARM .so)
dev-tools/procwatch/native.sh [so...]            # native builds, for timing
dev-tools/procwatch/ab.sh [so...]                # idle cost, two instances side by side, against baseline/
dev-tools/procwatch/device.sh <host> <so...>     # the same on the MPC, from /tmp, nothing installed
```

Names are `.so` names (`acid303`, `hushone`). Everything the scripts make goes in `work/`, which git ignores.
