#!/usr/bin/env python3
"""Stevequencer's generated files: params.json, presets.json, layout.conf and the SVG artwork in images/.
    python3 mpc/gen.py            (from the plugin folder or anywhere; standard library only)
Then mpc/make_png.py (in the mpc-vst-html-art container) for the PNG pictures, then tools/build.sh stevequencer.

The parameter order is the plugin's VST parameter order: MPC saves projects and Q-Link assignments by index, so once
released, only ever append. The screen mirrors design/prototype.html: tabs = pages of 16 steps (and SETUP), each with
six Q-Link sub-pages (PITCH, LENGTH, ON/OFF, VELO, CHANCE, RATCHET); a cell's big value and its highlighted chip
follow the sub-page (banks=)."""
import json
import os

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(HERE, "images")
NSTEPS, PAGE, Y_OFF = 64, 16, 86

NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
note_name = lambda n: NOTE[n % 12] + str(n // 12 - 2)   # MPC naming: 60 = C3
PITCHES = list(range(36, 97))
RATES = ["1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1"]
SCALES = ["CHROMATIC", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN", "LOCRIAN", "HARM MINOR",
          "MEL MINOR", "PENTA MAJ", "PENTA MIN", "BLUES", "WHOLE TONE"]
SCALE_DEG = [list(range(12)), [0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 3, 5, 7, 9, 10], [0, 1, 3, 5, 7, 8, 10],
             [0, 2, 4, 6, 7, 9, 11], [0, 2, 4, 5, 7, 9, 10], [0, 1, 3, 5, 6, 8, 10], [0, 2, 3, 5, 7, 8, 11],
             [0, 2, 3, 5, 7, 9, 11], [0, 2, 4, 7, 9], [0, 3, 5, 7, 10], [0, 3, 5, 6, 7, 10], [0, 2, 4, 6, 8, 10]]
DIRS = ["FWD", "REV", "PEND", "RANDOM", "DRUNK"]
OFFON = ["OFF", "ON"]
pct = lambda vals: ["%d%%" % v for v in vals]
signed = lambda v: "%+d" % v if v else "0"

# ---- parameters ------------------------------------------------------------------------------------------------------
SETTINGS = [   # key, name, definition, hint (SETUP footer)
    ("rate", "RATE", {"options": RATES, "default": 2}, "STEP SIZE"),
    ("swing", "SWING", {"options": pct(range(50, 76)), "default": 0}, "50% = STRAIGHT"),
    ("direction", "DIRECTION", {"options": DIRS, "default": 0}, "PLAY ORDER"),
    ("gate", "GATE", {"options": pct(range(10, 201, 5)), "default": 18}, "SCALES EVERY LENGTH"),
    ("loop_start", "LOOP START", {"min": 1, "max": 64, "default": 1, "display": "int"}, "FIRST STEP"),
    ("loop_len", "LOOP LEN", {"min": 1, "max": 64, "default": 16, "display": "int"}, "STEPS IN THE LOOP"),
    ("root", "ROOT", {"options": NOTE, "default": 0}, "SCALE ROOT"),
    ("scale", "SCALE", {"options": SCALES, "default": 0}, "PITCHES SNAP TO IT"),
    ("transpose", "TRANSPOSE", {"options": [signed(v) for v in range(-24, 25)], "default": 24}, "SEMITONES"),
    ("key_transpose", "KEY TRANSP", {"options": OFFON, "default": 0}, "PADS TRANSPOSE (C3 = 0)"),
    ("channel", "MIDI CH", {"min": 1, "max": 16, "default": 1, "display": "int"}, "OUTPUT CHANNEL"),
    ("step_light", "STEP LIGHT", {"options": OFFON, "default": 1}, "PLAYHEAD ON SCREEN"),
    ("audition", "AUDITION", {"options": OFFON, "default": 1}, "HEAR EDITS WHILE STOPPED"),
]
TRIGGERS = [("rotate_left", "ROTATE L"), ("rotate_right", "ROTATE R"), ("random_all", "RANDOM ALL"), ("clear_all", "CLEAR ALL")]
PAGE_TOOLS = [("copy", "COPY"), ("paste", "PASTE"), ("clear", "CLEAR"), ("random", "RANDOM")]
# per-step attributes, in Q-Link sub-page order: (id, sub-page name, parameter name, definition, chip?)
ATTRS = [
    ("pitch", "PITCH", "PITCH", {"options": [note_name(n) for n in PITCHES], "default": 24}, True),
    ("length", "LENGTH", "LENGTH", {"options": pct(range(5, 401, 5)), "default": 9}, True),
    ("on", "ON/OFF", "ON", {"options": OFFON, "default": 1}, False),
    ("velo", "VELO", "VELO", {"min": 1, "max": 127, "default": 100, "display": "int"}, True),
    ("chance", "CHANCE", "CHANCE", {"options": pct(range(0, 101, 5)), "default": 20}, True),
    ("ratchet", "RATCHET", "RATCHET", {"options": ["x1", "x2", "x3", "x4"], "default": 0}, True),
]
TABS = ["1-16", "17-32", "33-48", "49-64"]


def params():
    out = [dict(key=k, name=n, **d) for k, n, d, _ in SETTINGS]
    out += [{"key": k, "name": n, "momentary": True, "min": 0, "max": 1} for k, n in TRIGGERS]
    out += [{"key": "p%d_%s" % (p, k), "name": "P%d %s" % (p, n), "momentary": True, "min": 0, "max": 1}
            for p in range(1, 5) for k, n in PAGE_TOOLS]
    # the playing step (0 = none), set by the engine; vst.json "live" makes the wrapper report it (the step light)
    out.append({"key": "play_step", "name": "PLAY STEP", "options": ["-"] + [str(i) for i in range(1, 65)], "default": 0})
    for a, _, name, d, _ in ATTRS:
        out += [dict(key="s%d_%s" % (s, a), name="S%d %s" % (s, name), **d) for s in range(1, NSTEPS + 1)]
    return out


# ---- presets (MPC's PRESET menu): every parameter set, so switching is deterministic -------------------------------
def quant(n, root, scale):
    deg = SCALE_DEG[scale]
    for d in range(12):
        for m in (n - d, n + d):
            if (m - root) % 12 in deg:
                return m
    return n


def preset(name, steps, **settings):
    """steps: up to 64 (pitch, on, length %, velo, chance %, ratchet); the rest are init steps."""
    base = {k: d["options"][d["default"]] if "options" in d else d["default"] for k, _, d, _ in SETTINGS}
    for k, v in settings.items():
        d = dict((s[0], s[2]) for s in SETTINGS)[k]
        base[k] = v if "options" not in d or isinstance(v, str) else d["options"][v]
    vals = dict(base)
    root, scale = NOTE.index(base["root"]), SCALES.index(base["scale"])
    for i in range(NSTEPS):
        p, on, ln, ve, ch, ra = steps[i] if i < len(steps) else (60, 1, 50, 100, 100, 1)
        p = quant(p, root, scale)
        vals.update({"s%d_pitch" % (i + 1): note_name(p), "s%d_length" % (i + 1): "%d%%" % ln,
                     "s%d_on" % (i + 1): OFFON[on], "s%d_velo" % (i + 1): ve, "s%d_chance" % (i + 1): "%d%%" % ch,
                     "s%d_ratchet" % (i + 1): "x%d" % ra})
    return {"name": name, "values": vals}


def presets():
    bass = [(48, 1, 60, 120, 100, 1), (48, 0, 50, 100, 100, 1), (60, 1, 40, 100, 100, 1), (48, 1, 30, 90, 100, 2),
            (51, 1, 50, 110, 100, 1), (48, 0, 50, 100, 100, 1), (55, 1, 50, 100, 100, 1), (58, 1, 40, 90, 50, 1),
            (48, 1, 80, 120, 100, 1), (48, 1, 25, 80, 100, 1), (60, 1, 40, 70, 100, 1), (55, 1, 50, 100, 100, 1),
            (53, 1, 30, 100, 100, 3), (48, 0, 50, 100, 100, 1), (51, 1, 50, 100, 75, 1), (55, 1, 150, 110, 100, 1)]
    acid = [(45, 1, 45, 120, 100, 1), (45, 1, 110, 90, 100, 1), (57, 1, 45, 110, 100, 1), (45, 0, 50, 100, 100, 1),
            (48, 1, 110, 90, 100, 1), (52, 1, 45, 120, 100, 1), (45, 1, 30, 80, 100, 2), (55, 1, 110, 100, 100, 1),
            (57, 1, 45, 120, 100, 1), (45, 0, 50, 100, 100, 1), (45, 1, 45, 100, 100, 1), (60, 1, 110, 90, 60, 1),
            (57, 1, 45, 120, 100, 1), (52, 1, 45, 90, 100, 1), (45, 1, 110, 100, 100, 1), (43, 1, 45, 110, 100, 1)]
    arp = [(60 + o, 1, 40, 90 + (i % 4 == 0) * 30, 100, 1) for i, o in enumerate([0, 4, 7, 12, 4, 7, 12, 16, 7, 12, 16, 19, 12, 16, 19, 24])]
    stabs = [(0, 0, 50, 100, 100, 1) if i % 4 != 2 else (63 if i % 8 == 2 else 67, 1, 35, 115, 100, 1) for i in range(16)]
    stabs = [(p or 60, on, ln, ve, ch, ra) for p, on, ln, ve, ch, ra in stabs]
    walk = [(n, 1, 70, 95, 85, 1) for n in [60, 62, 64, 67, 69, 72, 69, 67, 64, 62, 60, 57, 55, 57, 60, 64,
                                             67, 64, 62, 60, 62, 64, 67, 69, 72, 74, 76, 74, 72, 69, 67, 64]]
    return {"presets": [
        preset("Init", []),
        preset("Bass Line", bass, scale="MINOR", swing="56%"),
        preset("Acid Line", acid, scale="MINOR", root="A", swing="54%"),
        preset("Arp Climb", arp, scale="MAJOR", rate="1/16"),
        preset("Offbeat Stabs", stabs, scale="MINOR"),
        preset("Drunk Walk", walk, scale="PENTA MAJ", direction="DRUNK", loop_len=32, rate="1/8"),
    ]}


# ---- screen geometry (plugin area 1280 x 628; layout y = screen y + 86) ---------------------------------------------
CW, CH, GX, GY, X0, Y0 = 238, 140, 254, 152, 16, 16
HD, BIG_Y, BIG_H, CHIP_Y = 36, 36, 68, 108
STRIP = (1032, 16, 232, 596)
cell_xy = lambda r, c: (X0 + c * GX, Y0 + r * GY)
cell_step = lambda r, c: r * 4 + c   # ROWS order: steps 1-4 across the top row (the prototype's default)
THEME = {"bg": "101216", "panel": "191c21", "line": "2a2f37", "ink": "eef1f4", "ink_dim": "a3abb5", "ink_faint": "5f6873",
         "accent": "ffb020", "accent_hi": "ffd27a", "box": "191c21", "lcd": "0d0f12", "seg_active": "ffb020",
         "seg_active_tx": "1d1404", "seg_inactive": "22262c", "btn_bg": "23272d", "btn_text": "eef1f4"}


def svg(w, h, body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">%s</svg>\n'
            % (w, h, w, h, body))


def text(x, y, s, size, fill, weight=600, anchor="start", spacing=0.06):
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return ('<text x="%g" y="%g" font-family="Titillium Web" font-size="%d" font-weight="%d" fill="#%s" '
            'text-anchor="%s" dominant-baseline="central" letter-spacing="%gem">%s</text>' % (x, y, size, weight, fill, anchor, spacing, s))


def rrect(x, y, w, h, r, fill, stroke=None, sw=1):
    st = ' stroke="#%s" stroke-width="%g"' % (stroke, sw) if stroke else ""
    return '<rect x="%g" y="%g" width="%g" height="%g" rx="%g" fill="#%s"%s/>' % (x, y, w, h, r, fill, st)


def hr(x, y, w):
    return '<rect x="%d" y="%d" width="%d" height="1" fill="#%s"/>' % (x, y, w, THEME["line"])


def cell_box(x, y):
    return rrect(x + 0.5, y + 0.5, CW - 1, CH - 1, 10, THEME["panel"], THEME["line"])


def bg_steps(t):
    o = rrect(0, 0, 1280, 628, 0, THEME["bg"])
    for r in range(4):
        for c in range(4):
            o += cell_box(*cell_xy(r, c))
    x, y, w, h = STRIP
    o += rrect(x + 0.5, y + 0.5, w - 1, h - 1, 10, "141619", THEME["line"])
    o += text(x + 16, y + 26, "STEPS", 13, THEME["ink_faint"], 700, spacing=0.12)
    o += text(x + 16, y + 66, TABS[t].replace("-", "–"), 46, THEME["ink"], 700, spacing=0)
    o += text(x + 16, y + 112, "Q-LINKS EDIT", 13, THEME["ink_faint"], 700, spacing=0.12)
    o += hr(x + 16, y + 172, w - 32)
    for k, (lab, yy) in enumerate((("LOOP START", 196), ("LOOP LEN", 226), ("RATE", 256))):
        o += text(x + 16, y + yy, lab, 16, THEME["ink_dim"], 600, spacing=0.04)
    o += hr(x + 16, y + 280, w - 32)
    o += text(x + 16, y + 304, "NOW PLAYING", 13, THEME["ink_faint"], 700, spacing=0.12)
    o += text(x + 16, y + 412, "TAP A HEADER: ON/OFF", 12, THEME["ink_faint"], 600, spacing=0.06)
    o += text(x + 16, y + 432, "DRAG A VALUE TO CHANGE IT", 12, THEME["ink_faint"], 600, spacing=0.06)
    return svg(1280, 628, o)


SETUP_COLS = [   # one panel per Q-Link column, top to bottom = knobs 1-4
    ("TIMING", ["rate", "swing", "direction", "gate"]),
    ("LOOP", ["loop_start", "loop_len", ("ROTATE LOOP", [("rotate_left", "◀ ROTATE"), ("rotate_right", "ROTATE ▶")]),
              ("WHOLE LOOP", [("random_all", "RANDOM"), ("clear_all", "CLEAR")])]),
    ("PITCH", ["root", "scale", "transpose", "key_transpose"]),
    ("OUTPUT", ["channel", "step_light", "audition", ("MIDI OUT", None)]),
]
SETUP_Q = [it if isinstance(it, str) else "-" for _, items in SETUP_COLS for it in items]
SETTING = {k: (n, d, hint) for k, n, d, hint in SETTINGS}
is_toggle = lambda k: SETTING[k][1].get("options") == OFFON


def bg_setup():
    o = rrect(0, 0, 1280, 628, 0, THEME["bg"])
    for c, (tag, items) in enumerate(SETUP_COLS):
        for r, it in enumerate(items):
            x, y = cell_xy(r, c)
            o += cell_box(x, y)
            if isinstance(it, str):
                name, _, hint = SETTING[it]
                if not is_toggle(it):   # a toggle's header is its own picture (on/off)
                    o += rrect(x + 1, y + 1, CW - 2, HD - 1, 9, "1f2227")
                    o += text(x + 12, y + 18, name, 16, THEME["ink_dim"], 700, spacing=0.04)
                    o += text(x + CW - 12, y + 18, tag, 12, THEME["ink_faint"], 700, "end", 0.08)
                o += text(x + CW / 2, y + CHIP_Y + 13, hint, 13, THEME["ink_faint"], 400, "middle", 0.04)
            else:
                title, tools = it
                o += rrect(x + 1, y + 1, CW - 2, HD - 1, 9, "1f2227")
                o += text(x + 12, y + 18, title, 16, THEME["ink_dim"], 700, spacing=0.04)
                o += text(x + CW - 12, y + 18, tag, 12, THEME["ink_faint"], 700, "end", 0.08)
                if tools is None:
                    for k, line in enumerate(("Port: [SEQ] Stevequencer", "MIDI Out. Pick it as an", "instrument track's", "MIDI input.")):
                        o += text(x + 12, y + 54 + k * 20, line, 15, THEME["ink_dim"], 400, spacing=0.01)
    x, y, w, h = STRIP
    o += rrect(x + 0.5, y + 0.5, w - 1, h - 1, 10, "141619", THEME["line"])
    o += text(x + 16, y + 26, "PAGE", 13, THEME["ink_faint"], 700, spacing=0.12)
    o += text(x + 16, y + 66, "SETUP", 46, THEME["ink"], 700, spacing=0)
    help_ = ["Stevequencer makes no sound:", "its notes leave through its", "own MIDI port.", "",
             "1. Menu > Preferences > MIDI:", "   Track on for [SEQ]", "   Stevequencer MIDI Out.",
             "2. On the instrument's track:", "   MIDI Input Port = that port,", "   Monitor = In.",
             "3. Press play: it follows", "   MPC's tempo."]
    for k, line in enumerate(help_):
        o += text(x + 16, y + 110 + k * 22, line, 15, THEME["ink_dim"] if line[:1].isdigit() else "8d949c", 400, spacing=0.01)
    o += hr(x + 16, y + 390, w - 32)
    o += text(x + 16, y + 414, "NOW PLAYING", 13, THEME["ink_faint"], 700, spacing=0.12)
    return svg(1280, 628, o)


def playhead():   # the playing step's outline (drawn into the step light's per-step picture)
    return svg(CW + 8, CH + 8, '<rect x="2" y="2" width="%d" height="%d" rx="12" fill="none" stroke="#ffffff" stroke-width="3"/>'
               '<rect x="0.5" y="0.5" width="%d" height="%d" rx="13" fill="none" stroke="#ffffff" stroke-opacity="0.25" stroke-width="1"/>'
               % (CW + 4, CH + 4, CW + 7, CH + 7))


def mode_label(name):
    return svg(200, 40, text(0, 20, name, 28, THEME["accent"], 700, spacing=0.04))


# ---- layout ------------------------------------------------------------------------------------------------------------
def L(*parts, **kw):
    return " ".join(list(parts) + ['%s=%s' % (k, ('"%s"' % v) if isinstance(v, str) and (" " in v or "|" in v) else v)
                                   for k, v in kw.items()])


def layout():
    o = ["# Stevequencer, generated by mpc/gen.py (do not edit by hand: change gen.py and run it again).",
         "# Tabs = pages of 16 steps; each has six Q-Link sub-pages (PITCH, LENGTH, ON/OFF, VELO, CHANCE, RATCHET).",
         "# banks= puts a control on some sub-pages only (the big value and the highlighted chip follow the sub-page)."]
    o += ["theme_%s=%s" % kv for kv in THEME.items()] + ["art_css=stevequencer.css"]
    bank = lambda t, a: "%s %s" % (TABS[t], a)
    for t in range(4):
        o += ["", "[tab %s]" % TABS[t], L("art", file="images/bg_steps_%d.svg" % t, x=0, y=Y_OFF, w=1280, h=628)]
        others = lambda a_tab: "|".join(bank(t, n) for _, n, _, _, _ in ATTRS if n != a_tab)
        for r in range(4):
            for c in range(4):
                x, y = cell_xy(r, c)
                s = t * PAGE + cell_step(r, c) + 1
                y += Y_OFF
                o.append(L("art", file="images/playhead.svg", x=x - 4, y=y - 4, w=CW + 8, h=CH + 8, when="play_step:%d" % s))
                o.append(L("toggle", cx=x + CW // 2, cy=y + HD // 2, w=CW, h=HD, ns=0, key="s%d_on" % s,
                           img="images/hd/%02d_off.png" % s, img_on="images/hd/%02d_on.png" % s))
                for a, a_tab, _, _, _ in ATTRS:   # the big value: the sub-page's attribute, dragged like a knob
                    strip = "images/blank.png" if a == "on" else "images/arc.png"
                    o.append(L("knob", cx=x + CW // 2, cy=y + BIG_Y + BIG_H // 2, r=24, lay="side", bw=CW - 12, bh=BIG_H,
                               vs=46, key="s%d_%s" % (s, a), strip=strip, frames=128, banks=bank(t, a_tab)))
                chips = [at for at in ATTRS if at[4]]
                for k, (a, a_tab, _, _, _) in enumerate(chips):   # the footer: every value, the sub-page's in accent
                    cx = x + 8 + 22 + round(k * 222 / len(chips) * 1.0 + (222 / len(chips) - 44) / 2)
                    common = dict(cx=cx, cy=y + CHIP_Y + 13, w=44, h=26, vs=20, box="no", key="s%d_%s" % (s, a))
                    o.append(L("readout", ink="accent", banks=bank(t, a_tab), **common))
                    o.append(L("readout", ink="faint", banks=others(a_tab), **common))
        sx, sy, sw, sh = STRIP
        sy += Y_OFF
        for _, a_tab, _, _, _ in ATTRS:
            o.append(L("art", file="images/mode_%s.svg" % a_tab.replace("/", "").lower(), x=sx + 16, y=sy + 120, w=200, h=40,
                       banks=bank(t, a_tab)))
        for k, (key, yy) in enumerate((("loop_start", 196), ("loop_len", 226), ("rate", 256))):
            o.append(L("readout", cx=sx + sw - 56, cy=sy + yy, w=84, h=28, vs=24, ink="ink", box="no", key=key))
        o.append(L("readout", cx=sx + sw // 2, cy=sy + 350, w=sw - 32, h=64, vs=60, ink="accent", box="no", key="play_step"))
        for k, (tool, lab) in enumerate(PAGE_TOOLS):
            o.append(L("button", cx=sx + 16 + 52 + (k % 2) * 104, cy=sy + 484 + 28 + (k // 2) * 64, w=96, h=56,
                       label=lab, key="p%d_%s" % (t + 1, tool), img="images/btn.png", img_on="images/btn_on.png"))
        for a, a_tab, _, _, _ in ATTRS:   # Q-Link slots 4c..4c+3 = column c, top knob first
            keys = ["s%d_%s" % (t * PAGE + cell_step(r, c) + 1, a) for c in range(4) for r in range(4)]
            o.append('qlinks "%s" = %s' % (bank(t, a_tab), ",".join(keys)))
    o += ["", "[tab SETUP]", L("art", file="images/bg_setup.svg", x=0, y=Y_OFF, w=1280, h=628)]
    for c, (tag, items) in enumerate(SETUP_COLS):
        for r, it in enumerate(items):
            x, y = cell_xy(r, c)
            y += Y_OFF
            if isinstance(it, str):
                if is_toggle(it):
                    o.append(L("toggle", cx=x + CW // 2, cy=y + HD // 2, w=CW, h=HD, ns=0, key=it,
                               img="images/hd/set_%s_off.png" % it, img_on="images/hd/set_%s_on.png" % it))
                vs = 30 if it == "scale" else 46
                o.append(L("knob", cx=x + CW // 2, cy=y + BIG_Y + BIG_H // 2, r=24, lay="side", bw=CW - 12, bh=BIG_H, vs=vs,
                           key=it, strip="images/blank.png" if is_toggle(it) else "images/arc.png", frames=128))
            elif it[1]:
                for k, (key, lab) in enumerate(it[1]):
                    o.append(L("button", cx=x + 10 + 51 + k * 112, cy=y + 46 + 42, w=102, h=84, label=lab, key=key,
                               img="images/btn.png", img_on="images/btn_on.png"))
    sx, sy, sw, sh = STRIP
    o.append(L("readout", cx=sx + sw // 2, cy=sy + Y_OFF + 462, w=sw - 32, h=64, vs=60, ink="accent", box="no", key="play_step"))
    o.append('qlinks "SETUP" = %s' % ",".join(SETUP_Q))
    return "\n".join(o) + "\n"


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(data)


def main():
    ps = params()
    write(os.path.join(HERE, "params.json"), json.dumps({"name": "Stevequencer", "params": ps}, indent=1) + "\n")
    write(os.path.join(HERE, "presets.json"), json.dumps(presets(), indent=1) + "\n")
    write(os.path.join(HERE, "layout.conf"), layout())
    for t in range(4):
        write(os.path.join(IMG, "bg_steps_%d.svg" % t), bg_steps(t))
    write(os.path.join(IMG, "bg_setup.svg"), bg_setup())
    write(os.path.join(IMG, "playhead.svg"), playhead())
    for _, a_tab, _, _, _ in ATTRS:
        write(os.path.join(IMG, "mode_%s.svg" % a_tab.replace("/", "").lower()), mode_label(a_tab))
    print("params.json: %d parameters; presets.json; layout.conf; images/*.svg" % len(ps))


if __name__ == "__main__":
    main()
