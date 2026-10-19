#!/usr/bin/env python3
"""Incipience: design/stitch.html, every page of the screen, from the Stitch design of one (design/stitch-kick.html,
"INCIPIENCE Drum Machine Front Panel", Stitch project 6463560389645269614, kept as Stitch wrote it).

The Stitch page is KICK's page A: a 4 x 4 matrix of the AlphaBase-style knobs (a row a panel, a green LED and number
by each knob), the OLED, the name, a MIX panel and the instrument keys. This writes that page's markup again for each
tab and sub-page in mpc/gen.py's PAGES (KICK A, KICK B, ... MIX B): the same styles and pieces, the tab's controls,
its instrument's key lit. steve/tools/stitch/convert.py reads the result like any design (MAPS "incipience"), and
mpc/merge_ab.py folds each tab's A and B into one tab with two Q-Link sub-pages.

    python3 mpc/gen.py && python3 mpc/design.py      (from originals/incipience)
"""
import html
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "design", "stitch-kick.html")
OUT = os.path.join(ROOT, "design", "stitch.html")

KEYS = ["KICK DRUM", "MBRANE", "CLSD HI HAT", "OPEN HI HAT", "CLAP", "RIM SHOT", "CRASH", "RIDE", "X SAMPLE 1",
        "X SAMPLE 2", "FM SYNTH"]
IDS = {"kd": "KD", "mb": "MB", "ch": "CH", "oh": "OH", "cp": "CP", "rm": "RM", "cr": "CR", "rd": "RD", "x1": "X1",
       "x2": "X2", "fm": "FM"}
NAMES = {"kd": "KICK", "mb": "MBRANE", "ch": "CL HAT", "oh": "OP HAT", "cp": "CLAP", "rm": "RIM", "cr": "CRASH",
         "rd": "RIDE", "x1": "X1", "x2": "X2", "fm": "FM"}
TAB_IDS = {"MIX": "MX", "MOD": "MD", "MIDI & OUT": "IO", "MIDI MOD": "CC", "SAMPLES": "SM"}

# the screen's colours for what MPC draws itself (names, values, switches, steppers), from the design's palette
THEME = {"bg": "#efede6", "panel": "#efede6", "box": "#e4e1d8", "line": "#141414", "ink": "#141414",
         "ink-dim": "#3a3a3a", "ink-faint": "#8d8d8d", "accent": "#d32a2a", "accent-hi": "#ff3333",
         "seg-active": "#141414", "seg-active-tx": "#f4f2ec", "seg-inactive": "#dcd8ce", "btn-bg": "#2c2c2c",
         "btn-text": "#e5e5e5", "lcd": "#0b0b0b", "display-ink": "#ffffff", "knob-face": "#151515",
         "knob-ring": "#2a2a2a", "knob-dot": "#ffffff"}

EXTRA_CSS = """
    /* added for the MPC pages (mpc/design.py) */
    :root { %s }
    .ab-page { position: absolute; inset: 0; display: none; }
    .ab-page.on { display: block; }
    .ab-opt { width: 46px; height: 22px; font-size: 9px; font-weight: 800; letter-spacing: 0.06em; color: #e5e5e5;
              background: linear-gradient(180deg, #3d3d3d 0%%, #282828 100%%); border-radius: 3px; border-bottom: 2px solid #151515; }
    .ab-stepper, .ab-popup { display: flex; align-items: center; justify-content: space-between; padding: 0 8px;
              background: #0b0b0b; border: 2px solid #222; border-radius: 4px; color: #fff; font-family: 'VT323', monospace;
              font-size: 18px; letter-spacing: 0.1em; box-shadow: inset 0 2px 6px rgba(0,0,0,0.9); }
    .ab-diag { position: absolute; border-radius: 8px; background: rgba(20,20,20,0.055); border: 1px solid rgba(20,20,20,0.12); }
"""


def knob(key, label, num, size=58, ptr=-90, led=True):
    """the design's knob cell (58 px in the matrix, 40 px in the side panels)"""
    body, ptrc = ("knob-body-58", "knob-pointer-line") if size == 58 else ("knob-body-40", "knob-pointer-line-40")
    led_html = ('<div style="position: absolute; left: %dpx; top: 12px; display: flex; flex-direction: column; '
                'align-items: center; gap: 2px;"><div class="hardware-led-green"></div><span style="font-size: 8px; '
                'font-weight: 800; color: #141414;">%d</span></div>' % (size + 8, num)) if led else ""
    return ('<div style="position: relative;"><div class="ab-knob" data-param="%s"><div class="%s"><div class="%s" '
            'style="transform: rotate(%ddeg);"></div></div></div>%s</div><div style="margin-top: %dpx; display: flex; '
            'flex-direction: column; align-items: center;"><span class="ab-label">%s</span><span class="ab-value">000</span>'
            '</div>' % (key, body, ptrc, ptr, led_html, 5 if size == 58 else 6, html.escape(label)))


def switch(key, label, num=None, w=64):
    led_html = ('<div style="position: absolute; left: %dpx; top: 12px; display: flex; flex-direction: column; '
                'align-items: center; gap: 2px;"><div class="hardware-led-green"></div><span style="font-size: 8px; '
                'font-weight: 800; color: #141414;">%d</span></div>' % (w + 8, num)) if num else ""
    return ('<div style="position: relative; height: 58px; display: flex; align-items: center;"><button class="ab-toggle '
            'ab-switch" data-param="%s" type="button" style="width: %dpx;"><span style="display: inline-block; width: 4px; '
            'height: 4px; border-radius: 50%%; background: #666; margin-right: 2px;"></span>ON</button>%s</div><div '
            'style="margin-top: 5px; display: flex; flex-direction: column; align-items: center;"><span class="ab-label">%s'
            '</span><span class="ab-value">OFF</span></div>' % (key, w, led_html, html.escape(label)))


def enum_v(key, options, num):
    opts = "".join('<button class="ab-opt" type="button">%s</button>' % html.escape(o) for o in options)
    return ('<div style="position: relative;"><div class="ab-enum" data-param="%s" style="display: flex; flex-direction: '
            'column; gap: 2px;">%s</div><div style="position: absolute; left: 56px; top: 38px; display: flex; '
            'flex-direction: column; align-items: center; gap: 2px;"><div class="hardware-led-green"></div><span '
            'style="font-size: 8px; font-weight: 800; color: #141414;">%d</span></div></div>' % (key, opts, num))


def stepper(key, label, num=None, w=120):
    led_html = ('<div style="position: absolute; left: %dpx; top: 8px; display: flex; flex-direction: column; '
                'align-items: center; gap: 2px;"><div class="hardware-led-green"></div><span style="font-size: 8px; '
                'font-weight: 800; color: #141414;">%d</span></div>' % (w + 6, num)) if num else ""
    # the box and arrows stay in the background; a transparent hit area over it is the control MPC places
    return ('<div style="position: relative; height: 58px; display: flex; align-items: center;"><div class="ab-stepper" '
            'style="position: relative; width: %dpx; height: 30px;"><span>&#9664;</span><span></span><span>&#9654;</span>'
            '<div class="ab-hit ab-step-hit" data-param="%s" style="position: absolute; inset: 0;"></div></div>%s</div><div style="margin-top: 5px;"><span class="print-label" style="font-size: 9.5px;">%s</span></div>'
            % (w, key, led_html, html.escape(label)))


def popup(key, label, w=120):
    return ('<div style="position: relative; height: 40px; display: flex; align-items: center;"><div class="ab-popup" '
            'style="position: relative; width: %dpx; height: 30px;"><span></span><div class="ab-hit '
            'ab-pop-hit" data-param="%s" style="position: absolute; inset: 0;"></div></div></div><div style="margin-top: '
            '6px;"><span class="print-label" style="font-size: 9.5px;">%s</span></div>' % (w, key, html.escape(label)))


INST_PREFIX = ("kd_", "mb_", "ch_", "oh_", "cp_", "rm_", "cr_", "rd_", "x1_", "x2_", "fm_")


def short(name, inst_name, key=""):
    """a control's caption in the design: its parameter name without the instrument (MPC writes the full one live);
       on FX, MIX and the MIDI pages, where each column is an instrument, the instrument instead; on MOD without the
       slot's or the LFO's number (the row's title has it)"""
    if not inst_name and key.startswith(INST_PREFIX):
        return NAMES[key[:2]]
    n = re.sub(r"^(MOD|LFO)\d+ ", "", name)
    for pre in (inst_name + " ", "KICK ", "MBRANE ", "CL HAT ", "OP HAT ", "CLAP ", "RIM ", "CRASH ", "RIDE ", "X1 ",
                "X2 ", "FM ", "REV ", "DLY "):
        if n.startswith(pre) and len(n) > len(pre):
            n = n[len(pre):]
            break
    return n


def cell(key, P, inst_name, num):
    p = P[key]
    if key.endswith(("_sample", "_ctl")):   # a box with arrows: one sample (one control) a tap
        return stepper(key, short(p["name"], inst_name, key), num)
    if p.get("options") and len(p["options"]) == 2:
        return switch(key, short(p["name"], inst_name, key), num)
    if p.get("options") and len(p["options"]) <= 4:
        return enum_v(key, p["options"], num)
    if p.get("options") and len(p["options"]) > 12:
        return stepper(key, short(p["name"], inst_name, key), num)
    if p.get("options"):
        return popup(key, short(p["name"], inst_name, key))
    return knob(key, short(p["name"], inst_name, key), num, ptr=[-90, -15, 40, -110][num % 4])


ABBR = {"OSCILLATOR": "OSC", "MEMBRANES": "M1+M2", "COUPLING": "COUPLE", "ENV / NOISE": "NOISE", "LFO / LEVEL": "LFO",
        "LFO / ACCENT": "LFO", "LFO 1 / LEVEL": "LFO1", "LFO 2 / ACCENT": "LFO2", "PITCH ENV": "P ENV", "HH FILTER": "FILTER",
        "MM FILTER": "FILTER"}
OLED_LINE = {("FM", "A"): "FM MATRIX", ("FM", "B"): "OP ENVELOPES", ("FM", "C"): "VOICES CHORD MIX", ("FX", "A"): "REVERB + SENDS",
             ("FX", "B"): "DELAY + SENDS", ("MIX", "PAN"): "PAN + VOLUME", ("MOD", "LFO"): "FOUR LFOS",
             ("MIDI & OUT", "NOTE"): "PAD NOTES", ("MIDI & OUT", "KEYS CH"): "PLAYED PITCHED",
             ("MIDI & OUT", "OUT"): "TO OUT 1-11", ("MIDI MOD", "WHEEL"): "CC 1 MOVES", ("MIDI MOD", "GP 1"): "CC 16 MOVES",
             ("MIDI MOD", "GP 2"): "CC 17 MOVES", ("MIDI MOD", "GP 3"): "CC 18 MOVES",
             ("SAMPLES", "BROWSE"): "DRIVES FOLDERS FILES"}
OLED_LINE.update({("MOD", "MOD %d-%d" % (s, s + 3)): "SLOTS %d-%d" % (s, s + 3) for s in range(1, 17, 4)})


def matrix(rows, P, inst_name, diag=()):
    out = ['<div style="position: absolute; left: 40px; top: 18px; width: 720px; height: 520px;">']
    for r, (title, cap, keys) in enumerate(rows):
        out.append('<div style="position: absolute; left: 0; top: %dpx; width: 720px; height: 124px;">' % (128 * r))
        out.append('<div style="display: flex; justify-content: space-between; align-items: flex-end; padding-bottom: 3px; '
                   'border-bottom: 1px solid #141414;"><span class="print-label" style="font-size: 11px;">%s</span><span '
                   'class="red-caption" style="font-size: 10px;">%s</span></div>' % (html.escape(title), html.escape(cap)))
        out.append('<div style="position: relative; height: 96px; margin-top: 8px;">')
        for c, key in enumerate(keys):
            if key == "-":
                continue
            if key in diag:   # the FM matrix's diagonal: the operators' own tunings, as on the hardware's panel diagram
                out.append('<div class="ab-diag" style="left: %dpx; top: -4px; width: 130px; height: 100px;"></div>'
                           % (6 + 160 * c))
            out.append('<div style="position: absolute; left: %dpx; top: 0; width: 110px; display: flex; flex-direction: '
                       'column; align-items: center;">%s</div>' % (16 + 160 * c, cell(key, P, inst_name, 4 * r + c + 1)))
        out.append('</div></div>')
    out.append('</div>')
    return "".join(out)


# the SAMPLES tab's page (gen.py: "browser"): the folder and what's in it over eight rows, SCROLL, FILE, LOAD, UP, DRIVES. The
# rows and the two readouts aren't in this markup: convert.py has no list, so mpc/merge_ab.py adds them as layout lines
# (BROWSER_LINES, in the layout's coordinates: the page's y + 86), and the skin draws the rows' tiles itself
BROWSER_LINES = [
    "readout cx=290 cy=154 w=488 h=32 vs=20 box=no style=dotmatrix key=br_path",
    "readout cx=656 cy=154 w=196 h=32 vs=20 box=no style=dotmatrix key=br_status",
    # white text, the cursor's row in a red border (owner, 2026-10-20: "make the font white and not red"). The data
    # wheel turns the control that has MPC's hardware focus, which a touch on FILE's box (a stepper, like SAMPLE) gives
    # it; a row can't hand it to FILE (MPC's touch action works on a control's own parameter only: Akai's skins, and
    # wheel= tried on the Live II, 2026-10-20), so no row takes the focus and a tap on one leaves the wheel on FILE
    "list x=40 y=182 w=720 h=362 key=br_row cols=1 rows=8 th=40 gap=6 ink=ffffff lit=d32a2a focus=no",
]


def browser_page():
    box = ('<div style="position: absolute; left: %dpx; top: 32px; width: %dpx; height: 36px; background: #0b0b0b; '
           'border: 2px solid #222; border-radius: 4px; box-shadow: inset 0 2px 6px rgba(0,0,0,0.9);"></div>')
    btn = ('<button class="ab-btn" data-param="%s" type="button" style="position: absolute; left: %dpx; top: 458px; '
           'width: %dpx; height: 40px; padding: 0; border-radius: 4px; background: #2c2c2c; color: #e5e5e5; font-family: '
           '\'Inter\', sans-serif; font-size: 12px; font-weight: 800; letter-spacing: 0.1em; border-bottom: 3px solid '
           '#151515;">%s'
           '</button>')
    return ('<div style="position: absolute; left: 40px; top: 18px; width: 720px; height: 520px;">'
            '<div style="display: flex; justify-content: space-between; align-items: flex-end; padding-bottom: 3px; '
            'border-bottom: 1px solid #141414;"><span class="print-label" style="font-size: 11px;">BROWSE</span><span '
            'class="red-caption" style="font-size: 10px;">DRIVES &middot; FOLDERS &middot; WAV FILES</span></div>'
            + box % (0, 500) + box % (512, 208)
            # 1.1.0: SCROLL a page at a time, FILE (the cursor) one entry; LOAD, UP, DRIVES
            # (MPC sizes a button by its label, about 76, 54 and 100 px here: only its centre comes from this markup)
            + '<div style="position: absolute; left: 0; top: 444px; width: 160px;">%s</div>' % stepper("br_scroll", "SCROLL", w=160)
            + '<div style="position: absolute; left: 170px; top: 444px; width: 270px;">%s</div>' % stepper("br_cursor", "FILE: TAP, THEN THE WHEEL", w=270)
            + btn % ("br_load", 455, 76, "LOAD") + btn % ("br_up", 545, 54, "UP") + btn % ("br_places", 615, 100, "DRIVES")
            + '</div>')


def grid(key, P, per=4, w=100, h=30):
    """an option list as buttons, per to a row (LOAD TO's eight instruments): one tap each, always in sight"""
    opts = "".join('<button class="ab-opt" type="button" style="width: %dpx; height: %dpx; font-size: 11px;">%s</button>'
                   % (w, h, html.escape(o)) for o in P[key]["options"])
    return ('<div class="ab-enum" data-param="%s" style="display: flex; flex-wrap: wrap; gap: 4px; width: %dpx;">%s</div>'
            % (key, per * w + (per - 1) * 4, opts))


def side_ctl(key, P, inst_name):
    p = P[key]
    if p.get("options") and len(p["options"]) == 2:
        return switch(key, short(p["name"], inst_name), w=56)
    if p.get("options"):
        return popup(key, short(p["name"], inst_name), w=110)
    return knob(key, short(p["name"], inst_name), 0, size=40, ptr=0, led=False)


def panel(title, caption, items, top):
    return ('<div style="margin-top: %dpx; width: 430px;"><div style="display: flex; justify-content: space-between; '
            'align-items: flex-end; padding-bottom: 3px; border-bottom: 1px solid #141414;"><span class="print-label" '
            'style="font-size: 11px;">%s</span><span style="font-size: 9px; font-weight: 700; color: #8d8d8d; '
            'letter-spacing: 0.08em; text-transform: uppercase;">%s</span></div><div style="display: flex; align-items: '
            'flex-start; justify-content: space-around; margin-top: 14px; padding: 0 6px;">%s</div></div>'
            % (top, html.escape(title), html.escape(caption),
               "".join('<div style="display: flex; flex-direction: column; align-items: center;">%s</div>' % i for i in items)))


def right_column(tab, inst, title, sub, rows, side, P, info=(), side_title=None, side_panels=None):
    ident = IDS.get(inst, TAB_IDS.get(tab, tab[:2]))
    sub_name = "PAGE " + sub if sub in ("A", "B") else ("VOICE" if inst == "fm" else "MIX") if sub == "C" else sub
    words = OLED_LINE.get((tab, sub)) or " &nbsp;".join(
        html.escape(ABBR.get(t, t.split(" ")[0][:6])) for t, _, _ in rows)
    out = ['<div style="position: absolute; left: 800px; top: 20px; width: 436px; height: 510px;">',
           '<div class="oled-display" style="width: 430px; height: 120px; padding: 22px 22px; display: flex; '
           'flex-direction: column; justify-content: space-between;"><div style="display: flex; justify-content: '
           'space-between; align-items: center;"><div style="font-family: \'VT323\', \'Chakra Petch\', monospace; '
           'font-size: 32px; letter-spacing: 0.12em; color: #ffffff; line-height: 1;">%s &nbsp;%s</div><svg width="44" '
           'height="20" viewBox="0 0 50 20" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M 2 10 C 12 1, 14 1, '
           '24 10 C 34 19, 36 19, 48 10" stroke="#ffffff" stroke-width="2.2" stroke-linecap="round"/></svg></div><div '
           'style="font-family: \'VT323\', \'Chakra Petch\', monospace; font-size: 19px; letter-spacing: 0.16em; color: '
           '#ffffff; opacity: 0.95; line-height: 1;">%s &nbsp; %s</div></div>' % (ident, html.escape(title),
                                                                                html.escape(sub_name), words),
           '<div style="margin-top: 30px; display: flex; align-items: center; gap: 10px;"><div style="width: 14px; height: '
           '26px; border-radius: 7px; background: #d32a2a; box-shadow: 0 1px 2px rgba(0,0,0,0.3);"></div><div style="'
           'font-family: \'Inter\', sans-serif; font-size: 27px; font-weight: 900; letter-spacing: 0.28em; color: #141414; '
           'line-height: 1;">INCIPIENCE</div></div><div style="margin-left: 24px; margin-top: 6px; font-family: \'Inter\', '
           'sans-serif; font-size: 9.5px; font-weight: 800; letter-spacing: 0.22em; color: #141414;">ELEVEN-VOICE DRUM '
           'SYNTHESIZER</div>']
    if inst:   # RANDOMISE by the name, outside the MIX panel (owner, 2026-10-18); touch only, on no Q-Link
        out.append('<button class="ab-btn" data-param="%s_rand" type="button" style="position: absolute; right: 6px; '
                   'top: 152px; width: 150px; height: 40px; border-radius: 4px; background: #2c2c2c; color: #e5e5e5; '
                   'font-family: \'Inter\', sans-serif; font-size: 12px; font-weight: 800; letter-spacing: 0.14em; '
                   'border-bottom: 3px solid #151515;">RANDOMISE</button>' % inst)
    panels = []
    if inst:
        panels.append(("MIX", title, [side_ctl(inst + "_pan", P, title), side_ctl(inst + "_rsend", P, title),
                                      side_ctl(inst + "_dsend", P, title), switch(inst + "_mute", "MUTE", w=56)]))
    if side:
        heading = side_title or {"fm": ("FM VOICE", "CHORD + LEVEL"), None: ("DELAY", "CLOCK")}.get(inst, ("LFO", "CLOCK"))
        extra = (heading[0], heading[1], [side_ctl(k, P, title) for k in side])
        panels.insert(0 if inst == "fm" else len(panels), extra)   # FM VOICE first: its Q-Links come first
    if side_panels:   # the SAMPLES tab: its own panels (LOAD TO as a grid of buttons)
        panels = [(t, c, [grid(k, P) if len(P[k].get("options", [])) > 2 else side_ctl(k, P, title) for k in keys])
                  for t, c, keys in side_panels]
    for i, (t, c, items) in enumerate(panels):
        out.append(panel(t, c, items, 30 if i == 0 else 22))
    if info:   # the MIDI and MOD tabs: how they work, in the printing's type
        out.append('<div style="margin-top: 34px; width: 430px;"><div style="padding-bottom: 3px; border-bottom: 1px solid '
                   '#141414;"><span class="print-label" style="font-size: 11px;">HOW IT WORKS</span></div><div style="'
                   'margin-top: 12px; font-family: \'Inter\', sans-serif; font-size: 11px; font-weight: 800; letter-spacing: '
                   '0.08em; color: #3a3a3a; line-height: 1.75;">%s</div></div>' % "<br>".join(html.escape(l) for l in info))
    out.append('</div>')
    return "".join(out)


def strip(lit):
    """the instrument keys along the bottom, the tab's own lit"""
    def key(i, w):
        return ('<div style="display: flex; flex-direction: column; align-items: center; width: %dpx;"><div style="display: '
                'flex; align-items: center; gap: 4px;"><div class="instrument-key"></div><div class="key-led-red%s"></div>'
                '</div><span style="font-size: 8.5px; font-weight: 800; color: #141414; letter-spacing: 0.04em; margin-top: '
                '5px; white-space: nowrap;">%s</span></div>' % (w, " lit" if i == lit else "", KEYS[i]))

    def group(ids, gap, cap, widths):
        return ('<div style="position: relative; display: flex; gap: %dpx; padding-bottom: 12px;">%s<div style="position: '
                'absolute; left: 8px; right: 8px; bottom: 0; display: flex; flex-direction: column; align-items: center;">'
                '<div style="width: 100%%; height: 1px; background-color: #141414;"></div><span class="red-caption" '
                'style="font-size: 7.5px; margin-top: 2px; white-space: nowrap;">%s</span></div></div>'
                % (gap, "".join(key(i, w) for i, w in zip(ids, widths)), cap))
    return ('<div style="position: absolute; left: 22px; right: 22px; top: 546px; height: 82px;"><div style="position: '
            'absolute; left: 0; right: 0; top: 0; height: 8px; background-color: #d32a2a; box-shadow: 0 1px 2px '
            'rgba(0,0,0,0.2);"></div><div style="position: absolute; left: 20px; right: 20px; top: 12px; display: flex; '
            'justify-content: space-between; align-items: flex-start;">%s%s%s%s%s%s%s</div></div>'
            % (key(0, 80), key(1, 75), group([2, 3], 36, "SAMPLE + HH FILTER + VCA", [75, 75]),
               group([4, 5, 6, 7], 28, "SAMPLE + MM FILTER + VCA", [62, 65, 62, 62]), key(8, 75), key(9, 75), key(10, 75)))


def main():
    src = open(SRC).read()
    head = src[:src.index("</head>")]
    head = head.replace("<title>INCIPIENCE Drum Machine Front Panel</title>", "<title>Incipience: every page</title>")
    head = head.replace("</style>", EXTRA_CSS % " ".join("--theme-%s: %s;" % kv for kv in THEME.items()) + "</style>", 1)
    body = re.search(r'<div id="mpc-screen">(.*?)<!-- =+\s*LEFT', src, re.S).group(1)   # the cheeks and screws
    pages = json.load(open(os.path.join(ROOT, "build", "pages.json")))
    P = {p["key"]: p for p in json.load(open(os.path.join(ROOT, "params.json")))["params"]}
    names, html_pages = [], []
    for pg in pages:
        tab, inst = pg["tab"], pg["inst"]
        lit = list(IDS).index(inst) if inst in IDS else -1
        for nm, rows, sub in zip(pg["subs"], pg["sub_rows"], pg["sub_kind"]):   # C: page A's matrix, Q-Links right
            names.append(nm)
            diag = {"fm_op1", "fm_op2", "fm_op3", "fm_op4"} if tab == "FM" and sub != "B" else ()
            html_pages.append('<div class="ab-page%s" data-page="%s">%s%s%s</div>' % (
                " on" if not html_pages else "", nm,
                browser_page() if pg.get("browser") else matrix(rows, P, pg["title"] if inst else "", diag),
                right_column(tab, inst, pg["title"], sub, rows, pg["side"], P, pg["info"], pg.get("side_title"),
                             pg.get("side_panels")),
                strip(lit)))
    nav = "".join('<button class="ab-tab" type="button" onclick="abShow(%d)">%s</button>' % (i, n)
                  for i, n in enumerate(names))
    doc = (head + "</head>\n<body>\n<!-- generated by mpc/design.py from design/stitch-kick.html: do not edit -->\n"
           '<div id="ab-nav" style="display: none;">%s</div>\n<div id="mpc-screen">%s%s</div>\n<script>\nfunction '
           "abShow(i) { document.querySelectorAll('.ab-page').forEach((p, j) => p.classList.toggle('on', i === j)); }\n"
           "</script>\n</body>\n</html>\n" % (nav, body, "\n".join(html_pages)))
    open(OUT, "w").write(doc)
    # what convert.py needs beyond the MAPS entry: each page's Q-Links (a row a column; "-" an empty slot) and the
    # controls on both of a tab's pages (the shared rows and the side panels)
    ql, twice, ti = {}, set(), 0
    for pg in pages:
        seen = set()
        for rows, sub in zip(pg["sub_rows"], pg["sub_kind"]):
            ql[ti] = pg["qlinks_c"] if sub == "C" else pg["qlinks"] if pg.get("qlinks") else \
                [k for _, _, row in rows for k in (row + ["-"] * (4 - len(row)))]
            on = {k for _, _, row in rows for k in row if k != "-"} | set(pg["side"])
            if pg["inst"]:
                on |= {pg["inst"] + s for s in ("_pan", "_rsend", "_dsend", "_mute", "_rand")}
            twice |= on & seen
            seen |= on
            ti += 1
    json.dump({"qlinks": ql, "twice": sorted(twice), "tab_names": names},
              open(os.path.join(ROOT, "build", "convert.json"), "w"), indent=1)
    json.dump({pg["tab"]: BROWSER_LINES for pg in pages if pg.get("browser")},
              open(os.path.join(ROOT, "build", "layout_lines.json"), "w"), indent=1)
    print("design/stitch.html: %d pages; build/convert.json" % len(names))


if __name__ == "__main__":
    main()
