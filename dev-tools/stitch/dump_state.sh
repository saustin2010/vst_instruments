#!/usr/bin/env bash
# Write <port>/build/state.json: every parameter's value and display text right after a fresh insert, from the real
# engine (x86, in the gcc:12 container), for showcase.py's screenshots. Needs the objects the offline test leaves in
# <port>/build (run skin-redesign/build.sh <port> first). Mounts the port's data folder at its MODULE_DIR like the test.
#   steve/tools/stitch/dump_state.sh <port> [<port> ...]
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); STEVE=$(dirname "$(dirname "$HERE")")
for port in "$@"; do
  D="$STEVE/schwung-ports/$port"
  MD=$(python3 -c "import json;print(json.load(open('$D/vst.json')).get('defines',{}).get('MODULE_DIR','').strip('\"'))")
  LIBS=$(python3 -c "import json;print(' '.join(json.load(open('$D/vst.json')).get('build',{}).get('libs',[])))")
  ARGS=()
  DATA=$D/deploy/vst/$(basename "$MD")   # the data folder: in deploy/, or (vst_instruments) the repo's presets/<port>
  [ -d "$DATA" ] || DATA=$(cd "$D" && git rev-parse --show-toplevel 2>/dev/null)/presets/$port
  [ -n "$MD" ] && [ -d "$DATA" ] && ARGS=(-v "$DATA":"$MD":ro)
  ls "$D"/build/host_*.o >/dev/null 2>&1 || { echo "$port: no build/host_*.o (run build.sh $port)"; continue; }
  mkdir -p "$D/logs"
  if docker run --rm -u "$(id -u):$(id -g)" -v "$D":"$D" -v "$HERE":"$HERE":ro "${ARGS[@]}" -w "$D/build" \
       -e ASAN_OPTIONS=detect_leaks=0 gcc:12 bash -c '
    set -e
    gcc -fsanitize=address,undefined -g -O1 -c "'"$HERE"'/dump_state.c" -o dump_state.o
    g++ -fsanitize=address,undefined $(ls host_*.o | grep -v "^host_host_test.c.o$") dump_state.o '"$LIBS"' -lpthread -o dump_state
    ./dump_state > state.json.new && mv state.json.new state.json' >"$D/logs/state.log" 2>&1; then
    echo "$port: $(python3 -c "import json;print(len(json.load(open('$D/build/state.json'))))" 2>&1) params"
  else
    echo "$port: FAILED (logs/state.log)"; tail -3 "$D/logs/state.log"
  fi
done
