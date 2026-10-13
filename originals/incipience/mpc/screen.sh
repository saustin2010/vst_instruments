#!/usr/bin/env bash
# Incipience's screen, from the Stitch design to the checked skin and the README screenshots, in order:
#   originals/incipience/mpc/screen.sh          (needs framework/setup.sh and dev-tools/workspace.sh once, and Docker)
# 1 mpc/gen.py       params.json, src/params_def.h, build/pages.json (the page plan)
# 2 mpc/design.py    design/stitch.html: every page in the Stitch design's markup (design/stitch-kick.html)
# 3 convert.py       layout.conf with a tab per page, the backgrounds and knob filmstrips (images/stitch/)
# 4 mpc/merge_ab.py  13 tabs, each with its A and B Q-Link sub-pages
# 5 tools/build.sh   the ARM .so, the skin, the offline test (must say PASSED)
# 6 check_skin.py    bindings, Q-Links, touch boxes, options (must say OK)
# 7 dump_state.sh + showcase.py   screenshots/page_<tab>[_1].png with the engine's real values
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
REPO=$(cd "$P/../.." && pwd)
FW=$REPO/framework/mpc-vst-plugins
python3 "$P/mpc/gen.py"
python3 "$P/mpc/design.py"
(cd "$FW" && python3 steve/tools/stitch/convert.py incipience)
python3 "$P/mpc/merge_ab.py"
"$REPO/tools/build.sh" incipience
(cd "$FW" && python3 steve/tools/stitch/check_skin.py incipience)
(cd "$FW" && steve/tools/stitch/dump_state.sh incipience)
rm -f "$P"/screenshots/page_*.png
(cd "$REPO" && docker run --rm -v "$REPO:$REPO" -w "$FW" mpc-vst-html-art python3 steve/tools/stitch/showcase.py incipience \
  "$P/screenshots" --banks)
