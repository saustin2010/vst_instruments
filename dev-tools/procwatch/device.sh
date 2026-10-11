#!/usr/bin/env bash
# The same checks on the MPC itself: abwatch (idle cost in real microseconds and % of a block) and procwatch (threads,
# heap, host calls) on the repo's built .so (deploy/vst), copied with the harness to /tmp/procwatch on the device.
# Nothing is installed and MPC keeps running, as with the framework's tools/bench.sh: the harness runs pinned to core 1
# at normal priority (MPC's audio threads are real-time and always win), and /tmp/procwatch is removed afterwards.
# The plugins read their libraries from /sdcard/vst/<name>/ (compiled in), so the installed library is used.
#   dev-tools/procwatch/device.sh <host> <so name...>        e.g. device.sh <mpc-address> acid303 helm
#   env: MPC_SSH_KEY, MPC_SSH_USER, MPC_SSH_BIN as for install.sh; RELEASE=secs, CYCLES=n as for the other scripts;
#        ONLY=ab or ONLY=proc (default both); INSTALLED=1 to test the installed /sdcard/vst/<name>.so instead;
#        ALLOW_SEQ=1 to run a sequencer (it opens its own ALSA MIDI port, which MPC can see while it runs).
# Results: work/device-<date>/<name>.txt, ab-device.txt, and ab_report.py's table.
set -euo pipefail
. "$(dirname "$0")/common.sh"
[ $# -ge 2 ] || { sed -n '2,12p' "$0" >&2; exit 2; }
host=$1; shift
[ -x "$W/bin/abwatch-armhf" ] || { echo "run dev-tools/procwatch/setup.sh first" >&2; exit 1; }
. "$REPO/tools/common.sh"   # ssh_setup / ssh_close: the installer's SSH settings
ssh_setup "$host"
R=/tmp/procwatch
cleanup() { "${SSH[@]}" "rm -rf $R" || true; ssh_close; }
trap cleanup EXIT
OUTD=$W/${OUT:-device-$(date +%Y%m%d-%H%M)}
mkdir -p "$OUTD"
put() { "${SSH[@]}" "cat > $R/$2 && chmod +x $R/$2" < "$1"; }
"${SSH[@]}" "rm -rf $R && mkdir -p $R"
put "$W/bin/abwatch-armhf" abwatch
put "$W/bin/procwatch-armhf" procwatch
envs="${RELEASE:+RELEASE=$RELEASE} ${CYCLES:+CYCLES=$CYCLES}"
for n in "$@"; do
    d=$(plugin_dir "$n")
    if is_sequencer "$n" && [ -z "${ALLOW_SEQ:-}" ]; then
        echo "$n: skipped (a sequencer opens an ALSA MIDI port MPC can see; ALLOW_SEQ=1 to run it anyway)"; continue
    fi
    if [ -n "${INSTALLED:-}" ]; then so=/sdcard/vst/$n.so
    else
        [ -f "$REPO/$d/deploy/vst/$n.so" ] || { echo "$n: no $d/deploy/vst/$n.so (tools/build.sh)"; continue; }
        put "$REPO/$d/deploy/vst/$n.so" "$n.so"; so=$R/$n.so
    fi
    echo "$n: running on $host (~30 s)"
    run=""
    [ "${ONLY:-}" != proc ] && run="\$T env $envs ./abwatch $so;"
    [ "${ONLY:-}" != ab ] && run="$run \$T env $envs ./procwatch $so;"
    "${SSH[@]}" "cd $R && T= && { command -v taskset >/dev/null 2>&1 && T='taskset 2' || true; } && $run rm -f $R/$n.so" \
        > "$OUTD/$n.txt" 2>&1 || echo "$n: failed, see $OUTD/$n.txt"
    grep '^AB' "$OUTD/$n.txt" >> "$OUTD/ab-device.txt" || true
done
[ -s "$OUTD/ab-device.txt" ] && python3 "$HERE/ab_report.py" "$OUTD/ab-device.txt" --min-us 20
[ "${ONLY:-}" != ab ] && python3 "$HERE/summarise.py" "$OUTD"
echo "results: $OUTD"
