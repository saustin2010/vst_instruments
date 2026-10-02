#!/usr/bin/env bash
# Copy the audio FX adapter, the Schwung host header and the repo's wrapper/engine.h into each effect port's mpc/:
#   steve/tools/audiofx/sync.sh [port ...]      (no args: every port that builds mpc/schwung_audio_fx.c)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); STEVE=$(dirname "$(dirname "$HERE")"); MV=$(dirname "$STEVE")
ports=("$@")
if [ $# -eq 0 ]; then
  for v in "$STEVE"/schwung-ports/*/vst.json; do
    grep -q 'mpc/schwung_audio_fx.c' "$v" && ports+=("$(basename "$(dirname "$v")")")
  done
fi
for p in "${ports[@]}"; do
  M="$STEVE/schwung-ports/$p/mpc"; mkdir -p "$M/host"
  cp "$HERE/schwung_audio_fx.c" "$M/"; cp "$STEVE/tools/midifx/host/plugin_api_v1.h" "$M/host/"; cp "$MV/wrapper/engine.h" "$M/"
  echo "$p: mpc/ updated"
done
