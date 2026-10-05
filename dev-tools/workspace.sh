#!/usr/bin/env bash
# The tools in dev-tools/ were written inside a working folder laid out as
#   <mpc-vst-plugins checkout>/steve/{tools, schwung-ports/<plugin>, stitch_layouts/<plugin>.md}
# This recreates that layout with symlinks into this repo, so they run unchanged:
#   dev-tools/workspace.sh            (after framework/setup.sh)
# Then, for example:  cd framework/mpc-vst-plugins && python3 steve/tools/stitch/convert.py hera
# Edits made through the links land in this repo's files. Safe to run again.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
FW=${MPC_VST_FRAMEWORK:-$REPO/framework/mpc-vst-plugins}
[ -d "$FW/.git" ] || { echo "no framework at $FW: run framework/setup.sh first"; exit 1; }
W=$FW/steve
mkdir -p "$W/schwung-ports" "$W/stitch_layouts/stitch_png"
grep -qx 'steve/' "$FW/.git/info/exclude" 2>/dev/null || echo 'steve/' >> "$FW/.git/info/exclude"
ln -sfn "$REPO/dev-tools" "$W/tools"
n=0
for v in "$REPO"/{schwung/*,mutable-instruments,vcv-rack,originals,mpc-ports}/*/vst.json; do
  [ -f "$v" ] || continue
  d=$(dirname "$v"); p=$(basename "$d")
  ln -sfn "$d" "$W/schwung-ports/$p"
  [ -f "$d/design/stitch.html" ] && ln -sfn "$d/design/stitch.html" "$W/stitch_layouts/$p.md"
  n=$((n + 1))
done
echo "workspace: $W ($n plugins linked)"
