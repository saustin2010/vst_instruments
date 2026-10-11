#!/usr/bin/env bash
# Build plugins natively (64-bit ARM on an Apple Silicon Mac: no emulation, so the timings mean something) from the
# same sources and flags as the framework's tools/build_port.sh, into work/native/so/<name>.so. Build logs go to
# work/native/build-<name>.log. Needs the framework (framework/setup.sh).
#   dev-tools/procwatch/native.sh [so name...]          (none: all of them; 4 at a time)
set -euo pipefail
. "$(dirname "$0")/common.sh"
[ -d "$MV/wrapper" ] || { echo "no framework checkout: run framework/setup.sh" >&2; exit 1; }
mkdir -p "$W/native/so"
build_one() {   # build_one <so name>
    local n=$1 D
    D=$REPO/$(plugin_dir "$n") || return 1
    (
    set -euo pipefail
    eval "$(python3 "$MV/tools/gen_vst.py" "$D/vst.json" --shell)"
    python3 "$MV/tools/gen_vst.py" "$D/vst.json" --params-h
    ADAPTER_SRC=""; [ -n "${ADAPTER:-}" ] && ADAPTER_SRC="$MV/adapters/$ADAPTER/${ADAPTER}_engine.c"
    OBJ="$W/native/obj/$n"; rm -rf "$OBJ"; mkdir -p "$OBJ"
    export SOURCES CFLAGS PORT LIBS ADAPTER_SRC OBJ SO MV W
    docker run --rm -u "$UIDGID" -v "$REPO":"$REPO" -w "$ROOT" \
      -e SOURCES -e CFLAGS -e PORT -e LIBS -e ADAPTER_SRC -e OBJ -e SO -e MV -e W gcc:12 bash -euc '
      OBJS=""
      # as build_port.sh: a C port compiles everything with its CFLAGS; a C++ port compiles the wrapper and adapter without
      case "$SOURCES" in *.cpp*|*.cc*|*.cxx*) CX=1 ;; *) CX=0 ;; esac
      for f in $SOURCES "$MV/wrapper/vst2_wrap.c" $ADAPTER_SRC; do
        o="$OBJ/$(echo "$f" | tr / _).o"; F="$CFLAGS"
        [ "$CX" = 1 ] && { [ "$f" = "$MV/wrapper/vst2_wrap.c" ] || [ "$f" = "$ADAPTER_SRC" ]; } && F=""
        case "$f" in
          *.cpp|*.cc|*.cxx) g++ -O2 -w -fPIC -fvisibility=hidden -std=gnu++11 $F -I"$PORT/build" -c "$f" -o "$o" ;;
          *) gcc -O2 -w -fPIC -fvisibility=hidden -std=gnu11 $F -I"$PORT/build" -c "$f" -o "$o" ;;
        esac
        OBJS="$OBJS $o"
      done
      g++ -shared $OBJS $LIBS -o "$W/native/so/$SO"'
    ) > "$W/native/build-$n.log" 2>&1 && echo "built $n" || { echo "FAILED $n: $W/native/build-$n.log"; return 1; }
}
export -f build_one plugin_dir plugin_vst_jsons so_name; export REPO W MV UIDGID
if [ $# -gt 0 ]; then names=("$@"); else names=($(all_so_names)); fi
printf '%s\n' "${names[@]}" | xargs -P4 -n1 bash -c 'build_one "$0"'
