#!/usr/bin/env python3
"""Percolator's skin artwork, from the spec mpc/gen.py writes (build/skin.json). Runs in the mpc-vst-html-art
container (Playwright's Chromium and Pillow), from the repo root:

    docker run --rm -v "$PWD:$PWD" -w "$PWD" mpc-vst-html-art python3 originals/percolator/mpc/skin.py \\
        originals/percolator/build/skin.json

Writes into images/skin/: each page's background (bg_*.png, 1280 x 628: panels, titles, the displays' glass, the
Q-Link row plates), a knob filmstrip per colour (knob_*.png, 64 frames: cap, pointer and a glowing value arc), the
switch segments (seg_*.png, unlit and lit per colour) and the envelope displays (env_*.png, mpc/displays.py)."""
import json
import math
import os
import sys

from playwright.sync_api import sync_playwright

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import displays  # noqa: E402

spec = json.load(open(sys.argv[1]))
OUT = spec["out"]
os.makedirs(OUT, exist_ok=True)
FONTS = spec["fonts"]
KF = 64   # knob frames
DPR = 2   # switch segments at twice the size MPC shows, for clean edges; knobs at 1x (MPC draws them at that size, and
          # 64 frames at 2x made 1.5 MB strips)

FONT_CSS = "".join('@font-face{font-family:"Titillium Web";font-weight:%d;src:url("file://%s/TitilliumWeb-%s.ttf")}'
                   % (w, FONTS, n) for w, n in ((400, "Regular"), (600, "SemiBold"), (700, "Bold")))


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
         '<defs><radialGradient id="cap" cx="38%" cy="30%" r="78%"><stop offset="0" stop-color="#4b4a5c"/>'
         '<stop offset="0.5" stop-color="#22212c"/><stop offset="1" stop-color="#0c0c12"/></radialGradient>'
         '<filter id="g" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="1.8"/></filter>'
         '</defs>',
         '<path d="%s" fill="none" stroke="#2a2935" stroke-width="3.6" stroke-linecap="round"/>' % arc(c, c, r + 0.4, -135, 135)]
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


def seg_html(w, h, col):
    if col:
        st = ("background:linear-gradient(180deg,%s 0%%,%s 100%%);border:1px solid %s;"
              "box-shadow:inset 0 0 9px rgba(255,255,255,.35)" % (rgba(col, 0.92), col, rgba(col, 1)))
    else:
        st = "background:linear-gradient(180deg,#272633 0%,#1b1a24 100%);border:1px solid #3a3948"
    return '<div id="s" style="box-sizing:border-box;width:%dpx;height:%dpx;border-radius:6px;%s"></div>' % (w, h, st)


with sync_playwright() as pw:
    br = pw.chromium.launch()
    pg = br.new_page(viewport={"width": 1280, "height": 628}, device_scale_factor=1)
    for bg in spec["bgs"]:
        pg.set_content("<!doctype html><html><head><style>%s%s</style></head><body>%s</body></html>"
                       % (FONT_CSS, spec["css"], bg["html"]))
        pg.evaluate("document.fonts.ready")
        pg.screenshot(path=os.path.join(OUT, bg["name"] + ".png"))
        print(bg["name"])
    k1 = br.new_page(viewport={"width": 400, "height": 400}, device_scale_factor=1)
    kp = br.new_page(viewport={"width": 400, "height": 400}, device_scale_factor=DPR)
    from PIL import Image
    import io
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
    for s in spec["segs"]:
        kp.set_content('<html><body style="margin:0;background:transparent">%s</body></html>'
                       % seg_html(s["w"], s["h"], s.get("color")))
        kp.locator("#s").screenshot(path=os.path.join(OUT, s["name"] + ".png"), omit_background=True)
        print(s["name"])
    br.close()

for ds in spec["displays"]:
    displays.strip(ds, spec.get("display_frames", 32)).save(os.path.join(OUT, ds["name"] + ".png"), optimize=True)
    print(ds["name"])
