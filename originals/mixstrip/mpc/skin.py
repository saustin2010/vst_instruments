#!/usr/bin/env python3
"""Mix Strip's and Mix Bus's skin artwork, from the spec mpc/gen.py (or mixbus/mpc/gen.py) writes, build/skin.json.
Runs in the mpc-vst-html-art container (Playwright's Chromium and Pillow), from the plugin's folder:

    docker run --rm -v "$REPO:$REPO" -w "$PWD" mpc-vst-html-art python3 mpc/skin.py build/skin.json

Writes into images/skin/: each page's background (bg_*.png, 1280 x 628: panels, titles, the readouts' glass), a knob
filmstrip per size and colour (knob_*.png, 64 frames: cap, pointer and a glowing value arc), switch segments
(seg_*.png, unlit and lit) and the lit buttons (btn_*.png, MUTE and CUE, off and on). Percolator's mpc/skin.py,
cut down to what these two use."""
import base64
import io
import json
import math
import os
import sys

from playwright.sync_api import sync_playwright
from PIL import Image

spec = json.load(open(sys.argv[1]))
OUT = spec["out"]
os.makedirs(OUT, exist_ok=True)
FONTS = spec["fonts"]
KF = 64   # knob frames
DPR = 2   # segments and buttons at twice the size MPC shows, for clean edges; knobs at 1x

# the fonts inline: a page set from a string can't load file:// URLs (they fell back to the browser's own)
FONT_CSS = "".join('@font-face{font-family:"Titillium Web";font-weight:%d;src:url(data:font/ttf;base64,%s)}'
                   % (w, base64.b64encode(open(os.path.join(FONTS, "TitilliumWeb-%s.ttf" % n), "rb").read()).decode())
                   for w, n in ((400, "Regular"), (600, "SemiBold"), (700, "Bold")))


def rgba(c, a):
    c = c.lstrip("#")
    return "rgba(%d,%d,%d,%.3f)" % (int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16), a)


def arc(cx, cy, R, a0, a1):
    p = lambda a: (cx + R * math.sin(math.radians(a)), cy - R * math.cos(math.radians(a)))
    (x0, y0), (x1, y1) = p(a0), p(a1)
    return "M%.2f %.2f A%.2f %.2f 0 %d 1 %.2f %.2f" % (x0, y0, R, R, 1 if a1 - a0 > 180 else 0, x1, y1)


def knob_svg(r, col, t):
    B = 2 * r + 8
    c = B / 2
    a = -135 + 270 * t
    rc = r - 4
    o = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (B, B, B, B),
         '<defs><radialGradient id="cap" cx="38%" cy="30%" r="78%"><stop offset="0" stop-color="#46524f"/>'
         '<stop offset="0.5" stop-color="#1e2625"/><stop offset="1" stop-color="#0a0e0e"/></radialGradient>'
         '<filter id="g" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="1.8"/></filter>'
         '</defs>',
         '<path d="%s" fill="none" stroke="#263130" stroke-width="3.6" stroke-linecap="round"/>' % arc(c, c, r + 0.4, -135, 135)]
    if t > 0.004:
        d = arc(c, c, r + 0.4, -135, a)
        o.append('<path d="%s" fill="none" stroke="%s" stroke-width="5" stroke-linecap="round" filter="url(#g)" '
                 'opacity="0.9"/>' % (d, col))
        o.append('<path d="%s" fill="none" stroke="%s" stroke-width="3.4" stroke-linecap="round"/>' % (d, col))
    o.append('<circle cx="%g" cy="%g" r="%g" fill="url(#cap)" stroke="#000" stroke-opacity="0.7"/>' % (c, c, rc))
    o.append('<circle cx="%g" cy="%g" r="%g" fill="none" stroke="#fff" stroke-opacity="0.07"/>' % (c, c, rc - 1.5))
    p = lambda k: (c + k * rc * math.sin(math.radians(a)), c - k * rc * math.cos(math.radians(a)))
    (x0, y0), (x1, y1) = p(0.2), p(0.8)
    o.append('<line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f" stroke="%s" stroke-width="5" stroke-linecap="round" '
             'filter="url(#g)" opacity="0.7"/>' % (x0, y0, x1, y1, col))
    o.append('<line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f" stroke="#fff" stroke-width="3" stroke-linecap="round"/>'
             % (x0, y0, x1, y1))
    return "".join(o) + "</svg>"


def seg_html(w, h, col, label="", size=None):
    if col:
        st = ("background:linear-gradient(180deg,%s 0%%,%s 100%%);border:1px solid %s;color:#06100f;"
              "box-shadow:inset 0 0 12px rgba(255,255,255,.35),0 0 14px %s" % (rgba(col, 0.92), col, col, rgba(col, .5)))
    else:
        st = ("background:linear-gradient(180deg,#222b2c 0%,#161d1e 100%);border:1px solid #33403f;color:#8aa09d;"
              "box-shadow:inset 0 1px 0 rgba(255,255,255,.05)")
    text = ("display:flex;align-items:center;justify-content:center;font-family:'Titillium Web';font-weight:700;"
            'font-size:%dpx;letter-spacing:.18em' % (size or max(14, h // 3))) if label else ""
    return ('<div id="s" style="box-sizing:border-box;width:%dpx;height:%dpx;border-radius:8px;%s;%s">%s</div>'
            % (w, h, st, text, label))


with sync_playwright() as pw:
    br = pw.chromium.launch()
    pg = br.new_page(viewport={"width": 1280, "height": 628}, device_scale_factor=1)
    for bg in spec["bgs"]:
        pg.set_content("<!doctype html><html><head><style>%s%s</style></head><body>%s</body></html>"
                       % (FONT_CSS, spec["css"], bg["html"]))
        pg.evaluate("async () => { await document.fonts.load('600 13px \"Titillium Web\"'); await document.fonts.ready; }")
        pg.screenshot(path=os.path.join(OUT, bg["name"] + ".png"))
        print(bg["name"])
    k1 = br.new_page(viewport={"width": 400, "height": 400}, device_scale_factor=1)
    kp = br.new_page(viewport={"width": 400, "height": 400}, device_scale_factor=DPR)
    for k in spec["knobs"]:
        r, B = k["r"], 2 * k["r"] + 8
        im = Image.new("RGBA", (B, B * KF), (0, 0, 0, 0))
        k1.set_content('<html><body style="margin:0;background:transparent"><div id="k"></div></body></html>')
        for i in range(KF):
            k1.evaluate("s => document.getElementById('k').innerHTML = s", knob_svg(r, k["color"], i / (KF - 1)))
            png = k1.locator("#k svg").screenshot(omit_background=True)
            im.paste(Image.open(io.BytesIO(png)).convert("RGBA").resize((B, B)), (0, i * B))
        im.save(os.path.join(OUT, k["name"] + ".png"), optimize=True)
        print(k["name"], "%d frames of %d px" % (KF, B))
    for s in spec["segs"] + spec.get("buttons", []):
        kp.set_content('<html><head><style>%s</style></head><body style="margin:0;background:transparent">%s</body></html>'
                       % (FONT_CSS, seg_html(s["w"], s["h"], s.get("color"), s.get("label", ""), s.get("size"))))
        kp.evaluate("async () => { await document.fonts.load('700 20px \"Titillium Web\"'); await document.fonts.ready; }")
        kp.locator("#s").screenshot(path=os.path.join(OUT, s["name"] + ".png"), omit_background=True)
        print(s["name"])
    br.close()
