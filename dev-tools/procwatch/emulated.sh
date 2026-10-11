#!/usr/bin/env bash
# procwatch on the shipped 32-bit ARM .so files under emulation (the binaries that go on the MPC): threads, open
# files, heap, host calls, MIDI out. CPU figures here are emulated: compare them only within one run.
#   dev-tools/procwatch/emulated.sh [so name...]        (none: all of them; 4 at a time)
#   env: OUT=<dir under work/> (default emulated-<date>), RELEASE=secs, CYCLES=n, FTZ=1, MIDIFX_DEBUG=1 (print a
#        sequencer's MIDI out)
# Writes work/<OUT>/<name>.txt and prints summarise.py's table and flags.
set -euo pipefail
. "$(dirname "$0")/common.sh"
[ -x "$W/bin/procwatch-armhf" ] || { echo "run dev-tools/procwatch/setup.sh first" >&2; exit 1; }
export OUT=${OUT:-emulated-$(date +%Y%m%d-%H%M)}
mkdir -p "$W/$OUT"
one() {
    docker run --rm --platform linux/arm/v7 ${FTZ:+-e FTZ=1} ${CYCLES:+-e CYCLES=$CYCLES} ${RELEASE:+-e RELEASE=$RELEASE} \
        ${MIDIFX_DEBUG:+-e MIDIFX_DEBUG=1} -v "$W/bin":/b:ro -v "$W/sdcard":/sdcard arm32v7/gcc:12 \
        timeout 900 /b/procwatch-armhf "/sdcard/vst/$1.so" > "$W/$OUT/$1.txt" 2>&1
    echo "$1 exit $?" >> "$W/$OUT/_status"
}
export -f one; export W
if [ $# -gt 0 ]; then names=("$@"); else names=($(all_so_names)); fi
for n in "${names[@]}"; do [ -f "$W/sdcard/vst/$n.so" ] || { echo "no $W/sdcard/vst/$n.so (setup.sh)" >&2; exit 1; }; done
printf '%s\n' "${names[@]}" | xargs -P4 -n1 bash -c 'one "$0"'
python3 "$HERE/summarise.py" "$W/$OUT"
echo "results: $W/$OUT"
