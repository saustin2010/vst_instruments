#!/usr/bin/env bash
# Copy this adapter, its Schwung host headers and the repo's wrapper/engine.h into every sequencer port's mpc/ folder
# (each port's build compiles its own copy, so the ports stay self-contained):
#   steve/tools/midifx/sync.sh [port ...]      (no args: all six)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); STEVE=$(dirname "$(dirname "$HERE")"); MV=$(dirname "$STEVE")
ports=("$@"); [ $# -gt 0 ] || ports=(eucalypso superarp groovebank pixelwalkers mazelite midiplayer)
for p in "${ports[@]}"; do
  M="$STEVE/schwung-ports/$p/mpc"; mkdir -p "$M/host"
  cp "$HERE/schwung_midi_fx.c" "$M/"; cp "$HERE"/host/*.h "$M/host/"; cp "$MV/wrapper/engine.h" "$M/"
  echo "$p: mpc/ updated"
done
