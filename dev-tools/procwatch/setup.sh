#!/usr/bin/env bash
# Build the harness (32-bit ARM for emulation and the device, native for timing) and fill work/sdcard/vst with every
# plugin's shipped .so (deploy/vst) and its library (presets/), laid out like the MPC's /sdcard/vst.
# Run it once, and again after tools/build.sh so emulated.sh sees the new .so:  dev-tools/procwatch/setup.sh
set -euo pipefail
. "$(dirname "$0")/common.sh"
. "$REPO/tools/docker-ready.sh"
docker_ready
mkdir -p "$W/bin"
F="-O2 -Wall -Wno-format-truncation"
docker run --rm --platform linux/arm/v7 -u "$UIDGID" -v "$HERE":/s:ro -v "$W/bin":/o arm32v7/gcc:12 sh -ec "
  gcc $F -o /o/procwatch-armhf /s/procwatch.c -ldl -lm
  gcc $F -o /o/abwatch-armhf /s/abwatch.c -ldl -lm"
docker run --rm -u "$UIDGID" -v "$HERE":/s:ro -v "$W/bin":/o gcc:12 sh -ec "
  gcc $F -o /o/procwatch-native /s/procwatch.c -ldl -lm
  gcc $F -o /o/abwatch-native /s/abwatch.c -ldl -lm"
rm -rf "${W:?}/sdcard"
mkdir -p "$W/sdcard/vst"
n=0
for j in $(plugin_vst_jsons); do
    d=$(dirname "$j"); s=$(so_name "$d")
    if [ -f "$REPO/$d/deploy/vst/$s.so" ]; then cp "$REPO/$d/deploy/vst/$s.so" "$W/sdcard/vst/"; n=$((n + 1))
    else echo "  $d: no deploy/vst/$s.so (tools/build.sh $(basename "$d"))"; fi
done
for p in "$REPO"/presets/*/; do [ -d "$p" ] && cp -R "${p%/}" "$W/sdcard/vst/"; done
echo "procwatch: binaries in $W/bin, $n plugins in $W/sdcard/vst"
[ -d "$MV/wrapper" ] || echo "  (native.sh needs the framework: framework/setup.sh)"
