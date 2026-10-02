#!/usr/bin/env bash
# Record the MPC's screen while someone uses it (steve/tools/screengrab, 2026-10-02):
#   steve/tools/screengrab/watch.sh [seconds=600] [interval=1.5] [max=80] [host]
# Grabs the framebuffer every interval, keeps only frames that differ from the last kept one (md5), stops after
# `seconds` or `max` kept frames, then converts them to PNGs in steve/screens/watch-<time>/ (NNN_<hhmmss>.png).
set -uo pipefail
SECS=${1:-600}; IV=${2:-1.5}; MAX=${3:-80}; HOST=${4:-mpc-live-ii.local}
HERE=$(cd "$(dirname "$0")" && pwd); STEVE=$(dirname "$(dirname "$HERE")"); MV=$(dirname "$STEVE")
OUT="$STEVE/screens/watch-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OUT/raw"
SSH=(ssh -o ConnectTimeout=10 -o ServerAliveInterval=5 ${MPC_KEY:+-i "$MPC_KEY"} "root@$HOST")
"${SSH[@]}" '[ -x /tmp/drmgrab ]' || "${SSH[@]}" 'cat > /tmp/drmgrab.new && chmod +x /tmp/drmgrab.new && mv /tmp/drmgrab.new /tmp/drmgrab' < "$HERE/drmgrab"
# one ssh session: the device loops and sends "FRAME <n> <hhmmss> <bytes>\n" + that many bytes of gzip per changed frame
"${SSH[@]}" "last=; i=0; end=\$((\$(date +%s) + $SECS)); while [ \$(date +%s) -lt \$end ]; do
  /tmp/drmgrab /dev/dri/card0 > /tmp/wg.raw 2>/dev/null; m=\$(md5sum /tmp/wg.raw | cut -c1-32)
  if [ \"\$m\" != \"\$last\" ]; then last=\$m; i=\$((i+1)); gzip -1 -c /tmp/wg.raw > /tmp/wg.gz
    echo \"FRAME \$i \$(date +%H%M%S) \$(wc -c < /tmp/wg.gz)\"; cat /tmp/wg.gz; [ \$i -ge $MAX ] && break; fi
  sleep $IV; done; rm -f /tmp/wg.raw /tmp/wg.gz" |
python3 -c "
import gzip, sys
out, f = sys.argv[1], sys.stdin.buffer
while True:
    line = f.readline()
    if not line:
        break
    if not line.startswith(b'FRAME '):
        continue
    _, n, t, size = line.split()
    path = '%s/raw/%03d_%s.raw' % (out, int(n), t.decode())
    open(path, 'wb').write(gzip.decompress(f.read(int(size))))
    print(path, flush=True)
" "$OUT"
for r in "$OUT"/raw/*.raw; do
  [ -f "$r" ] || continue
  docker run --rm -v "$MV":"$MV" mpc-vst-html-art python3 "$HERE/rawpng.py" "$r" "${r%.raw}.png" >/dev/null && mv "${r%.raw}.png" "$OUT/"
done
rm -rf "$OUT/raw"; ls "$OUT" | wc -l; echo "$OUT"
