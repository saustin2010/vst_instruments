"""Real waveform pictures for a port's option parameter, one per option, rendered by the port's own engine.
   python3 steve/tools/stitch/waveforms.py <port> <param> [--note 48] [--w 560 --h 120] [--adapter schwung]
For each option of <param> (from the port's params.json) the engine probe (steve/tools/probe) sets it, holds the note,
records ~70 ms and releases; this script then draws two periods (--periods) of the steady part as an SVG in the port's theme
colours (theme_accent / theme_lcd / theme_line from layout.conf) and writes images/waves/<param>_<i>.svg.
A layout shows them with  picture x= y= w= h= key=<param> files="images/waves/<param>_0.svg,..."  (MPC switches the
picture with the option: IndexedEnabling, verified on a device). Other knobs stay where they are while recording."""
import argparse, json, math, os, re, struct, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("port")
ap.add_argument("param")
ap.add_argument("--note", type=int, default=48)
ap.add_argument("--w", type=int, default=560)
ap.add_argument("--h", type=int, default=120)
ap.add_argument("--adapter", default=None)
ap.add_argument("--periods", type=float, default=2, help="periods of the note shown (a chord needs ~4)")
ap.add_argument("--set", action="append", default=[], help="key=value set before recording (e.g. a fixed timbre)")
a = ap.parse_args()

STEVE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
D = os.path.join(STEVE, "schwung-ports", a.port)
params = json.load(open(os.path.join(D, "params.json")))["params"]
p = next(x for x in params if x["key"] == a.param)
n = len(p["options"])
lay = open(os.path.join(D, "layout.conf")).read()
theme = dict(re.findall(r"^theme_(\w+)=(\w+)", lay, re.M))
accent = "#" + theme.get("accent_hi", theme.get("accent", "4fe6ff"))
bg = "#" + theme.get("lcd", "05070a")
grid = "#" + theme.get("line", "2a3038")

os.makedirs(os.path.join(D, "build", "waves"), exist_ok=True)
os.makedirs(os.path.join(D, "images", "waves"), exist_ok=True)
cmds = ["set:%s" % s for s in a.set]
for i in range(n):
    cmds += ["set:%s=%d" % (a.param, i), "hold:%d" % a.note, "dump:build/waves/%s_%d.raw:24" % (a.param, i),
             "off:%d" % a.note, "dump:/dev/null:80"]
env = dict(os.environ)
if a.adapter:
    env["ADAPTER"] = a.adapter
probe = os.path.join(STEVE, "tools", "probe", "probe.sh")
r = subprocess.run(["/opt/homebrew/bin/bash", probe, D, ""] + cmds, env=env, capture_output=True, text=True)
if r.returncode:
    sys.exit(r.stdout[-2000:] + r.stderr[-2000:])

period = 44100.0 / (440.0 * 2 ** ((a.note - 69) / 12.0))
files = []
for i in range(n):
    raw = open(os.path.join(D, "build", "waves", "%s_%d.raw" % (a.param, i)), "rb").read()
    s = [v / 32768.0 for v in struct.unpack("<%dh" % (len(raw) // 2), raw)[0::2]]   # left channel
    start = int(0.03 * 44100)
    span = int(round(a.periods * period))
    for k in range(start, len(s) - span - 1):   # an upward zero crossing, so pictures line up
        if s[k] <= 0 < s[k + 1]:
            start = k
            break
    seg = s[start:start + span]
    peak = max(1e-6, max(abs(v) for v in seg))
    W, H, pad = a.w, a.h, 10
    pts = " ".join("%.1f,%.1f" % (pad + (W - 2 * pad) * j / (len(seg) - 1), H / 2 - (H / 2 - pad) * 0.9 * v / peak)
                   for j, v in enumerate(seg))
    gl = "".join('<line x1="%d" y1="0" x2="%d" y2="%d" stroke="%s" stroke-width="1"/>' % (x, x, H, grid)
                 for x in range(0, W, W // 8)) + \
         "".join('<line x1="0" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="1"/>' % (y, W, y, grid)
                 for y in (H // 4, H // 2, 3 * H // 4))
    quiet = peak < 0.003
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n'
           ' <defs><filter id="g%d" x="-5%%" y="-20%%" width="110%%" height="140%%"><feGaussianBlur stdDeviation="2.2"/></filter></defs>\n'
           ' <rect width="%d" height="%d" rx="6" fill="%s"/>\n %s\n'
           % (W, H, W, H, i, W, H, bg, gl))
    if quiet:
        svg += ' <line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="2" opacity="0.5"/>\n' % (pad, H // 2, W - pad, H // 2, accent)
    else:
        svg += (' <polyline points="%s" fill="none" stroke="%s" stroke-width="5" opacity="0.35" filter="url(#g%d)"/>\n'
                ' <polyline points="%s" fill="none" stroke="%s" stroke-width="2.2" stroke-linejoin="round"/>\n'
                % (pts, accent, i, pts, accent))
    svg += "</svg>\n"
    rel = "images/waves/%s_%d.svg" % (a.param, i)
    open(os.path.join(D, rel), "w").write(svg)
    files.append(rel)
    print("%2d %-16s peak %.3f%s" % (i, p["options"][i], peak, "  (silent)" if quiet else ""))
print('picture key=%s files="%s"' % (a.param, ",".join(files)))
