"""What a port's Stitch reskin needs to know, in one go:  python3 steve/tools/stitch/survey.py <port> [--no-shots]
Prints the port's parameters (key | name | range or options), its current pages and Q-Links, and an outline of the
design (stitch_layouts/<port>.md: tags with ids, classes, data-*/on* attributes, inline rotations, text), and writes
every tab of the design to stitch_layouts/build/look/<port>_tabs.png (container, shoot.py + a sheet)."""
import glob, json, os, re, shutil, subprocess, sys
from html.parser import HTMLParser

HERE = os.path.dirname(os.path.abspath(__file__))
STEVE = os.path.dirname(os.path.dirname(HERE))
MV = os.path.dirname(STEVE)
# what Docker mounts: the framework checkout, or in a vst_instruments workspace (framework/mpc-vst-plugins inside
# the repo, plugins linked from the repo) the whole repo, so the links resolve in the container too
DOCKER_ROOT = (os.path.dirname(os.path.dirname(MV)) if os.path.basename(os.path.dirname(MV)) == "framework"
               and os.path.exists(os.path.join(os.path.dirname(MV), "setup.sh")) else MV)
port = sys.argv[1]
D = os.path.join(STEVE, "schwung-ports", port)
SL = os.path.join(STEVE, "stitch_layouts")

print("== params")
for p in json.load(open(os.path.join(D, "params.json")))["params"]:
    rng = p.get("options") and "[%s]" % ",".join(map(str, p["options"][:12])) + ("+%d" % (len(p["options"]) - 12) if len(p["options"]) > 12 else "") \
        or "%s..%s" % (p.get("min", ""), p.get("max", ""))
    extra = " ".join("%s=%s" % (k, p[k]) for k in ("type", "step_of", "display") if p.get(k))
    print("%-22s %-14s %s %s" % (p["key"], p.get("name", ""), rng, extra))
print("== current pages")
lay = open(os.path.join(D, "layout.grid.conf" if os.path.exists(os.path.join(D, "layout.grid.conf")) else "layout.conf")).read()
for l in lay.splitlines():
    if l.startswith("[tab") or l.startswith("qlinks") or re.match(r"(button|popup|stepper|toggle|picture|meter) ", l):
        print(" ", l[:160])

print("== design outline")
SK = {'path', 'circle', 'line', 'rect', 'polyline', 'polygon', 'g', 'defs', 'stop', 'linearGradient', 'radialGradient',
      'ellipse', 'text', 'filter', 'feGaussianBlur', 'pattern', 'mask', 'clipPath', 'use', 'tspan', 'feMerge', 'feMergeNode'}
VOID = {'input', 'br', 'img', 'meta', 'link', 'hr'}


class P(HTMLParser):
    d = 0
    skip = 0

    def handle_starttag(self, t, a):
        if t in SK:
            return
        a = dict(a)
        s = t + ("#" + a["id"] if a.get("id") else "")
        cls = [c for c in (a.get("class") or "").split() if not re.match(r"^(-?(p|m|px|py|pt|pb|pl|pr|mx|my|mt|mb|ml|mr|gap|space|w|h|min|max|text|font|tracking|leading|bg|border|rounded|shadow|flex|grid|items|justify|col|row|place|self|inset|top|left|right|bottom|z|opacity|overflow|transition|duration|ease|hover|active|focus|cursor|select|uppercase|relative|absolute|fixed|block|inline|hidden|truncate|whitespace|ring|from|to|via|backdrop|blur|drop|animate|scale|translate|origin|pointer|divide|order|aspect|object|sr|sm|md|lg|xl|dark|group|peer|fill|stroke|outline|appearance)\b|\[)", c)]
        if cls:
            s += "." + ".".join(cls[:5])
        for k, v in a.items():
            if k.startswith("data-") or k.startswith("on") or k in ("type", "value") or (k == "style" and "rotate" in (v or "")):
                s += " %s=%s" % (k, (v or "")[:40])
        print("  " * self.d + s)
        if t not in VOID:
            self.d += 1

    def handle_endtag(self, t):
        if t in SK or t in VOID:
            return
        self.d -= 1

    def handle_data(self, x):
        x = " ".join(x.split())
        if x:
            print("  " * self.d + '"' + x[:70] + '"')


h = open(os.path.join(SL, port + ".md")).read()
P().feed(re.sub(r"<script.*?</script>", "", h[h.find("<body"):], flags=re.S))

if "--no-shots" not in sys.argv:
    L = os.path.join(SL, "build", "look")
    os.makedirs(os.path.join(L, "shots"), exist_ok=True)
    for f in glob.glob(os.path.join(L, "*.md")) + glob.glob(os.path.join(L, "shots", "*")):
        os.remove(f)
    shutil.copy(os.path.join(SL, port + ".md"), L)
    r = subprocess.run(["docker", "run", "--rm", "-v", "%s:%s" % (DOCKER_ROOT, DOCKER_ROOT), "-w", MV, "mpc-vst-html-art", "sh", "-c",
                        "python3 %s %s && cd %s && python3 grid.py %s_tabs.png 2 shots/*.png" % (
                            os.path.join(HERE, "shoot.py"), L, L, port)], capture_output=True, text=True)
    print("== tabs\n" + r.stdout.strip() + r.stderr[-1500:])
    print("sheet:", os.path.join(L, port + "_tabs.png"))
