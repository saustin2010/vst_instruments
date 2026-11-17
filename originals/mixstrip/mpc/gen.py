#!/usr/bin/env python3
"""[UTIL] Mix Strip: writes params.json, layout.conf and build/skin.json (the artwork's spec).

    python3 mpc/gen.py
    docker run --rm -v "$REPO:$REPO" -w "$PWD" mpc-vst-html-art python3 mpc/skin.py build/skin.json

(from the plugin folder, with REPO the repo root; Mix Bus's mixbus/mpc/gen.py uses the same mpc/skin.py). The
parameter order is the VST index MPC saves projects and Q-Links by: append new parameters at the end.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(ROOT))   # originals/mixstrip -> the repo
COL = "#3fd3c3"    # the family's colour (Mix Strip and Mix Bus)
MUTE = "#ff5a4f"   # a lit MUTE
CUE = "#3cbcff"    # a lit CUE
KILL = "#ffb02e"   # a lit SEND KILL


def params():
    db = dict(dynamic_display=True)
    return [
        dict(key="status", name="STATUS", min=0, max=0, display="string", type="readout"),   # index 0: MPC nudges it
        dict(key="track", name="TRACK", min=1, max=16, default=1, display="int"),
        dict(key="level", name="LEVEL", min=0, max=100, default=75, **db),
        dict(key="mute", name="MUTE", options=["OFF", "ON"], default="OFF"),
        dict(key="cue", name="CUE", options=["OFF", "ON"], default="OFF"),
        dict(key="filter", name="FILTER", min=-100, max=100, default=0, **db),
        dict(key="send_a", name="SEND A", min=0, max=100, default=0, **db),
        dict(key="send_b", name="SEND B", min=0, max=100, default=0, **db),
        dict(key="midi_ch", name="MIDI CH", min=1, max=15, default=1, display="int"),
        dict(key="knobs", name="KNOBS", options=["RELATIVE", "ABSOLUTE"], default="RELATIVE"),
    ] + [dict(key="cc_" + k, name="CC " + n, min=0, max=0, display="string", type="readout")
         for k, n in (("send_a", "SEND A"), ("send_b", "SEND B"), ("filter", "FILTER"), ("level", "LEVEL"),
                      ("mute", "MUTE"), ("cue", "CUE"))] + [
        dict(key="track_prev", name="TRACK DOWN", momentary=True, step_of="track", step_delta=-1),   # the steppers' arrows
        dict(key="track_next", name="TRACK UP", momentary=True, step_of="track", step_delta=1),
        dict(key="midi_ch_prev", name="MIDI CH DOWN", momentary=True, step_of="midi_ch", step_delta=-1),
        dict(key="midi_ch_next", name="MIDI CH UP", momentary=True, step_of="midi_ch", step_delta=1),
        # 2026-10-25: the third knob can be a send kill instead of the filter, per strip
        dict(key="knob3", name="KNOB 3", options=["FILTER", "SEND KILL"], default="FILTER"),
        dict(key="send_kill", name="SEND KILL", options=["OFF", "ON"], default="OFF")]


THEME = """theme_bg=090c0d
theme_panel=111617
theme_line=2b3537
theme_ink=eefaf8
theme_ink_dim=a3b8b5
theme_ink_faint=627573
theme_accent=3fd3c3
theme_accent_hi=a6f0e7
theme_box=0b0f10
theme_lcd=060909
theme_seg_active=3fd3c3
theme_seg_active_tx=071010
theme_seg_inactive=1d2526
theme_btn_bg=243032
theme_btn_text=eefaf8
theme_knob_face=1f2829
theme_knob_ring=33403f
theme_knob_dot=3fd3c3
art_css=mixstrip.css
qlink_bounds=column
qlink_box=slot
label_scale=1
focus_ring=1
"""

CSS = """*{box-sizing:border-box}
body{margin:0;width:1280px;height:628px;overflow:hidden;position:relative;font-family:"Titillium Web",sans-serif;
background:radial-gradient(ellipse 85% 75% at 50% 35%,#121a1b 0%,#0a0e0f 55%,#050707 100%)}
body:after{content:"";position:absolute;inset:0;background:repeating-linear-gradient(0deg,rgba(255,255,255,.014) 0 1px,
transparent 1px 3px)}
.a{position:absolute}
.t{position:absolute;white-space:nowrap;line-height:1}
"""

SKIN_CSS = """/* Mix Strip and Mix Bus (loaded after tools/html_art/default.css): a dark green-black panel, teal lights. */
:root { --title-size: 18px; --label-size: 14px; --seg-size: 14px; --sheen: 0.05; --radius: 4px; }
.frame-border { stroke: #2b3537; }
.frame-title { fill: #eefaf8; letter-spacing: 0.14em; }
.text { fill: #a3b8b5; letter-spacing: 0.1em; }
"""


def rgba(c, a):
    c = c.lstrip("#")
    return "rgba(%d,%d,%d,%.3f)" % (int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16), a)


def h_panel(x, y, w, h, c, title, sub=""):   # layout y in, artwork y out
    y -= 86
    o = ('<div class="a" style="left:%dpx;top:%dpx;width:%dpx;height:%dpx;border-radius:16px;'
         'background:linear-gradient(180deg,#151c1d 0%%,#0d1213 100%%);border:1.5px solid %s;'
         'box-shadow:0 0 24px %s,inset 0 1px 0 rgba(255,255,255,.06)"></div>' % (x, y, w, h, rgba(c, .55), rgba(c, .2)))
    o += ('<div class="a" style="left:%dpx;top:%dpx;width:%dpx;height:3px;border-radius:2px;'
          'background:linear-gradient(90deg,%s,%s 60%%,transparent);box-shadow:0 0 10px %s"></div>'
          % (x + 16, y + 1, w - 32, c, rgba(c, .35), rgba(c, .8)))
    if title:
        o += ('<div class="t" style="left:%dpx;top:%dpx;display:flex;align-items:baseline;gap:14px">'
              '<span style="font-size:22px;font-weight:700;letter-spacing:.22em;color:%s;text-shadow:0 0 12px %s">%s</span>'
              '<span style="font-size:13px;font-weight:600;letter-spacing:.16em;color:#8aa09d">%s</span></div>'
              % (x + 20, y + 14, c, rgba(c, .7), title, sub))
    return o


def h_box(x, y, w, h, c):   # a sunken box (a section of the panel)
    return ('<div class="a" style="left:%dpx;top:%dpx;width:%dpx;height:%dpx;border-radius:12px;background:#0a0e0f;'
            'border:1px solid %s;box-shadow:inset 0 2px 14px #000"></div>' % (x, y - 86, w, h, rgba(c, .28)))


def h_ctext(cx, y, s, c="#a3b8b5", size=13, w=600, weight=600):
    return ('<div class="t" style="left:%dpx;top:%dpx;width:%dpx;text-align:center;font-size:%dpx;font-weight:%d;'
            'letter-spacing:.14em;color:%s">%s</div>' % (cx - w // 2, y - 86, w, size, weight, c, s))


def h_glass(cx, cy, w, h, c):   # the dark glass behind a readout
    return ('<div class="a" style="left:%dpx;top:%dpx;width:%dpx;height:%dpx;border-radius:8px;background:#030505;'
            'border:1px solid %s;box-shadow:inset 0 2px 10px #000,0 0 10px %s"></div>'
            % (cx - w // 2, cy - h // 2 - 86, w, h, rgba(c, .35), rgba(c, .1)))


class Art:
    """The artwork spec mpc/skin.py draws: backgrounds, knob filmstrips, switch segments, lit buttons."""

    def __init__(self, root):
        self.root = root
        self.spec = {"out": os.path.join(root, "images", "skin"), "css": CSS,
                     "fonts": os.path.join(REPO, "framework", "mpc-vst-plugins", "tools", "html_art", "fonts"),
                     "bgs": [], "knobs": [], "segs": [], "buttons": []}

    def knob(self, r, col):
        name = "knob_%d_%s" % (r, col.lstrip("#"))
        if not any(k["name"] == name for k in self.spec["knobs"]):
            self.spec["knobs"].append({"name": name, "r": r, "color": col})
        return "images/skin/%s.png" % name

    def seg(self, lit, w, h, col=COL):
        name = "seg_%s_%dx%d" % ("on" if lit else "off", w, h)
        if not any(s["name"] == name for s in self.spec["segs"]):
            self.spec["segs"].append({"name": name, "w": w, "h": h, "color": col if lit else None})
        return "images/skin/%s.png" % name

    def button(self, label, lit, w, h, col, size=None):
        name = "btn_%s_%s" % (label.lower().replace(" ", "_"), "on" if lit else "off")
        if not any(b["name"] == name for b in self.spec["buttons"]):
            self.spec["buttons"].append({"name": name, "w": w, "h": h, "label": label, "color": col if lit else None,
                                         "size": size})
        return "images/skin/%s.png" % name

    def write(self):
        os.makedirs(os.path.join(self.root, "build"), exist_ok=True)
        json.dump(self.spec, open(os.path.join(self.root, "build", "skin.json"), "w"), indent=1)


def layout():
    art = Art(ROOT)
    L = ["# [UTIL] Mix Strip, generated by mpc/gen.py (do not edit by hand: change gen.py and run it again).",
         THEME, "[tab STRIP]", "art file=images/skin/bg_strip.png x=0 y=86 w=1280 h=628"]
    html = h_panel(8, 94, 1264, 612, COL, "MIX STRIP", "THIS TRACK'S MIXER STRIP, ON A MIDI CONTROLLER")

    # left: which controller column this strip is
    html += h_box(24, 146, 360, 420, COL)
    html += h_ctext(204, 160, "TRACK")
    L.append('stepper cx=204 cy=206 w=300 h=64 label="" key=track label_align=center')
    html += h_ctext(204, 256, "MIDI CH")
    L.append('stepper cx=204 cy=296 w=300 h=56 label="" key=midi_ch label_align=center')
    html += h_ctext(204, 344, "KNOBS")
    L.append('enum_h cx=204 cy=384 label="" key=knobs sw=150 sh=48 img=%s img_on=%s'
             % (art.seg(0, 150, 48), art.seg(1, 150, 48)))
    html += h_ctext(204, 428, "KNOB 3")
    L.append('enum_h cx=204 cy=468 label="" key=knob3 sw=150 sh=48 img=%s img_on=%s'
             % (art.seg(0, 150, 48), art.seg(1, 150, 48)))
    html += h_ctext(204, 522, "TRACK 1-8: COLUMNS 1-8 ON MIDI CH", "#627573", 11, 340)
    html += h_ctext(204, 542, "TRACK 9-16: THE SAME ON MIDI CH + 1", "#627573", 11, 340)

    # right: the strip, as the controller's column has it
    html += h_box(400, 146, 856, 420, COL)
    for cx, key in ((520, "send_a"), (680, "send_b"), (840, "filter")):
        L.append('knob cx=%d cy=262 r=46 label="%s" key=%s strip=%s frames=64%s'
                 % (cx, key.upper().replace("_", " "), key, art.knob(46, COL), " when=knob3:0" if key == "filter" else ""))
        html += h_glass(cx, 392, 112, 30, COL)
        L.append('readout cx=%d cy=392 w=104 h=26 vs=18 ink=dim box=0 key=cc_%s' % (cx, key))
    L.append('toggle cx=840 cy=262 w=150 h=84 ns=0 label="" key=send_kill img=%s img_on=%s when=knob3:1'   # KNOB 3 = SEND KILL
             % (art.button("SEND KILL", 0, 150, 84, KILL, 19), art.button("SEND KILL", 1, 150, 84, KILL, 19)))
    L.append('slider_v cx=1010 cy=300 w=56 h=250 label="LEVEL" key=level')
    html += h_glass(1010, 518, 112, 30, COL)
    L.append('readout cx=1010 cy=518 w=104 h=26 vs=18 ink=dim box=0 key=cc_level')
    for cy, key, col in ((232, "mute", MUTE), (402, "cue", CUE)):
        L.append('toggle cx=1170 cy=%d w=140 h=84 ns=0 label="" key=%s img=%s img_on=%s'
                 % (cy, key, art.button(key.upper(), 0, 140, 84, col), art.button(key.upper(), 1, 140, 84, col)))
        html += h_glass(1170, cy + 66, 112, 30, COL)
        L.append('readout cx=1170 cy=%d w=104 h=26 vs=18 ink=dim box=0 key=cc_%s' % (cy + 66, key))

    # what's going on, then how it fits together
    html += h_glass(640, 612, 1220, 52, COL)
    L.append("readout cx=640 cy=612 w=1200 h=46 vs=26 ink=accent box=0 key=status")
    for i, line in enumerate((
            "LAST IN THE TRACK'S INSERTS. FILTER, THEN CUE (TO A MIX BUS ON CUE), LEVEL AND MUTE, THE SENDS. KNOB 3 = SEND KILL: LEFT CUTS THE SENDS",
            "THE CONTROLLER IS READ DIRECTLY: NO MIDI TRACK NEEDED. KEEP TRACKS OFF ITS CHANNELS AND OFF \"ALL CHANNELS\" FOR IT")):
        html += h_ctext(640, 654 + 22 * i, line, "#8aa09d" if i == 0 else "#627573", 11, 1240)
    art.spec["bgs"].append({"name": "bg_strip", "html": html})
    L.append('qlinks "STRIP" = level,send_a,send_b,filter,mute,cue,track,midi_ch,knobs,knob3,send_kill')
    L.append("")
    open(os.path.join(ROOT, "layout.conf"), "w").write("\n".join(L) + "\n")
    open(os.path.join(ROOT, "mixstrip.css"), "w").write(SKIN_CSS)
    art.write()


if __name__ == "__main__":
    P = params()
    json.dump({"name": "Mix Strip", "params": P}, open(os.path.join(ROOT, "params.json"), "w"), indent=1)
    layout()
    print("params.json: %d parameters; layout.conf; build/skin.json (then mpc/skin.py)" % len(P))
