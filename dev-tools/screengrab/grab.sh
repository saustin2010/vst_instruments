#!/usr/bin/env bash
# Screenshot the MPC (steve/tools/screengrab, 2026-10-02):  steve/tools/screengrab/grab.sh <mpc-address> [out.png]
# Copies drmgrab to the device's /tmp if it isn't there, reads the framebuffer the display plane shows (read-only),
# converts it to a landscape PNG in the mpc-vst-html-art container (Pillow). Default out: steve/screens/<time>.png
set -euo pipefail
HOST=${1:-${MPC_HOST:?pass the MPC address or set MPC_HOST}}
HERE=$(cd "$(dirname "$0")" && pwd); STEVE=$(dirname "$(dirname "$HERE")"); MV=$(dirname "$STEVE")
OUT=${2:-$STEVE/screens/$(date +%Y%m%d-%H%M%S).png}
mkdir -p "$(dirname "$OUT")" "$STEVE/screens"
SSH=(ssh -o ConnectTimeout=10 ${MPC_KEY:+-i "$MPC_KEY"} "root@$HOST")
"${SSH[@]}" '[ -x /tmp/drmgrab ]' || "${SSH[@]}" 'cat > /tmp/drmgrab.new && chmod +x /tmp/drmgrab.new && mv /tmp/drmgrab.new /tmp/drmgrab' < "$HERE/drmgrab"
RAW="$STEVE/screens/.last.raw"
# the display's card: card0 on some boots, card1 on others (2026-10-04): the one whose plane shows a framebuffer
CARD=$("${SSH[@]}" 'for c in /dev/dri/card*; do /tmp/drmgrab $c list 2>&1 | grep -q "fb [1-9]" && { echo $c; break; }; done')
[ -n "$CARD" ] || { echo "no display plane found on /dev/dri/card*" >&2; exit 1; }
"${SSH[@]}" "/tmp/drmgrab $CARD" > "$RAW" 2>/dev/null
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac
REAL=$(cd "$HERE" && pwd -P)   # in a workspace, steve/tools links into the repo: mount the real folder too
docker run --rm -v "$MV":"$MV" -v "$REAL":"$REAL":ro -v "$(dirname "$OUT")":"$(dirname "$OUT")" mpc-vst-html-art \
  python3 "$REAL/rawpng.py" "$RAW" "$OUT" >/dev/null
rm -f "$RAW"
echo "$OUT"
