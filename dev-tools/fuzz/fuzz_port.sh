#!/usr/bin/env bash
# Stress-test a port offline (steve/tools/fuzz, 2026-10-02):
#   steve/tools/fuzz/fuzz_port.sh <port> [seed] [blocks] [threads 0|1] [focus param key]
# Builds fuzz.c with the wrapper, the port's sources and its adapter under ASan/UBSan (like tools/test_port.sh) in
# the gcc:12 container, with the port's data folder mounted at its device path, and runs it. threads=1 runs audio and
# control on two threads, as MPC does. A focus key makes half the control steps hit that parameter.
set -euo pipefail
PORT_NAME=$1; SEED=${2:-1}; BLOCKS=${3:-3000}; THREADS=${4:-0}; FOCUS=${5:-}
STEVE=$(cd "$(dirname "$0")/../.." && pwd); MV=$(dirname "$STEVE"); D="$STEVE/schwung-ports/$PORT_NAME"
CFG="$D/vst.json"
eval "$(python3 "$MV/tools/gen_vst.py" "$CFG" --shell)"
CFLAGS="$CFLAGS ${FUZZ_CFLAGS:-}"   # extra defines for a debugging run, e.g. FUZZ_CFLAGS=-DMPC_FUZZ_CHECK
python3 "$MV/tools/gen_vst.py" "$CFG" --params-h >/dev/null
ADAPTER_SRC=""; [ -n "$ADAPTER" ] && ADAPTER_SRC="$MV/adapters/$ADAPTER/${ADAPTER}_engine.c"
OUT="$ROOT/$PORT/build/fuzz"
SAN=${FUZZ_SAN:-"-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"}   # FUZZ_SAN: other flags (steve/tools/scope: -O2)
MD=$(python3 -c "import json;print(json.load(open('$CFG')).get('defines',{}).get('MODULE_DIR','').strip('\"'))")
MOUNT=""; [ -n "$MD" ] && [ -d "$D/deploy/vst/$(basename "$MD")" ] && MOUNT="-v $D/deploy/vst/$(basename "$MD"):$MD:ro"
build='
  set -e
  OBJS=""
  for f in $SOURCES; do
    o="$PORT/build/fz_${f//\//_}.o"
    if [ ! "$o" -nt "$f" ]; then case "$f" in
      *.cpp|*.cc|*.cxx) g++ $SAN -std=gnu++11 $CFLAGS -I"$PORT/build" -c "$f" -o "$o" ;;
      *) gcc $SAN -std=gnu11 $CFLAGS -I"$PORT/build" -c "$f" -o "$o" ;;
    esac; fi
    OBJS="$OBJS $o"
  done
  for f in "$FUZZ_C" "$MV/wrapper/vst2_wrap.c" $ADAPTER_SRC; do
    o="$PORT/build/fz_$(basename "$f").o"
    gcc $SAN -std=gnu11 -I"$PORT/build" -I"$MV/wrapper" -c "$f" -o "$o"
    OBJS="$OBJS $o"
  done
  g++ $SAN $OBJS $LIBS -lpthread -o "$OUT"
'
FUZZ_C=${FUZZ_MAIN:-"$STEVE/tools/fuzz/fuzz.c"}   # FUZZ_MAIN: another host program built the same way
# Objects are reused while newer than their source; a changed header, vst.json or FUZZ_CFLAGS rebuilds them all.
STAMP="$D/build/fz_stamp"; mkdir -p "$D/build"
if [ ! -f "$STAMP" ] || [ "$(cat "$STAMP")" != "${FUZZ_CFLAGS:-}${FUZZ_SAN:-}" ] || \
   [ -n "$(find "$D/src" "$D/mpc" "$CFG" "$MV/wrapper" -type f -newer "$STAMP" 2>/dev/null | head -n 1)" ]; then
  rm -f "$D"/build/fz_*.o
  printf "%s" "${FUZZ_CFLAGS:-}${FUZZ_SAN:-}" > "$STAMP"
fi
export SOURCES CFLAGS PORT LIBS SAN MV ADAPTER_SRC OUT FUZZ_C
cd "$ROOT"
# shellcheck disable=SC2086
docker run --rm -v "$ROOT":"$ROOT" -v "$MV":"$MV" -w "$ROOT" $MOUNT -e ASAN_OPTIONS=detect_leaks=0 -e FUZZ_TRACE \
  -e SOURCES -e CFLAGS -e PORT -e LIBS -e SAN -e MV -e ADAPTER_SRC -e OUT -e FUZZ_C gcc:12 \
  bash -c "$build"'timeout 900 "$OUT" '"$SEED $BLOCKS $THREADS $FOCUS"
