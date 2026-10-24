#!/usr/bin/env bash
# MPC's count-in, replayed through a sequencer with each COUNT-IN setting (countin.c): did MIDI go out before the real
# start, and when did it begin?
#   dev-tools/countin/run.sh <plugin dir> [<count_in param index>]     e.g. originals/stevequencer16
# Needs the objects the offline test leaves in <plugin>/build (tools/build.sh <plugin> first). Runs in gcc:12 like it.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
D=$(cd "$1" && pwd)
LIBS=$(python3 -c "import json;print(' '.join(json.load(open('$D/vst.json')).get('build',{}).get('libs',[])))")
ls "$D"/build/host_*.o >/dev/null 2>&1 || { echo "$D: no build/host_*.o (run tools/build.sh first)"; exit 1; }
docker run --rm -u "$(id -u):$(id -g)" -v "$D":"$D" -v "$HERE":"$HERE":ro -w "$D/build" \
  -e ASAN_OPTIONS=detect_leaks=0 -e MIDIFX_DEBUG=1 gcc:12 bash -c '
  set -e
  gcc -O2 -c "'"$HERE"'/countin.c" -o countin.o
  g++ -fsanitize=address,undefined $(ls host_*.o | grep -v "^host_host_test.c.o$") countin.o '"$LIBS"' -lpthread -o countin
  ./countin "$@" 2>&1 >/dev/null' countin "${2:--1}" | awk '
  /^== case/ { name = substr($0, 4); phase = ""; early = 0; first = ""; n = 0; beat = 0; next }
  /^== play pressed/ { phase = "count"; next }
  /^== real start/ { phase = "play"; beat = 0; next }
  /^== beat / { beat = $3; next }
  /^== stopping/ {
      printf "%-70s %s\n", name, (early ? "MIDI DURING THE COUNT-IN (" early ")" : "nothing before the start") \
             (n ? ", then " n " messages from beat " first : ", then nothing")
      phase = ""; next }
  /^midi out/ { if (phase == "count") early++; else if (phase == "play") { if (!n) first = beat; n++ } }
  /^== end/ { exit }'
