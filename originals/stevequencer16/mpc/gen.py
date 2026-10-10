#!/usr/bin/env python3
"""Stevequencer 16's generated files: params.json, presets.json, layout.conf and the SVG artwork in images/.
    python3 mpc/gen.py            (from the plugin folder or anywhere; standard library only)
Then mpc/make_png.py (in the mpc-vst-html-art container) for the PNG pictures, then tools/build.sh stevequencer16.

The parameter order is the plugin's VST parameter order: MPC saves projects and Q-Link assignments by index, so once
released, only ever append. Tabs: STEPS (the 16 steps, six Q-Link sub-pages: PITCH, LENGTH, ON/OFF, VELO, CHANCE,
RATCHET), MOD (the same grid, a sub-page per lane: each step's lane value), LANES (the eight lanes' settings, a row
each, a sub-page per lane) and SETUP. A cell's big value follows the sub-page (banks=)."""
import json
import os
import random

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(HERE, "images")
NSTEPS, NLANES, Y_OFF = 16, 8, 86

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
    ("loop_start", "LOOP START", {"min": 1, "max": 16, "default": 1, "display": "int"}, "FIRST STEP"),
    ("loop_len", "LOOP LEN", {"min": 1, "max": 16, "default": 16, "display": "int"}, "STEPS IN THE LOOP"),
    ("root", "ROOT", {"options": NOTE, "default": 0}, "SCALE ROOT"),
    ("scale", "SCALE", {"options": SCALES, "default": 0}, "PITCHES SNAP TO IT"),
    ("transpose", "TRANSPOSE", {"options": [signed(v) for v in range(-24, 25)], "default": 24}, "SEMITONES"),
    ("key_transpose", "KEY TRANSP", {"options": OFFON, "default": 0}, "PADS TRANSPOSE (C3 = 0)"),
    ("channel", "MIDI CH", {"min": 1, "max": 16, "default": 1, "display": "int"}, "NOTES AND LANES"),
    ("step_light", "STEP LIGHT", {"options": OFFON, "default": 1}, "PLAYHEAD ON SCREEN"),
    ("audition", "AUDITION", {"options": OFFON, "default": 1}, "HEAR EDITS WHILE STOPPED"),
]
TRIGGERS = [("rotate_left", "ROTATE L"), ("rotate_right", "ROTATE R"), ("random_all", "RANDOM ALL"), ("clear_all", "CLEAR ALL")]
LANEV = ["-"] + [str(v) for v in range(128)]
# per-step note attributes, in Q-Link sub-page order: (id, sub-page name, parameter name, definition, chip?)
ATTRS = [
    ("pitch", "PITCH", "PITCH", {"options": [note_name(n) for n in PITCHES], "default": 24}, True),
    ("length", "LENGTH", "LENGTH", {"options": pct(range(5, 401, 5)), "default": 9}, True),
    ("on", "ON/OFF", "ON", {"options": OFFON, "default": 1}, False),
    ("velo", "VELO", "VELO", {"min": 1, "max": 127, "default": 100, "display": "int"}, True),
    ("chance", "CHANCE", "CHANCE", {"options": pct(range(0, 101, 5)), "default": 20}, True),
    ("ratchet", "RATCHET", "RATCHET", {"options": ["x1", "x2", "x3", "x4"], "default": 0}, True),
]
DESTS = ["OFF", "CC", "PARAM"]
MODES = ["HOLD", "RETURN", "SLIDE", "LFO", "FOLLOW"]
LRATES = ["1/4X", "1/2X", "1X", "2X", "4X"]
SHAPES = ["SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "RANDOM"]
CYCLES = [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64]
# a lane's settings, in Q-Link order (columns of four): (field, name, definition per lane number, LANES header, hint)
LANE_SET = [
    ("dest", "DEST", lambda n: {"options": DESTS, "default": 1 if n <= 2 else 0}, "DEST", "OFF / CC / PARAM"),
    ("num", "NUMBER", lambda n: {"min": 1, "max": 512, "default": 19 + n if n <= 2 else 1, "display": "int",
                                 "dynamic_display": True}, "NUMBER", "CC 1-119 / P1-P512"),
    ("mode", "MODE", lambda n: {"options": MODES, "default": 0}, "MODE", ""),
    ("src", "SOURCE", lambda n: {"options": ["LANE %d" % k for k in range(1, NLANES + 1)], "default": 0}, "SOURCE", "FOLLOW"),
    ("rate", "RATE", lambda n: {"options": LRATES, "default": 2}, "RATE", ""),
    ("len", "LENGTH", lambda n: {"min": 1, "max": 16, "default": 16, "display": "int"}, "LENGTH", ""),
    ("low", "LOW", lambda n: {"min": 0, "max": 127, "default": 0, "display": "int"}, "LOW", ""),
    ("high", "HIGH", lambda n: {"min": 0, "max": 127, "default": 127, "display": "int"}, "HIGH", ""),
    ("shape", "SHAPE", lambda n: {"options": SHAPES, "default": 0}, "SHAPE", "LFO"),
    # the labels are numbers, so the options carry "values": the wrapper sends and reads the length itself
    ("cycle", "CYCLE", lambda n: {"options": ["%d STEP%s" % (c, "" if c == 1 else "S") for c in CYCLES],
                                  "values": CYCLES, "default": 7}, "CYCLE", "LFO"),
]


def params():
    out = [dict(key=k, name=n, **d) for k, n, d, _ in SETTINGS]
    out += [{"key": k, "name": n, "momentary": True, "min": 0, "max": 1} for k, n in TRIGGERS]
    # the playing step (0 = none), set by the engine; vst.json "live" makes the wrapper report it (the step light)
    out.append({"key": "play_step", "name": "PLAY STEP", "options": ["-"] + [str(i) for i in range(1, NSTEPS + 1)],
                "default": 0})
    for a, _, name, d, _ in ATTRS:
        out += [dict(key="s%d_%s" % (s, a), name="S%d %s" % (s, name), **d) for s in range(1, NSTEPS + 1)]
    for n in range(1, NLANES + 1):   # each step's lane value: the value itself (-1 = "-", none)
        out += [{"key": "l%d_s%d" % (n, s), "name": "S%d L%d" % (s, n), "options": LANEV,
                 "values": list(range(-1, 128)), "default": 0} for s in range(1, NSTEPS + 1)]
    for n in range(1, NLANES + 1):
        out += [dict(key="l%d_%s" % (n, f), name="L%d %s" % (n, name), **d(n)) for f, name, d, _, _ in LANE_SET]
    out += [{"key": "l%d_summary" % n, "name": "L%d" % n, "min": 0, "max": 0, "display": "string", "type": "readout"}
            for n in range(1, NLANES + 1)]
    return out


# ---- presets (MPC's PRESET menu): every parameter set, so switching is deterministic -------------------------------
def quant(n, root, scale):
    deg = SCALE_DEG[scale]
    for d in range(12):
        for m in (n - d, n + d):
            if (m - root) % 12 in deg:
                return m
    return n


def lane_defaults(n):
    out = {}
    for f, _, d, _, _ in LANE_SET:
        dd = d(n)
        out["l%d_%s" % (n, f)] = dd["options"][dd["default"]] if "options" in dd else dd["default"]
    return out


def preset(name, steps, lanes=None, **settings):
    """steps: up to 16 (pitch, on, length %, velo, chance %, ratchet); the rest are init steps.
    lanes: {lane number: dict(values=[16 values or None], <setting>=<option label or number>)}"""
    base = {k: d["options"][d["default"]] if "options" in d else d["default"] for k, _, d, _ in SETTINGS}
    sdefs = dict((s[0], s[2]) for s in SETTINGS)
    for k, v in settings.items():
        base[k] = v if "options" not in sdefs[k] or isinstance(v, str) else sdefs[k]["options"][v]
    vals = dict(base)
    root, scale = NOTE.index(base["root"]), SCALES.index(base["scale"])
    for i in range(NSTEPS):
        p, on, ln, ve, ch, ra = steps[i] if i < len(steps) else (60, 1, 50, 100, 100, 1)
        p = quant(p, root, scale)
        vals.update({"s%d_pitch" % (i + 1): note_name(p), "s%d_length" % (i + 1): "%d%%" % ln,
                     "s%d_on" % (i + 1): OFFON[on], "s%d_velo" % (i + 1): ve, "s%d_chance" % (i + 1): "%d%%" % ch,
                     "s%d_ratchet" % (i + 1): "x%d" % ra})
    lanes = lanes or {}
    for n in range(1, NLANES + 1):
        cfg = dict(lanes.get(n, {}))
        values = cfg.pop("values", [])
        for s in range(NSTEPS):
            v = values[s] if s < len(values) else None
            vals["l%d_s%d" % (n, s + 1)] = "-" if v is None else str(v)
        ld = lane_defaults(n)
        for k, v in cfg.items():
            key = "l%d_%s" % (n, k)
            assert key in ld, key
            ld[key] = ("%d STEP%s" % (v, "" if v == 1 else "S")) if k == "cycle" else v
        vals.update(ld)
    return {"name": name, "values": vals}


def walk(seed, n=16, lo=48, hi=72, jump=3, density=0.8, lengths=(50,), velo=(85, 115), chance=(100, 100),
         ratchet=0.0, accent=4):
    """A seeded random walk of n steps (the same every run): pitch moves up to `jump` semitones a step inside lo..hi
    (the preset puts it on the scale), `density` of the steps play (the first always does)."""
    rng = random.Random(seed)
    p, out = rng.randint(lo, hi), []
    for i in range(n):
        p = max(lo, min(hi, p + rng.randint(-jump, jump)))
        on = 1 if i == 0 or rng.random() < density else 0
        ve = min(127, rng.randint(*velo) + (15 if accent and i % accent == 0 else 0))
        ch = 5 * round(rng.randint(*chance) / 5)
        ra = rng.choice((2, 2, 3, 4)) if rng.random() < ratchet else 1
        out.append((p, on, rng.choice(lengths), ve, ch, ra))
    return out


def every(step_vals):
    """{step: value} -> 16 values (None elsewhere)."""
    return [step_vals.get(s) for s in range(1, NSTEPS + 1)]


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
    stabs = [(63 if i % 8 == 2 else 67, 1 if i % 4 == 2 else 0, 35, 115, 100, 1) for i in range(16)]
    return {"presets": [
        preset("Init", []),
        # lanes 1 and 2 default to CC 20 and CC 21: the first two Q-Links of this repo's instruments
        preset("Acid Slide", acid, {1: dict(mode="SLIDE", values=every({1: 20, 5: 90, 9: 40, 13: 110})),
                                    2: dict(mode="LFO", shape="SQUARE", cycle=4, low=40, high=90)},
               scale="MINOR", root="A", swing="54%"),
        preset("Bass Sweep Group", bass, {1: dict(mode="SLIDE", values=every({1: 10, 16: 127})),
                                          2: dict(mode="FOLLOW", src="LANE 1", low=100, high=30)},
               scale="MINOR", swing="56%"),
        preset("LFO Pulse", walk(11, lo=50, hi=62, jump=5, density=0.95, lengths=(40, 50)),
               {1: dict(mode="LFO", shape="SINE", cycle=16, low=30, high=100),
                2: dict(mode="LFO", shape="TRIANGLE", cycle=6, low=20, high=90)}, scale="MINOR", root="D"),
        preset("Random Steps", walk(12, lo=60, hi=84, jump=4, density=0.7, lengths=(25, 35, 50), chance=(60, 100)),
               {1: dict(mode="LFO", shape="RANDOM", cycle=1, low=40, high=110)}, scale="PENTA MIN", root="E"),
        preset("Half-Time Mod", walk(13, lo=48, hi=67, jump=3, density=0.75, lengths=(30, 60, 90)),
               {1: dict(rate="1/2X", values=[30, 60, 90, 120, 90, 60, 30, 10] + [None] * 8),
                2: dict(len=3, values=[30, 80, 120])}, scale="DORIAN", root="G", swing="58%"),
        preset("Group of Three", walk(14, lo=52, hi=76, jump=2, density=0.9, lengths=(45,), ratchet=0.2),
               {1: dict(dest="OFF", mode="SLIDE", values=every({1: 0, 9: 127})),   # the leader: sends nothing itself
                2: dict(mode="FOLLOW", src="LANE 1", low=20, high=120),
                3: dict(mode="FOLLOW", src="LANE 1", low=110, high=30),
                4: dict(dest="CC", num=22, mode="FOLLOW", src="LANE 1", low=40, high=90)},
               scale="PHRYGIAN", root="E"),
        preset("Accent Return", stabs, {1: dict(mode="RETURN", low=30, values=every({3: 120, 7: 100, 11: 120, 15: 90}))},
               scale="MINOR"),
        preset("Bass Line", bass, scale="MINOR", swing="56%"),
        preset("Arp Climb", arp, {1: dict(mode="SLIDE", values=every({1: 30, 16: 110}))}, scale="MAJOR", rate="1/16"),
        preset("Blues Shuffle", walk(16, lo=48, hi=65, jump=3, density=0.8, lengths=(60, 40)),
               {1: dict(mode="LFO", shape="SAW DOWN", cycle=2, low=40, high=100)},
               scale="BLUES", root="A", swing="66%", rate="1/8"),
        preset("Ratchet Stabs", walk(18, lo=55, hi=67, jump=7, density=0.45, lengths=(25,), ratchet=0.45, velo=(95, 120)),
               {1: dict(mode="LFO", shape="RANDOM", cycle=1, low=50, high=120), 2: dict(rate="2X", len=4, values=[20, 70, 120, 70])},
               scale="MINOR", root="C", gate="80%"),
        preset("Pendulum Arp", walk(19, lo=60, hi=88, jump=5, density=1.0, lengths=(40,), accent=8),
               {1: dict(mode="LFO", shape="TRIANGLE", cycle=32, low=20, high=120)}, scale="MAJOR", root="D", direction="PEND"),
        preset("Drunk Garden", walk(20, lo=55, hi=84, jump=6, density=0.9, lengths=(30, 50, 80), chance=(40, 90)),
               {1: dict(len=5, values=[40, 90, 60, 110, 20]), 2: dict(len=7, mode="SLIDE", values=[10, None, None, 120])},
               scale="MIXOLYDIAN", root="G", direction="DRUNK"),
    ]}


# ---- screen geometry (plugin area 1280 x 628; layout y = screen y + 86) ---------------------------------------------
CW, CH, GX, GY, X0, Y0 = 238, 140, 254, 152, 16, 16
HD, BIG_Y, BIG_H, CHIP_Y = 36, 36, 68, 108
STRIP = (1032, 16, 232, 596)
cell_xy = lambda r, c: (X0 + c * GX, Y0 + r * GY)
cell_step = lambda r, c: r * 4 + c   # rows: steps 1-4 across the top row
THEME = {"bg": "101216", "panel": "191c21", "line": "2a2f37", "ink": "eef1f4", "ink_dim": "a3abb5", "ink_faint": "5f6873",
         "accent": "ffb020", "accent_hi": "ffd27a", "box": "191c21", "lcd": "0d0f12", "seg_active": "ffb020",
         "seg_active_tx": "1d1404", "seg_inactive": "22262c", "btn_bg": "23272d", "btn_text": "eef1f4"}
# LANES table: a lane label column, then the ten settings
LX, LW, LCW, LHY, LRY, LRH = 16, 84, 114, 16, 60, 70   # label x/w, setting column width, header y, first row y, row height
lane_col_x = lambda k: LX + LW + 6 + k * LCW + 6 * (k >= 4) + 6 * (k >= 8)   # a gap between Q-Link columns (1-4 | 5-8 | 9-10)
lane_row_y = lambda n: LRY + (n - 1) * LRH


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


def strip_box(title, big):
    x, y, w, h = STRIP
    o = rrect(x + 0.5, y + 0.5, w - 1, h - 1, 10, "141619", THEME["line"])
    o += text(x + 16, y + 26, title, 13, THEME["ink_faint"], 700, spacing=0.12)
    o += text(x + 16, y + 66, big, 46, THEME["ink"], 700, spacing=0)
    return o


def bg_steps():
    o = rrect(0, 0, 1280, 628, 0, THEME["bg"])
    for r in range(4):
        for c in range(4):
            o += cell_box(*cell_xy(r, c))
    x, y, w, h = STRIP
    o += strip_box("STEPS", "1–16")
    o += text(x + 16, y + 112, "Q-LINKS EDIT", 13, THEME["ink_faint"], 700, spacing=0.12)
    o += hr(x + 16, y + 172, w - 32)
    for lab, yy in (("LOOP START", 196), ("LOOP LEN", 226), ("RATE", 256)):
        o += text(x + 16, y + yy, lab, 16, THEME["ink_dim"], 600, spacing=0.04)
    o += hr(x + 16, y + 280, w - 32)
    o += text(x + 16, y + 304, "NOW PLAYING", 13, THEME["ink_faint"], 700, spacing=0.12)
    o += text(x + 16, y + 412, "TAP A HEADER: ON/OFF", 12, THEME["ink_faint"], 600, spacing=0.06)
    o += text(x + 16, y + 432, "DRAG A VALUE TO CHANGE IT", 12, THEME["ink_faint"], 600, spacing=0.06)
    return svg(1280, 628, o)


def bg_mod():
    o = rrect(0, 0, 1280, 628, 0, THEME["bg"])
    for r in range(4):
        for c in range(4):
            x, y = cell_xy(r, c)
            o += cell_box(x, y)
            o += rrect(x + 1, y + 1, CW - 2, HD - 1, 9, "1f2227")
            o += text(x + 12, y + 18, "%02d" % (cell_step(r, c) + 1), 18, THEME["ink_dim"], 700, spacing=0.04)
            o += text(x + CW / 2, y + CHIP_Y + 13, "-  =  NO VALUE", 13, THEME["ink_faint"], 400, "middle", 0.04)
    x, y, w, h = STRIP
    o += strip_box("MOD", "LANES")
    o += text(x + 16, y + 112, "Q-LINKS EDIT", 13, THEME["ink_faint"], 700, spacing=0.12)
    o += hr(x + 16, y + 172, w - 32)
    o += text(x + 16, y + 196, "SENDS TO · MODE", 13, THEME["ink_faint"], 700, spacing=0.12)
    o += hr(x + 16, y + 280, w - 32)
    o += text(x + 16, y + 304, "NOW PLAYING", 13, THEME["ink_faint"], 700, spacing=0.12)
    for k, line in enumerate(("A VALUE PER STEP, 0-127.", "SET WHERE IT SENDS AND", "HOW IT MOVES ON LANES.")):
        o += text(x + 16, y + 412 + k * 20, line, 12, THEME["ink_faint"], 600, spacing=0.06)
    return svg(1280, 628, o)


def bg_lanes():
    o = rrect(0, 0, 1280, 628, 0, THEME["bg"])
    for k, (_, _, _, head, hint) in enumerate(LANE_SET):
        x = lane_col_x(k)
        o += text(x + LCW / 2, LHY + 12, head, 14, THEME["ink_dim"], 700, "middle", 0.08)
        if hint:
            o += text(x + LCW / 2, LHY + 30, hint, 10, THEME["ink_faint"], 600, "middle", 0.04)
    for n in range(1, NLANES + 1):
        y = lane_row_y(n)
        o += rrect(LX + 0.5, y + 0.5, 1280 - 2 * LX - 1, LRH - 7, 8, THEME["panel"], THEME["line"])
        o += text(LX + 14, y + (LRH - 6) / 2, "LANE %d" % n, 16, THEME["ink_dim"], 700, spacing=0.04)
        for k in (4, 8):   # the Q-Link columns: 1-4 | 5-8 | 9-10
            xx = lane_col_x(k) - 4
            o += '<rect x="%d" y="%d" width="1" height="%d" fill="#%s"/>' % (xx, y + 8, LRH - 22, THEME["line"])
    return svg(1280, 628, o)


def row_highlight():   # the active lane's row on LANES (one picture, placed per sub-page)
    return svg(1280 - 2 * LX, LRH - 6, '<rect x="1.5" y="1.5" width="%d" height="%d" rx="8" fill="#%s" fill-opacity="0.07" '
               'stroke="#%s" stroke-width="2"/>' % (1280 - 2 * LX - 3, LRH - 9, THEME["accent"], THEME["accent"]))


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
label_file = lambda name: "images/mode_%s.svg" % name.replace("/", "").replace(" ", "").lower()


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
                    for k, line in enumerate(("Port: [SEQ] Stevequencer", "16 MIDI Out. Pick it as an", "instrument track's",
                                              "MIDI input.")):
                        o += text(x + 12, y + 54 + k * 20, line, 15, THEME["ink_dim"], 400, spacing=0.01)
    x, y, w, h = STRIP
    o += strip_box("PAGE", "SETUP")
    help_ = ["Stevequencer 16 makes no", "sound: its notes and lanes", "leave through its own port.", "",
             "1. Menu > Preferences > MIDI:", "   Track on for its MIDI Out.",
             "2. On the instrument's track:", "   MIDI Input Port = that port,", "   Monitor = In.",
             "3. Press play: it follows", "   MPC's tempo."]
    for k, line in enumerate(help_):
        o += text(x + 16, y + 110 + k * 22, line, 15, THEME["ink_dim"] if line[:1].isdigit() else "8d949c", 400, spacing=0.01)
    o += hr(x + 16, y + 390, w - 32)
    o += text(x + 16, y + 414, "NOW PLAYING", 13, THEME["ink_faint"], 700, spacing=0.12)
    return svg(1280, 628, o)


def playhead():   # the playing step's outline
    return svg(CW + 8, CH + 8, '<rect x="2" y="2" width="%d" height="%d" rx="12" fill="none" stroke="#ffffff" stroke-width="3"/>'
               '<rect x="0.5" y="0.5" width="%d" height="%d" rx="13" fill="none" stroke="#ffffff" stroke-opacity="0.25" stroke-width="1"/>'
               % (CW + 4, CH + 4, CW + 7, CH + 7))


def big_label(name):
    return svg(200, 40, text(0, 20, name, 28, THEME["accent"], 700, spacing=0.04))


# ---- layout ------------------------------------------------------------------------------------------------------------
def L(*parts, **kw):
    return " ".join(list(parts) + ['%s=%s' % (k, ('"%s"' % v) if isinstance(v, str) and (" " in v or "|" in v) else v)
                                   for k, v in kw.items()])


LANE_TABS = ["LANE %d" % n for n in range(1, NLANES + 1)]


def layout():
    o = ["# Stevequencer 16, generated by mpc/gen.py (do not edit by hand: change gen.py and run it again).",
         "# STEPS: six Q-Link sub-pages (PITCH ... RATCHET); MOD and LANES: a sub-page per lane. banks= puts a control on",
         "# some sub-pages only (the big value, the highlighted chip, the lane's row)."]
    o += ["theme_%s=%s" % kv for kv in THEME.items()] + ["art_css=stevequencer16.css",
          # sd88me's release tools draw it as this repo's do with these (the old tools ignore them)
          "qlink_bounds=column", "qlink_box=slot", "label_scale=1", "focus_ring=1"]
    sx, sy, sw, sh = STRIP
    # STEPS
    o += ["", "[tab STEPS]", L("art", file="images/bg_steps.svg", x=0, y=Y_OFF, w=1280, h=628)]
    others = lambda a_tab: "|".join(n for _, n, _, _, _ in ATTRS if n != a_tab)
    for r in range(4):
        for c in range(4):
            x, y = cell_xy(r, c)
            s = cell_step(r, c) + 1
            y += Y_OFF
            o.append(L("art", file="images/playhead.svg", x=x - 4, y=y - 4, w=CW + 8, h=CH + 8, when="play_step:%d" % s))
            o.append(L("toggle", cx=x + CW // 2, cy=y + HD // 2, w=CW, h=HD, ns=0, key="s%d_on" % s,
                       img="images/hd/%02d_off.png" % s, img_on="images/hd/%02d_on.png" % s))
            for a, a_tab, _, _, _ in ATTRS:   # the big value: the sub-page's attribute, dragged like a knob
                strip = "images/blank.png" if a == "on" else "images/arc.png"
                o.append(L("knob", cx=x + CW // 2, cy=y + BIG_Y + BIG_H // 2, r=24, lay="side", bw=CW - 12, bh=BIG_H,
                           vs=46, key="s%d_%s" % (s, a), strip=strip, frames=128, banks=a_tab))
            chips = [at for at in ATTRS if at[4]]
            for k, (a, a_tab, _, _, _) in enumerate(chips):   # the footer: every value, the sub-page's in accent
                cx = x + 8 + 22 + round(k * 222 / len(chips) + (222 / len(chips) - 44) / 2)
                common = dict(cx=cx, cy=y + CHIP_Y + 13, w=44, h=26, vs=20, box="no", key="s%d_%s" % (s, a))
                o.append(L("readout", ink="accent", banks=a_tab, **common))
                o.append(L("readout", ink="faint", banks=others(a_tab), **common))
    for _, a_tab, _, _, _ in ATTRS:
        o.append(L("art", file=label_file(a_tab), x=sx + 16, y=sy + Y_OFF + 120, w=200, h=40, banks=a_tab))
    for key, yy in (("loop_start", 196), ("loop_len", 226), ("rate", 256)):
        o.append(L("readout", cx=sx + sw - 56, cy=sy + Y_OFF + yy, w=84, h=28, vs=24, ink="ink", box="no", key=key))
    o.append(L("readout", cx=sx + sw // 2, cy=sy + Y_OFF + 350, w=sw - 32, h=64, vs=60, ink="accent", box="no", key="play_step"))
    for k, (tool, lab) in enumerate([("rotate_left", "◀ ROT"), ("rotate_right", "ROT ▶"), ("random_all", "RANDOM"),
                                     ("clear_all", "CLEAR")]):
        o.append(L("button", cx=sx + 16 + 52 + (k % 2) * 104, cy=sy + Y_OFF + 484 + 28 + (k // 2) * 64, w=96, h=56,
                   label=lab, key=tool, img="images/btn.png", img_on="images/btn_on.png"))
    for a, a_tab, _, _, _ in ATTRS:   # Q-Link slots 4c..4c+3 = column c, top knob first
        o.append('qlinks "%s" = %s' % (a_tab, ",".join("s%d_%s" % (cell_step(r, c) + 1, a) for c in range(4) for r in range(4))))
    # MOD: each step's value for the sub-page's lane
    o += ["", "[tab MOD]", L("art", file="images/bg_mod.svg", x=0, y=Y_OFF, w=1280, h=628)]
    for r in range(4):
        for c in range(4):
            x, y = cell_xy(r, c)
            s = cell_step(r, c) + 1
            y += Y_OFF
            o.append(L("art", file="images/playhead.svg", x=x - 4, y=y - 4, w=CW + 8, h=CH + 8, when="play_step:%d" % s))
            for n in range(1, NLANES + 1):
                o.append(L("knob", cx=x + CW // 2, cy=y + BIG_Y + BIG_H // 2 + 4, r=24, lay="side", bw=CW - 12, bh=BIG_H,
                           vs=46, key="l%d_s%d" % (n, s), strip="images/arc.png", frames=128, banks=LANE_TABS[n - 1]))
    for n in range(1, NLANES + 1):
        tab = LANE_TABS[n - 1]
        o.append(L("art", file=label_file(tab), x=sx + 16, y=sy + Y_OFF + 120, w=200, h=40, banks=tab))
        o.append(L("readout", cx=sx + sw // 2, cy=sy + Y_OFF + 236, w=sw - 32, h=44, vs=26, ink="ink", box="no",
                   key="l%d_summary" % n, banks=tab))
    o.append(L("readout", cx=sx + sw // 2, cy=sy + Y_OFF + 350, w=sw - 32, h=64, vs=60, ink="accent", box="no", key="play_step"))
    for n in range(1, NLANES + 1):
        o.append('qlinks "%s" = %s' % (LANE_TABS[n - 1], ",".join("l%d_s%d" % (n, cell_step(r, c) + 1)
                                                                    for c in range(4) for r in range(4))))
    # LANES: a row per lane; the sub-page's row lit and on the Q-Links
    o += ["", "[tab LANES]", L("art", file="images/bg_lanes.svg", x=0, y=Y_OFF, w=1280, h=628)]
    for n in range(1, NLANES + 1):
        y = lane_row_y(n) + Y_OFF
        o.append(L("art", file="images/row_hl.svg", x=LX, y=y, w=1280 - 2 * LX, h=LRH - 6, banks=LANE_TABS[n - 1]))
        for k, (f, _, _, _, _) in enumerate(LANE_SET):
            x = lane_col_x(k)
            o.append(L("knob", cx=x + LCW // 2, cy=y + (LRH - 6) // 2, r=14, lay="side", bw=LCW - 8, bh=LRH - 18, vs=19,
                       key="l%d_%s" % (n, f), strip="images/arc_s.png", frames=128))
    for n in range(1, NLANES + 1):
        keys = ["l%d_%s" % (n, f) for f, _, _, _, _ in LANE_SET]
        o.append('qlinks "%s" = %s' % (LANE_TABS[n - 1], ",".join(keys)))
    # SETUP
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
    o.append(L("readout", cx=sx + sw // 2, cy=sy + Y_OFF + 462, w=sw - 32, h=64, vs=60, ink="accent", box="no", key="play_step"))
    o.append('qlinks "SETUP" = %s' % ",".join(SETUP_Q))
    return "\n".join(o) + "\n"


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(data)


def main():
    ps = params()
    write(os.path.join(HERE, "params.json"), json.dumps({"name": "Stevequencer 16", "params": ps}, indent=1) + "\n")
    write(os.path.join(HERE, "presets.json"), json.dumps(presets(), indent=1) + "\n")
    write(os.path.join(HERE, "layout.conf"), layout())
    write(os.path.join(IMG, "bg_steps.svg"), bg_steps())
    write(os.path.join(IMG, "bg_mod.svg"), bg_mod())
    write(os.path.join(IMG, "bg_lanes.svg"), bg_lanes())
    write(os.path.join(IMG, "bg_setup.svg"), bg_setup())
    write(os.path.join(IMG, "row_hl.svg"), row_highlight())
    write(os.path.join(IMG, "playhead.svg"), playhead())
    for name in [n for _, n, _, _, _ in ATTRS] + LANE_TABS:
        write(os.path.join(HERE, label_file(name)), big_label(name))
    print("params.json: %d parameters; presets.json: %d; layout.conf; images/*.svg" % (len(ps), len(presets()["presets"])))


if __name__ == "__main__":
    main()
