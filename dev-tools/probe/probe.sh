#!/usr/bin/env bash
# (steve/tools/probe) e.g.  steve/tools/probe/probe.sh steve/schwung-ports/braids src "presets:preset_count:preset:preset_name" note:60
# probe.sh <port dir> <module_dir, relative to the port or ""> cmd... : builds the port's engine + the Schwung adapter +
# probe.c in gcc:12 (objects cached in build/) and runs it there.
set -euo pipefail
MV=$(cd "$(dirname "$0")/../../.." && pwd); P=$(cd "$1" && pwd); MD="$2"; shift 2
eval "$(cd "$MV" && python3 tools/gen_vst.py "$P/vst.json" --shell)"
(cd "$MV" && python3 tools/gen_vst.py "$P/vst.json" --params-h >/dev/null 2>&1) || true   # params.h (plugin name etc.)
ADAPTER_O=""; [ -n "${ADAPTER:-}" ] && ADAPTER_O=build/probe_adapter.o
export SOURCES CFLAGS LIBS MV P MD ADAPTER ADAPTER_O
build='set -e; cd "$P"; mkdir -p build; OBJS=""
  for f in $SOURCES; do o="build/probe_${f//\//_}.o"
    if [ ! "$o" -nt "$f" ] || [ -n "$(find . -name "*.h" -newer "$o" 2>/dev/null | head -n 1)" ]; then case "$f" in *.cpp|*.cc|*.cxx) g++ -O1 -w $CFLAGS -Ibuild -c "$f" -o "$o";; *) gcc -O1 -w $CFLAGS -Ibuild -c "$f" -o "$o";; esac; fi
    OBJS="$OBJS $o"; done
  gcc -O1 -I$MV/wrapper -c /probe/probe.c -o build/probe_main.o
  [ -n "$ADAPTER" ] && gcc -O1 -c $MV/adapters/$ADAPTER/${ADAPTER}_engine.c -o build/probe_adapter.o
  g++ $OBJS build/probe_main.o $ADAPTER_O $LIBS -o build/probe
  ./build/probe "$MD" "$@"'
docker run --rm ${PROBE_DOCKER_ARGS:-} -v "$P":"$P" -v "$MV":"$MV":ro -v "$(cd "$(dirname "$0")" && pwd)":/probe:ro -e SOURCES -e CFLAGS -e LIBS -e MV -e P -e MD -e MIDIFX_DEBUG -e MIDIOUT_DEBUG -e ADAPTER -e ADAPTER_O \
  gcc:12 bash -c "$build" probe "$@"
