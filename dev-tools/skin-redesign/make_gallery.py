"""Write steve/schwung-ports/GALLERY.html: every port's page previews on one page (relative image paths, so it
works straight from the folder). Rerun after rebuilding a port:  python3 steve/tools/skin-redesign/make_gallery.py"""
import glob, html, json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(os.path.dirname(os.path.dirname(HERE)), "schwung-ports")
GROUPS = [("Synths (Stitch skins 2026-10-03: built, not deployed yet)",
           ["303", "aphex", "chordism", "denis", "fizzik", "hank", "helm", "hera", "hush1", "libpo32", "monksynth",
            "monovoice", "moog", "mrdrums", "mrhyde", "noisemaker", "nusaw", "obxd", "tablor", "wurl"]),
          ("MIDI sequencers: play other tracks through their own MIDI port",
           ["eucalypso", "groovebank", "mazelite", "midiplayer", "pixelwalkers", "superarp"]),
          ("Mutable Instruments: synths and sequencers", ["braids", "elements", "grids", "marbles", "plaits", "rings"]),
          ("Audio effects", ["ringsfx", "verglas", "warps"]),
          ("VCV Rack modules (through steve/tools/rack)", ["rampage"])]
ORDER = [p for _, ps in GROUPS for p in ps]

cards = {}
for p in ORDER:
    d = os.path.join(R, p)
    if not os.path.isdir(os.path.join(d, "preview")):
        continue
    v = json.load(open(os.path.join(d, "vst.json")))
    lay = open(os.path.join(d, "layout.conf")).read()
    tabs = re.findall(r"^\[tab (.+)\]$", lay, re.M)
    readme = open(os.path.join(d, "README.md")).read() if os.path.exists(os.path.join(d, "README.md")) else ""
    what = readme.split("\n\n")[1].split(".")[0] if readme.count("\n\n") > 1 else ""
    shots = []
    for i, t in enumerate(tabs):
        for suffix, note in (("", ""), ("_open", " (pop-ups open)")):
            f = "preview/page_%d%s.png" % (i, suffix)
            if os.path.exists(os.path.join(d, f)):
                shots.append('<figure><img loading="lazy" src="%s/%s" alt="%s %s"><figcaption>%s%s</figcaption></figure>'
                             % (p, f, html.escape(v["name"]), html.escape(t), html.escape(t), note))
    cards[p] = ('<section id="%s"><h2>%s <span>%s · %d pages · <a href="%s/README.md">README</a></span></h2>%s</section>'
                 % (p, html.escape(v["name"]), html.escape(what), len(tabs), p, "".join(shots)))

nav = "<br>".join("<b>%s:</b> " % html.escape(g.split(" (")[0].split(":")[0]) +
                  " ".join('<a href="#%s">%s</a>' % (p, p) for p in ps if p in cards) for g, ps in GROUPS)
body = "".join('<h3 class="group">%s</h3>' % html.escape(g) + "".join(cards[p] for p in ps if p in cards)
               for g, ps in GROUPS)
page = """<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MPC plugin skins</title><style>
:root{color-scheme:dark}body{margin:0;background:#101114;color:#e9e9ec;font:15px/1.45 -apple-system,system-ui,sans-serif}
header{position:sticky;top:0;background:#101114ee;backdrop-filter:blur(6px);padding:14px 20px;border-bottom:1px solid #2a2c31;z-index:1}
h1{margin:0 0 6px;font-size:20px}header p{margin:0 0 8px;color:#a4a7ad;font-size:13px}nav{font-size:13px;line-height:1.9}nav a{color:#8fc1ff;margin-right:12px;text-decoration:none}.group{margin:0;padding:16px 20px 0;font-size:14px;letter-spacing:.08em;text-transform:uppercase;color:#d8b46a}
section{padding:18px 20px;border-bottom:1px solid #22242a}h2{margin:0 0 12px;font-size:18px}h2 span{color:#a4a7ad;font-weight:400;font-size:13px}
h2 a{color:#8fc1ff}.grid{display:grid}figure{margin:0 0 14px}img{width:100%;max-width:1280px;height:auto;border-radius:6px;border:1px solid #2a2c31;display:block}
figcaption{color:#a4a7ad;font-size:13px;margin-top:4px}
</style></head><body><header><h1>MPC plugin skins — 2026-10-01 redesign</h1>
<p>Offline previews of each plugin's screen (1280 × 628, the MPC's plugin area). Knob/value boxes and the green/blue
outlines are preview markers; on the MPC the names, values and patch names are drawn live in those boxes.</p>
<nav>""" + nav + "</nav></header>" + body + "</body></html>\n"
open(os.path.join(R, "GALLERY.html"), "w").write(page)
print("wrote", os.path.join(R, "GALLERY.html"), "with", len(cards), "plugins")
