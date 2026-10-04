"""Screenshots of a built skin as MPC draws it, for docs:  python3 steve/tools/stitch/showcase.py <port> <out_dir>
Like `tools/studio.py preview`, but every control sits at its parameter's default: knobs and displays on their
frame, switches and buttons lit by value, controls shown/hidden by their when= parameter, value labels with the
text the wrapper reports (option label, or the number as effGetParamDisplay formats it), names and values in
Titillium Web (the font MPC uses). No outlines, no Q-Link box. With <port>/build/state.json (dump_state.sh: the real
engine's values and text right after an insert, preset names included) it uses those; without, the declared defaults
and no run-time text. Writes <out_dir>/page_N.png (1280 x 628); with a third argument --banks also every further Q-Link
sub-page as page_N_S.png (for layouts whose sub-pages differ: banks=). Needs Pillow (the mpc-vst-html-art image has it):
  docker run --rm -v $PWD:$PWD -w $PWD mpc-vst-html-art python3 steve/tools/stitch/showcase.py hera /tmp/hera"""
import glob, json, math, os, re, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
STEVE = os.path.dirname(os.path.dirname(HERE))
MV = os.path.dirname(STEVE)
FONTS = os.path.join(MV, "tools", "html_art", "fonts")
W, H = 1280, 628


def params_h(path):
    """build/params.h -> [dict(key, min, max, def, opts, string, int)] in VST index order."""
    src = open(path).read()
    opts = {m.group(1): re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
            for m in re.finditer(r"static const char \*const (OPTS_\d+)\[\] = \{(.*?)\};", src)}
    out = []
    for row in re.findall(r"^\s*\{(\".*)\},\s*$", src.split("PARAMS[] = {", 1)[1], re.M):
        f = [x.strip() for x in re.findall(r'"(?:[^"\\]|\\.)*"|[^,]+', row)]
        out.append({"key": f[0].strip('"'), "min": float(f[3].rstrip("f")), "max": float(f[4].rstrip("f")),
                    "def": float(f[5].rstrip("f")), "opts": opts.get(f[7], []), "string": f[9] == "1",
                    "int": f[10] == "1"})
    return out


def display(p):
    if p["opts"]:
        return p["opts"][int(round(p["def"] * (len(p["opts"]) - 1)))]
    if p["string"]:
        return ""
    v = p["min"] + (p["max"] - p["min"]) * p["def"]
    if p["int"]:
        return "%d" % int(math.floor(v + 0.5))
    return "%.*f" % (0 if abs(p["max"] - p["min"]) > 20 else 1, v)


_fonts = {}


def font(style, height):
    """Titillium Web at a size whose ascent + descent is `height` px (JUCE's font height)."""
    k = (style, height)
    if k not in _fonts:
        f = os.path.join(FONTS, "TitilliumWeb-%s.ttf" % {"Bold": "Bold", "SemiBold": "SemiBold"}.get(style, "Regular"))
        size = max(4, int(round(height)))
        while size > 4 and sum(ImageFont.truetype(f, size).getmetrics()) > height:
            size -= 1
        _fonts[k] = ImageFont.truetype(f, size)
    return _fonts[k]


def argb(c):
    return tuple(int(c[i:i + 2], 16) for i in (2, 4, 6, 0)) if len(c) >= 8 else (200, 200, 200, 255)


def text(im, box, s, ts):
    if not s:
        return
    if ts.get("case") == "Upper Case":
        s = s.upper()
    x, y, w, h = box
    f = font(ts["font"].get("style", "Regular"), float(ts["font"]["height"]))
    layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    while d.textlength(s, font=f) > w and len(s) > 1:   # MPC squeezes, then cuts; just cut
        s = s[:-1]
    tw = d.textlength(s, font=f)
    asc, desc = f.getmetrics()
    j = ts.get("justification", "")
    tx = x + (w - tw) / 2 if "horizontallyCentred" in j or "centred" == j else (x + w - tw if "right" in j else x)
    ty = y + (h - asc - desc) / 2 if "verticallyCentred" in j or "centred" == j else (y + h - asc - desc if "bottom" in j else y)
    d.text((tx, ty), s, font=f, fill=argb(ts.get("colour", "ffc8c8c8")))
    im.alpha_composite(layer)


def render(port, out_dir, banks=False):
    D = os.path.join(STEVE, "schwung-ports", port)
    sk = glob.glob(os.path.join(D, "deploy", "Synths", "*", "Plugin Skins"))[0]
    t = json.load(open(os.path.join(sk, "TUI.json")))["pageData"]
    defs = {d["key"]: d["value"] for d in t["componentDefinitions"]["localComponentDefinitions"]}
    P = params_h(os.path.join(D, "build", "params.h"))
    norm = [p["def"] for p in P]
    shown_text = [display(p) for p in P]
    st = os.path.join(D, "build", "state.json")
    if os.path.exists(st):   # the engine's own values and text after an insert (dump_state.sh)
        for i, (v, s) in json.load(open(st)).items():
            norm[int(i)], shown_text[int(i)] = v, s
    xywh = lambda b: [int(float(v)) for v in b["bounds"].split()]
    imgs = {}

    def img(name):
        if name not in imgs:
            imgs[name] = Image.open(os.path.join(sk, name)).convert("RGBA")
        return imgs[name]

    def pnum(c, handle):
        for e in c["handle remapping"]["map"]:
            if e["key"] == handle:
                m = re.match(r"Parameter (\d+)$", e["value"])
                return int(m.group(1)) if m else None
        return None

    def shown(b):
        for h in b.get("additionalInvalidatingHandles", []):
            m = re.match(r"IndexedEnabling/(\d+)/(\d+)/Parameter (\d+)$", h)
            if m:
                i, n, p = (int(g) for g in m.groups())
                if int(round(norm[p] * (n - 1))) != i if n > 1 else norm[p] < 0.5:
                    return False
        return b.get("whenVisible", "Always") in ("Always", "WhenNotFocussed")

    os.makedirs(out_dir, exist_ok=True)
    outs, drawn = [], set()
    for tab in t["tabs"]:
        page = tab["componentName"].split("|")[0]
        if page in drawn and not banks:
            continue   # a second Q-Link bank of a page already drawn: same screen (unless it has banks= controls)
        drawn.add(page)
        im = Image.new("RGBA", (W, H), (0, 0, 0, 255))
        for c in defs[tab["componentName"]]["componentsData"]:
            if not shown(c["bounds"]):
                continue
            cd = c["componentData"]
            x, y, w, h = xywh(c["bounds"])
            subs = [c] if cd["type"] not in defs else defs[cd["type"]]["componentsData"]
            for s in subs:
                sd = s["componentData"]
                if not shown(s["bounds"]):
                    continue
                sx, sy, sw, sh = (0, 0, w, h) if s is c else xywh(s["bounds"])
                ox, oy = (x, y) if s is c else (x + sx, y + sy)
                data = sd.get("data", {})
                p = pnum(c, data.get("handleName", "Data")) if s is not c else None
                if sd["type"] == "Image":
                    im.alpha_composite(img(data["image"]), (ox, oy))
                elif sd["type"] == "Knob":
                    st = img(data["filmStrip"])
                    fh = sh if sh > 0 else st.size[0]
                    frames = max(1, st.size[1] // fh)
                    v = norm[p] if p is not None else 0
                    if data.get("invert"):
                        v = 1 - v
                    k = int(round(v * (frames - 1)))
                    im.alpha_composite(st.crop((0, k * fh, st.size[0], (k + 1) * fh)), (ox, oy))
                elif sd["type"] == "Button":
                    nb = int(data.get("numButtonsInGroup", 1))
                    v = norm[p] if p is not None else 0
                    on = int(round(v * (nb - 1))) == int(data.get("buttonId", 0)) if nb > 1 else v >= 0.5
                    name = data.get("onImage") if on else data.get("offImage")
                    if name:
                        im.alpha_composite(img(name), (ox, oy))
                elif sd["type"] == "Label":
                    ts = data["textStyle"]
                    if data.get("type") == "Name":
                        text(im, (ox, oy, sw, sh), cd["name"].strip(), ts)
                    elif data.get("type") == "Value" and p is not None:
                        text(im, (ox, oy, sw, sh), shown_text[p], ts)
        pages_done = list(dict.fromkeys(o[2] for o in outs))
        if page in pages_done:   # --banks: every further Q-Link sub-page, page_<n>_<sub>.png
            out = os.path.join(out_dir, "page_%d_%d.png" % (pages_done.index(page), sum(o[2] == page for o in outs)))
        else:
            out = os.path.join(out_dir, "page_%d.png" % len(pages_done))
        im.convert("RGB").save(out, optimize=True)
        outs.append((out, tab["tabName"], page))
    return [(o, n) for o, n, _ in outs]


if __name__ == "__main__":
    for o, name in render(sys.argv[1], sys.argv[2], "--banks" in sys.argv[3:]):
        print(o, name)
