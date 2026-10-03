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
"${SSH[@]}" '/tmp/drmgrab /dev/dri/card0' > "$RAW" 2>/dev/null
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac
docker run --rm -v "$MV":"$MV" -v "$(dirname "$OUT")":"$(dirname "$OUT")" mpc-vst-html-art \
  python3 "$HERE/rawpng.py" "$RAW" "$OUT" >/dev/null
rm -f "$RAW"
echo "$OUT"
