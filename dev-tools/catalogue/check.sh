#!/usr/bin/env bash
# Can a plugin be released from its own repo yet? Builds it with the release tools (framework/setup-release.sh: what its
# repo's release workflow uses), runs the offline host test, and checks that the screen is the one in its deploy/ folder
# (the device build, made with framework/setup.sh's tools), by content (skin_same.py):
#   dev-tools/catalogue/check.sh <plugin> [<plugin> ...]
# Writes only the plugin's build/ (git-ignored); deploy/ is left alone. A plugin's library (tools/fetch-presets.py) is
# put next to the test binary as its data folder (MODULE_SUBDIR) or mounted at MODULE_DIR. MPC_VST=<checkout> uses
# other tools. Logs: <plugin>/build/check-build.log, check-test.log. Prints PASSED/FAILED and "screen: same" per plugin.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
cd "$REPO"
. tools/common.sh
. tools/docker-ready.sh
MV=${MPC_VST:-$REPO/framework/release-tools}
[ -x "$MV/tools/build_port.sh" ] || { echo "no release tools at $MV: run framework/setup-release.sh first"; exit 1; }
BASH5=$(command -v /opt/homebrew/bin/bash || command -v /usr/local/bin/bash || command -v bash)
[ $# -gt 0 ] || { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
docker_ready || exit 1

status=0
for name in "$@"; do
  rel=$(lookup "$name") || exit 1
  D=$REPO/$rel
  n=$(basename "$rel")
  printf "== %-30s " "$rel"
  mkdir -p "$D/build"
  if ! "$BASH5" "$MV/tools/build_port.sh" "$D/vst.json" > "$D/build/check-build.log" 2>&1; then
    echo "BUILD FAILED"; grep -E 'error|SystemExit|layout:|Traceback' "$D/build/check-build.log" | grep -v Wstrict | head -8; status=1; continue
  fi
  def() { python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('defines',{}).get(sys.argv[2],'').strip('\"'))" "$D/vst.json" "$1"; }
  P=$REPO/presets/$n MD=$(def MODULE_DIR) SUB=$(def MODULE_SUBDIR)
  # leak detection on, as in the release workflow's host test (a leak fails the release run; tools/build.sh has it off)
  export TEST_DOCKER_ARGS="-e ASAN_OPTIONS=detect_leaks=1"
  if [ -d "$P" ]; then
    [ -n "$MD" ] && TEST_DOCKER_ARGS="$TEST_DOCKER_ARGS -v $P:$MD:ro"
    [ -n "$SUB" ] && { rm -rf "${D:?}/build/$SUB"; cp -R "$P" "$D/build/$SUB"; }
  fi
  if "$BASH5" "$MV/tools/test_port.sh" "$D/vst.json" > "$D/build/check-test.log" 2>&1 && grep -q '^PASSED' "$D/build/check-test.log"; then
    printf "PASSED  "
  else
    echo "TEST FAILED (build/check-test.log)"; grep -E '^FAIL' "$D/build/check-test.log" | head -5 || true; status=1; continue
  fi
  # skin_same.py compares pictures by their pixels with Pillow: the html-art image has it (built by build_port.sh)
  if docker run --rm -u "$(id -u):$(id -g)" -v "$REPO":"$REPO" -w "$REPO" mpc-vst-html-art \
       python3 dev-tools/catalogue/skin_same.py "$D" > "$D/build/check-skin.log" 2>&1; then
    echo "screen: same"
  else
    echo "screen: DIFFERENT (build/check-skin.log)"; head -12 "$D/build/check-skin.log"; status=1
  fi
done
exit $status
