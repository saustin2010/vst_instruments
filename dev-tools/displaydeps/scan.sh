#!/usr/bin/env bash
# Which parameters other labels follow, so they need "refresh_display": true (scan.c says why):
#   dev-tools/displaydeps/scan.sh <plugin folder>          e.g. dev-tools/displaydeps/scan.sh originals/percolator
# Builds scan.c with the wrapper, the engine and its adapter (as tools/test_port.sh builds the host test, in the gcc:12
# container) and runs it with the plugin's data folder where the MPC has it. MPC_VST_FRAMEWORK picks the framework.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
MV=${MPC_VST_FRAMEWORK:-$REPO/framework/mpc-vst-plugins}
CFG="$(cd "$1" && pwd)/vst.json"
eval "$(python3 "$MV/tools/gen_vst.py" "$CFG" --shell)"
python3 "$MV/tools/gen_vst.py" "$CFG" --params-h
ADAPTER_SRC=""
[ -n "$ADAPTER" ] && ADAPTER_SRC="$MV/adapters/$ADAPTER/${ADAPTER}_engine.c"
SCAN="$REPO/dev-tools/displaydeps/scan.c"
OUT="$ROOT/$PORT/build/displaydeps"
build='
  set -e
  OBJS=""
  for f in $SOURCES; do
    o="$PORT/build/dd_${f//\//_}.o"
    case "$f" in
      *.cpp|*.cc|*.cxx) g++ -O1 -g -std=gnu++11 $CFLAGS -I"$PORT/build" -I"$MV/wrapper" -c "$f" -o "$o" ;;
      *) gcc -O1 -g -std=gnu11 $CFLAGS -I"$PORT/build" -I"$MV/wrapper" -c "$f" -o "$o" ;;
    esac
    OBJS="$OBJS $o"
  done
  for f in "$SCAN" "$MV/wrapper/vst2_wrap.c" $ADAPTER_SRC; do
    o="$PORT/build/dd_$(basename "$f").o"
    gcc -O1 -g -std=gnu11 -I"$PORT/build" -c "$f" -o "$o"
    OBJS="$OBJS $o"
  done
  g++ $OBJS $LIBS -lpthread -lm -o "$OUT"
  "$OUT"
'
MD=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('defines',{}).get('MODULE_DIR','').strip('\"'))" "$CFG")
DATA=$REPO/presets/$(basename "$1")
MOUNT=""
[ -n "$MD" ] && [ -d "$DATA" ] && MOUNT="-v $DATA:$MD:ro"
MVMOUNT="-v $MV:$MV:ro"; case "$MV" in "$ROOT"/*) MVMOUNT="" ;; esac
REPOMOUNT="-v $REPO:$REPO:ro"; [ "$REPO" = "$ROOT" ] && REPOMOUNT=""
cd "$ROOT"
# shellcheck disable=SC2086
docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT":"$ROOT" $MVMOUNT $REPOMOUNT -w "$ROOT" $MOUNT -e SCAN="$SCAN" \
  -e SOURCES="$SOURCES" -e CFLAGS="$CFLAGS" -e PORT="$PORT" -e LIBS="$LIBS" -e MV="$MV" -e ADAPTER_SRC="$ADAPTER_SRC" -e OUT="$OUT" \
  gcc:12 bash -c "$build"
