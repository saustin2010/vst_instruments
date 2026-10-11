#!/usr/bin/env bash
# Idle cost, the reliable way: two instances of one native build timed side by side (abwatch.c), so a Mac core
# changing speed hits both alike. Two trials by default; then ab_report.py against the newest baseline.
#   dev-tools/procwatch/ab.sh [so name...]              (none: every native build in work/native/so; 2 at a time)
#   env: TRIALS=n (default 2), RELEASE=secs (wait before "after", default 6), OUT=<dir under work/>
# Build first with native.sh (after changing a plugin, rebuild it there too: tools/build.sh only makes the ARM .so).
set -euo pipefail
. "$(dirname "$0")/common.sh"
[ -x "$W/bin/abwatch-native" ] || { echo "run dev-tools/procwatch/setup.sh first" >&2; exit 1; }
OUT=${OUT:-ab-$(date +%Y%m%d-%H%M)}
mkdir -p "$W/$OUT"
if [ $# -gt 0 ]; then names=("$@"); else names=($(cd "$W/native/so" && ls *.so | sed 's/\.so$//')); fi
for n in "${names[@]}"; do [ -f "$W/native/so/$n.so" ] || { echo "no native build of $n: native.sh $n" >&2; exit 1; }; done
ab_one() {   # ab_one <so name> <out file>
    docker run --rm ${RELEASE:+-e RELEASE=$RELEASE} -v "$W/bin":/b:ro -v "$W/sdcard":/sdcard \
        -v "$W/native/so/$1.so":"/sdcard/vst/$1.so":ro gcc:12 timeout 300 /b/abwatch-native "/sdcard/vst/$1.so" 2>&1 \
        | grep '^AB' >> "$2" || echo "AB $1 FAILED" >> "$2"
}
export -f ab_one; export W
for t in $(seq 1 "${TRIALS:-2}"); do
    f="$W/$OUT/ab-trial$t.txt"; : > "$f"
    printf '%s\n' "${names[@]}" | xargs -P2 -I{} bash -c 'ab_one "$0" "$1"' {} "$f"
done
python3 "$HERE/ab_report.py" "$W/$OUT"/ab-trial*.txt
echo "results: $W/$OUT"
