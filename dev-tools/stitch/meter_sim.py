"""Draw a built skin's pages the way MPC would for given parameter values: each page's background, then every
filmstrip meter on that page at its bounds with the frame its value selects, each shown only while its
IndexedEnabling band matches. Checks envelope displays offline (steve/tools/stitch, 2026-10-02).
   python3 meter_sim.py <Plugin Skins dir> <out prefix> key=value ... [--band=round|floor] [--all]
Values are normalized 0..1 (missing keys: 0). Writes <prefix>_<page>_<TAB>.png for each page that has a meter
(--all: every page). Model (as Akai's skins lay strips out): numFrames = the frame count, a frame is height /
numFrames tall, value v shows frame round(v * (numFrames - 1)); a band i of N is round(v * (N - 1)) (--band=floor: min(floor(v * N), N - 1))."""
import json, os, re, sys
from PIL import Image

skin, prefix = sys.argv[1], sys.argv[2]
vals = {k: float(v) for k, v in (a.split("=", 1) for a in sys.argv[3:] if "=" in a and not a.startswith("--"))}
band_floor = "--band=floor" in sys.argv
d = json.load(open(os.path.join(skin, "TUI.json")))
defs = {e["key"]: e["value"] for e in d["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
cache = {}


def image(name):
    if name not in cache:
        cache[name] = Image.open(os.path.join(skin, name)).convert("RGBA")
    return cache[name]


def param_of(o):
    for m in o.get("handle remapping", {}).get("map", []):
        if m["key"] == "Data":
            return int(m["value"].split()[-1])
    return -1


for t, tab in enumerate(d["pageData"]["tabs"]):
    kids = defs[tab["componentName"]]["componentsData"]
    meters = [o for o in kids if str(o["componentData"].get("type", "")).startswith("shMeter_")]
    if not meters and "--all" not in sys.argv:
        continue
    names = {param_of(o): o["componentData"]["name"] for o in meters}
    page = None
    for o in kids:   # the page's own background image comes first
        data = o["componentData"].get("data") or {}
        if o["componentData"].get("type") == "Image" and str(data.get("image", "")).startswith("sh_bg_"):
            page = image(data["image"]).copy(); break
    if page is None:
        page = Image.new("RGBA", (1280, 628), (20, 20, 20, 255))
    for o in meters:
        key = o["componentData"]["name"]
        h = o["bounds"]["additionalInvalidatingHandles"]
        if h:
            m = re.match(r"IndexedEnabling/(\d+)/(\d+)/Parameter (\d+)", h[0])
            i, n, p = (int(x) for x in m.groups())
            v = vals.get(names.get(p, ""), 0.0)
            idx = min(int(v * n), n - 1) if band_floor else round(v * (n - 1))
            if idx != i:
                continue
        kd = defs[o["componentData"]["type"]]["componentsData"][0]["componentData"]["data"]
        strip = image(kd["filmStrip"])
        nf = kd["numFrames"]
        fw, fh = strip.size[0], strip.size[1] / nf   # Akai's layout: numFrames = the frame count, frame = height / count
        f = round(vals.get(key, 0.0) * (nf - 1))
        x, y, w, hh = (int(s) for s in o["bounds"]["bounds"].split())
        page.alpha_composite(strip.crop((0, round(f * fh), fw, round((f + 1) * fh))).resize((w, hh)), (x, y))
    out = "%s_%d_%s.png" % (prefix, t, re.sub(r"\W+", "", tab.get("tabName", "")))
    page.save(out)
    print("wrote", out)
