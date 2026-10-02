#!/usr/bin/env bash
# Build, test, preview and package one port in steve/schwung-ports/<port>:
#   steve/tools/skin-redesign/build.sh <port> [data_src:data_dest ...]
# e.g. build.sh braids src/presets:presets   (copies <port>/src/presets to deploy/vst/<MODULE_DIR name>/presets)
# Output: <port>/build (compiler + skin output), <port>/preview/page_N.png, <port>/logs/{build,test}.log and
# <port>/deploy/, laid out like the device:  deploy/vst/<x>.so, deploy/vst/<data dir>/..., deploy/Synths/<skin>/,
# deploy/pluginlist-entry.xml. Needs Docker (Colima) with arm32 emulation (see steve/README.md).
set -euo pipefail
PORT=$1; shift
STEVE=$(cd "$(dirname "$0")/../.." && pwd); MV=$(dirname "$STEVE"); D="$STEVE/schwung-ports/$PORT"
BASH5=$(command -v /opt/homebrew/bin/bash || command -v bash)
mkdir -p "$D/logs"

"$BASH5" "$MV/tools/build_port.sh" "$D/vst.json" > "$D/logs/build.log" 2>&1 || { tail -20 "$D/logs/build.log"; exit 1; }
grep -E "^(params.h|skin|exported|highest)" "$D/logs/build.log" | sed "s/^/  /"

SKIN=$(ls -d "$D/build/skin/"*/ | head -1); SKIN=${SKIN%/}
SO=$(python3 -c "import json;print(json.load(open('$D/vst.json'))['so'])")
MD=$(python3 -c "import json;print(json.load(open('$D/vst.json')).get('defines',{}).get('MODULE_DIR','').strip('\"'))")
rm -rf "$D/deploy"; mkdir -p "$D/deploy/vst" "$D/deploy/Synths"
cp "$D/build/$SO" "$D/deploy/vst/"
cp -R "$SKIN" "$D/deploy/Synths/"
cp "$D/build/pluginlist-entry.xml" "$D/deploy/"
for pair in "$@"; do
  src=${pair%%:*}; dst=${pair#*:}
  [ -n "$MD" ] || { echo "data given but vst.json has no MODULE_DIR"; exit 1; }
  mkdir -p "$D/deploy/vst/$(basename "$MD")/$(dirname "$dst")"
  cp -R "$D/$src" "$D/deploy/vst/$(basename "$MD")/$dst"
done
# test with the data folder mounted where the device has it, so preset-loading engines see their presets
# leak detection off: some upstream engines (Noisemaker) leak at teardown, and LeakSanitizer's exit drops the
# test's buffered output. Address/UB checks stay on.
TEST_DOCKER_ARGS="-e ASAN_OPTIONS=detect_leaks=0"
[ -n "$MD" ] && [ -d "$D/deploy/vst/$(basename "$MD")" ] && TEST_DOCKER_ARGS="$TEST_DOCKER_ARGS -v $D/deploy/vst/$(basename "$MD"):$MD:ro"
export TEST_DOCKER_ARGS
"$BASH5" "$MV/tools/test_port.sh" "$D/vst.json" > "$D/logs/test.log" 2>&1 || true
if grep -q "^PASSED" "$D/logs/test.log"; then echo "  test: PASSED"; else
  echo "  test: $(grep -E '^(FAIL|FAILED)' "$D/logs/test.log" | tr '\n' ' ' || echo 'no result (see logs/test.log)')"; fi
grep -E "^warn" "$D/logs/test.log" | sed "s/^/  /" || true

rm -rf "$D/preview"; mkdir -p "$D/preview"
DR=$MV   # in a vst_instruments workspace (framework inside the repo) mount the repo, so linked plugins resolve
[ "$(basename "$(dirname "$MV")")" = framework ] && [ -f "$(dirname "$MV")/setup.sh" ] && DR=$(dirname "$(dirname "$MV")")
docker run --rm -v "$DR":"$DR" -w "$MV" mpc-vst-html-art python3 tools/studio.py preview "$SKIN/Plugin Skins" \
  -o "$D/preview/page_%d.png" > "$D/logs/preview.log" 2>&1 || { tail -5 "$D/logs/preview.log"; exit 1; }
sed "s/^/  preview: /" "$D/logs/preview.log" | grep -v "wrote" || true

echo "  deploy: $(cd "$D/deploy" && find . -maxdepth 3 -not -path "./Synths/*/*" | sort | tr '\n' ' ')"
