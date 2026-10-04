#!/usr/bin/env bash
# How loud each preset in a plugin's PRESET menu plays (levels.c), to even them out:
#   dev-tools/presets/levels.sh <plugin dir> [<low note> <high note> [<program> ...]]     e.g. mpc-ports/mpcplaits 36 60
# LEVELS_MAX=<n>: at most n presets (every k-th), for a quick survey. A plugin with no presets is measured as inserted.
# Needs the objects the offline test leaves in <plugin>/build (tools/build.sh <plugin> first). Runs in gcc:12 like the
# test, with the plugin's data folder mounted where the plugin looks for it.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
D=$(cd "$1" && pwd); shift
MD=$(python3 -c "import json;print(json.load(open('$D/vst.json')).get('defines',{}).get('MODULE_DIR','').strip('\"'))")
LIBS=$(python3 -c "import json;print(' '.join(json.load(open('$D/vst.json')).get('build',{}).get('libs',[])))")
ARGS=()
DATA=$(cd "$D" && git rev-parse --show-toplevel)/presets/$(basename "$D")
[ -n "$MD" ] && [ -d "$DATA" ] && ARGS=(-v "$DATA":"$MD":ro)
ls "$D"/build/host_*.o >/dev/null 2>&1 || { echo "$D: no build/host_*.o (run tools/build.sh first)"; exit 1; }
docker run --rm -u "$(id -u):$(id -g)" -v "$D":"$D" -v "$HERE":"$HERE":ro "${ARGS[@]}" -w "$D/build" \
  -e ASAN_OPTIONS=detect_leaks=0 -e LEVELS_MAX gcc:12 bash -c '
  set -e
  gcc -O2 -c "'"$HERE"'/levels.c" -o levels.o
  g++ -fsanitize=address,undefined $(ls host_*.o | grep -v "^host_host_test.c.o$") levels.o '"$LIBS"' -lpthread -o levels
  ./levels "$@"' levels "$@"
