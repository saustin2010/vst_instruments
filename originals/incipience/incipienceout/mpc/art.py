#!/usr/bin/env python3
"""[FX] Incipience OUT's artwork, from the spec mpc/gen.py writes (build/skin.json). Runs in the mpc-vst-html-art
container (Playwright's Chromium), from the plugin folder:

    docker run --rm -v "$REPO:$REPO" -w "$PWD" mpc-vst-html-art python3 mpc/art.py build/skin.json

Writes into images/skin/: the page's background (bg_out.png, 1280 x 628) and the switch segments (seg_*.png, unlit
and lit, drawn at twice the size MPC shows them). Incipience's type (Inter) comes from Google Fonts, as its Stitch
design's does."""
import json
import os
import sys

from playwright.sync_api import sync_playwright

spec = json.load(open(sys.argv[1]))
OUT = spec["out"]
os.makedirs(OUT, exist_ok=True)
FONTS = ('<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700;800;900&display=swap" '
         'rel="stylesheet">')


def seg_html(w, h, lit):   # the design's buttons: black when on, the panel's grey when off
    st = ("background:linear-gradient(180deg,#2a2a2a 0%,#141414 100%);border-bottom:2px solid #000"
          if lit else "background:linear-gradient(180deg,#e4e1d8 0%,#d6d2c7 100%);border:1px solid #bdb8ab;"
          "border-bottom:2px solid #a9a397")
    return '<div id="s" style="box-sizing:border-box;width:%dpx;height:%dpx;border-radius:4px;%s"></div>' % (w, h, st)


with sync_playwright() as pw:
    br = pw.chromium.launch()
    pg = br.new_page(viewport={"width": 1280, "height": 628}, device_scale_factor=1)
    for bg in spec["bgs"]:
        pg.set_content("<!doctype html><html><head>%s<style>%s</style></head><body>%s</body></html>"
                       % (FONTS, spec["css"], bg["html"]))
        pg.evaluate("document.fonts.ready")
        pg.wait_for_timeout(300)
        pg.screenshot(path=os.path.join(OUT, bg["name"] + ".png"))
        print(bg["name"])
    kp = br.new_page(viewport={"width": 400, "height": 400}, device_scale_factor=2)
    for s in spec["segs"]:
        kp.set_content('<html><body style="margin:0;background:transparent">%s</body></html>'
                       % seg_html(s["w"], s["h"], s["lit"]))
        kp.locator("#s").screenshot(path=os.path.join(OUT, s["name"] + ".png"), omit_background=True)
        print(s["name"])
    br.close()
