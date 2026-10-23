#!/usr/bin/env bash
# Build one plugin from source, run the offline test and refresh its deploy/ folder (see BUILDING.md):
#   tools/build.sh <plugin> [<plugin> ...]          e.g.  tools/build.sh hera
# Needs framework/setup.sh run once (or MPC_VST_FRAMEWORK=<an mpc-vst-plugins checkout with the patch>), Python 3,
# bash 4+ and Docker (tools/docker-ready.sh starts Colima and registers 32-bit ARM emulation if they're missing).
# Output per plugin: build/ (compiler and skin output, logs: build.log, test.log) and a fresh deploy/ (the .so, its
# screen, its plugin-list entry; its presets stay in presets/<plugin>/). Prints PASSED/FAILED.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO"
. tools/common.sh
. tools/docker-ready.sh
FW=${MPC_VST_FRAMEWORK:-$REPO/framework/mpc-vst-plugins}
[ -x "$FW/tools/build_port.sh" ] || { echo "no framework at $FW: run framework/setup.sh first"; exit 1; }
BASH5=$(command -v /opt/homebrew/bin/bash || command -v /usr/local/bin/bash || command -v bash)
"$BASH5" -c '[ "${BASH_VERSINFO[0]}" -ge 4 ]' || { echo "needs bash 4 or later (macOS: brew install bash)"; exit 1; }
[ $# -gt 0 ] || { sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
docker_ready || exit 1

HAS_PRESETS=" $(python3 tools/fetch-presets.py --list) "
# Screens the current tools don't draw as they were approved: made with this repo's older tools (2026-10-23, when
# the builds moved to the release tools), whose sub-page mode pictures, stepper art in a sub-page's picture, knob
# label_scale and meter strips weren't carried over. Their committed deploy/Synths stays; the .so and the plugin-list
# entry are refreshed (parameters are append-only, so the screen's bindings still hold). Their releases ship deploy/
# too. To change one's screen, carry those features over (framework/README.md) or review the new one on the device.
KEEP_SCREEN=" incipience stevequencer stevequencer16 mpcplaits mutablevibe "

status=0
for name in "$@"; do
  rel=$(lookup "$name") || exit 1
  [ "$(echo "$rel" | wc -l)" -eq 1 ] || { echo "$name: more than one plugin"; exit 1; }
  D=$REPO/$rel
  printf "== %-30s " "$rel"
  mkdir -p "$D/build"
  "$BASH5" "$FW/tools/build_port.sh" "$D/vst.json" > "$D/build/build.log" 2>&1 || { echo "BUILD FAILED"; tail -20 "$D/build/build.log"; status=1; continue; }
  SKIN=$(ls -d "$D/build/skin/"*/ | head -1); SKIN=${SKIN%/}
  SO=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['so'])" "$D/vst.json")
  MD=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('defines',{}).get('MODULE_DIR','').strip('\"'))" "$D/vst.json")
  keep=""; case "$KEEP_SCREEN" in *" $(basename "$D") "*) keep=" (screen kept: older tools)" ;; esac
  if [ -n "$keep" ]; then
    rm -rf "$D/deploy/vst"; mkdir -p "$D/deploy/vst"
  else
    rm -rf "$D/deploy"; mkdir -p "$D/deploy/vst" "$D/deploy/Synths"
    cp -R "$SKIN" "$D/deploy/Synths/"
  fi
  cp "$D/build/$SO" "$D/deploy/vst/"
  python3 tools/deploy-entry.py "$D"   # file= /sdcard/vst/<so> and vst.json's category, as this repo installs it
  P=$REPO/presets/$(basename "$D")   # its presets/kits/... (not in git): fetched if missing
  case "$HAS_PRESETS" in *" $(basename "$D") "*) [ -n "$(ls -A "$P" 2>/dev/null)" ] ||
    python3 tools/fetch-presets.py "$(basename "$D")" >/dev/null || echo "(couldn't fetch its presets: testing without)" ;; esac
  # the offline test, with the data folder where the MPC has it (engines that load presets find them)
  TEST_DOCKER_ARGS="-e ASAN_OPTIONS=detect_leaks=0"
  [ -n "$MD" ] && [ -d "$P" ] && TEST_DOCKER_ARGS="$TEST_DOCKER_ARGS -v $P:$MD:ro"
  export TEST_DOCKER_ARGS
  if "$BASH5" "$FW/tools/test_port.sh" "$D/vst.json" > "$D/build/test.log" 2>&1 && grep -q "^PASSED" "$D/build/test.log"; then
    echo "PASSED$keep"
  else
    echo "TEST FAILED (build/test.log)"; grep -E '^FAIL' "$D/build/test.log" | head -5 || true; status=1
  fi
done
exit $status
