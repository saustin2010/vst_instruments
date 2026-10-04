#!/usr/bin/env python3
"""Build the skin artwork (bg.png, knob images, model LED pictures) from Mutable Instruments' own panel
vector (src/plaits_v50_panel.svg, pdftocairo -svg of plaits/hardware_design/panel/plaits_v50.ai, CC-BY-SA 3.0)
plus drawn knobs/LEDs, laid out for the MPC screen. Coordinates here are layout (shadow canvas) coordinates;
the plugin area is canvas y 86..714, so bg.png y = canvas y - 86. Keep in sync with ../layout.conf.

    python3 make_art.py      # needs rsvg-convert (brew install librsvg)
"""
import math
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
W, H, Y0 = 1280, 628, 86

PANEL = "#E8E8E6"
INK = "#231F20"
BAND = "#BDBCBC"
BOX = "#656263"

# layout.conf positions (canvas coords)
GRID_X = [640 + (i - 3.5) * 66 for i in range(8)]
TEAL_Y, LED_Y, CORAL_Y = 190, 232, 274
TEAL, INK_DIM = "#03A3AC", "#6E6B6C"
POPUP = (640, 336)
ATT = {"lpg_color": 470, "fm_amount": 640, "lpg_decay": 810}
ATT_Y, ATT_R = 470, 30
TIMBRE, MORPH, KNOB_S_R = (170, 508), (1110, 508), 46
AUX = (640, 612, 26)

# panel vector coordinates (pt) of the pieces reused from the real panel
P_TEAL_X, P_CORAL_X = 73.7, 98.0
P_ROWS = [70.3, 85.8, 100.8, 116.2, 131.7, 146.7, 162.0, 177.5]
P_ICON = 4.7                      # half-size of an icon crop
P_WRENCH, P_DOTS = (57.9, 48.3), (113.9, 48.3)
P_BAND = (20.0, 22.3, 38.75, 11.2)   # one period of the chevron band, X centre to X centre
P_OUT, P_AUX = (110.4, 293.6, 15.8, 7.9), (142.6, 293.6, 16.6, 7.9)


_BODY = []


def panel_body():
    if not _BODY:
        src = open(os.path.join(HERE, "src", "plaits_v50_panel.svg")).read()
        inner = src[src.index(">", src.index("<svg")) + 1:src.rindex("</svg>")]
        # drop the red drill/placement marks (jack and pot holes, LED holes, screws)
        _BODY.append(re.sub(r'<path[^>]*stroke="rgb\(92\.655945%, 11\.062622%, 14\.993286%\)"[^>]*/>', "", inner))
    return _BODY[0]


PIECES = {}


def piece(x, y, w, h, vx, vy, vw, vh):
    """Draw the panel region (vx, vy, vw, vh) [pt] into the box (x, y, w, h) [bg px]. Each region is rendered once
    to a PNG (librsvg caps how often one element can be instanced, so <use> of the whole panel runs out)."""
    key = (vx, vy, vw, vh)
    if key not in PIECES:
        name = "pieces/p%02d.png" % len(PIECES)
        PIECES[key] = name
        path = os.path.join(HERE, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = path[:-4] + ".svg"
        open(tmp, "w").write('<svg xmlns="http://www.w3.org/2000/svg" width="%g" height="%g" viewBox="%g %g %g %g">'
                             '%s</svg>' % (vw * 16, vh * 16, vx, vy, vw, vh, panel_body()))
        subprocess.check_call(["rsvg-convert", tmp, "-o", path])
        os.remove(tmp)
    return ('<image x="%.1f" y="%.1f" width="%.1f" height="%.1f" preserveAspectRatio="xMidYMid meet" href="%s"/>'
            % (x, y, w, h, PIECES[key]))


def knockout(img_tag, colour="#E4E4E2"):
    """The panel's OUT/AUX words are holes in the dark box (the aluminium shows through): turn the piece into
    just the letters, light, so it sits on this skin's own box without edge slivers."""
    name = re.search(r'href="([^"]+)"', img_tag).group(1)
    path = os.path.join(HERE, name)
    subprocess.check_call(["magick", path, "-alpha", "extract", "-negate", "-background", colour, "-alpha", "shape", path])
    return img_tag


def icon(cx, cy, r, px, py):
    return piece(cx - r, cy - Y0 - r, 2 * r, 2 * r, px - P_ICON, py - P_ICON, 2 * P_ICON, 2 * P_ICON)


def screw(cx, cy):
    y = cy - Y0
    return ('<circle cx="%d" cy="%d" r="15" fill="url(#screw)" stroke="#a9a9a6" stroke-width="1.5"/>'
            '<path d="M%d %d h14 M%d %d v14" stroke="#8e8e8b" stroke-width="4" stroke-linecap="round"/>'
            % (cx, y, cx - 7, y, cx, y - 7))


def dotted(d):
    return ('<path d="%s" fill="none" stroke="%s" stroke-width="3.2" stroke-linecap="round" '
            'stroke-dasharray="0.1 7.5"/>' % (d, INK))


def led(cx, cy, colour=None):
    o = ('<circle cx="%g" cy="%g" r="13" fill="#cfcfcb" stroke="#b3b3ae" stroke-width="1"/>'
         '<circle cx="%g" cy="%g" r="10" fill="#EFE2A6"/>' % (cx, cy, cx, cy))
    if colour:
        o += ('<circle cx="%g" cy="%g" r="15" fill="%s" opacity="0.45" filter="url(#glow)"/>'
              '<circle cx="%g" cy="%g" r="10" fill="%s"/>' % (cx, cy, colour, cx, cy, colour))
    return o + '<circle cx="%g" cy="%g" r="4" fill="#fff" opacity="0.55"/>' % (cx - 3, cy - 3)


def band(th):
    """The chevron band as one seamless strip: one period rendered at 4x, tiled, scaled down."""
    bx, by, bw, bh = P_BAND
    tw4, th4 = round(bw * th / bh * 4), th * 4
    os.makedirs(os.path.join(HERE, "pieces"), exist_ok=True)
    tile = os.path.join(HERE, "pieces", "band_tile.png")
    tmp = tile[:-4] + ".svg"
    open(tmp, "w").write('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="%g %g %g %g" '
                         'preserveAspectRatio="none">%s</svg>' % (tw4, th4, bx, by, bw, bh, panel_body()))
    subprocess.check_call(["rsvg-convert", tmp, "-o", tile])
    os.remove(tmp)
    out = os.path.join(HERE, "pieces", "band.png")
    subprocess.check_call(["magick", "-size", "%dx%d" % (W * 4, th4), "tile:" + tile, "-resize", "%dx%d!" % (W, th), out])
    return "pieces/band.png"


def page_base(page=None):
    """What every page shares: the panel, its four screws, the title and the chevron band."""
    o = ['<rect width="%d" height="%d" fill="%s"/>' % (W, H, PANEL)]
    o += [screw(34, 106), screw(1246, 106), screw(34, 694), screw(1246, 694)]
    sub = ('<tspan fill="%s" font-weight="500" font-size="22" dx="14">%s</tspan>' % (TEAL, page)) if page else ""
    o.append('<text x="640" y="%d" text-anchor="middle" font-family="Helvetica Neue" font-weight="300" '
             'font-size="34" fill="%s">MPC Plaits%s</text>' % (120 - Y0, INK, sub))
    o.append('<image x="0" y="%d" width="%d" height="32" preserveAspectRatio="none" href="%s"/>' % (130 - Y0, W, band(32)))
    return o


def section(x, y, w, h, title):
    """A group of controls: a rounded outline in the band's grey, titled with a dark legend tab like the
    module's buttons. Canvas coordinates."""
    y -= Y0
    tw = 16 + 11.5 * len(title)
    return ('<rect x="%g" y="%g" width="%g" height="%g" rx="14" fill="#EDEDEB" stroke="%s" stroke-width="2"/>'
            '<rect x="%g" y="%g" width="%g" height="26" rx="13" fill="%s"/>'
            '<text x="%g" y="%g" text-anchor="middle" font-family="Helvetica Neue" font-weight="700" font-size="15" '
            'letter-spacing="1.2" fill="#F4F4F2">%s</text>'
            % (x, y, w, h, BAND, x + 18, y - 13, tw, INK, x + 18 + tw / 2, y + 5, title.replace("&", "&amp;")))


# ENV page: a live ADSR drawing made of display-only filmstrips (layout `meter` lines), one per parameter, since a
# skin can't draw -- MPC only shows the frame matching each value. Four slots (A, D, S, R); a strip can only follow
# one parameter, so each slot stacks layers that hide each other's excess (see ENV_LAYERS). A strip's frames are
# square and a strip is at most 16384 px (docs/NOTES.md), so a slot is at most 128 px: 120 here.
ENV_SLOT, ENV_FRAMES = 120, 128
ENV_TOP, ENV_BOTTOM = 10, 110            # level 1 / level 0, inside a frame
ENV_CY = 470                             # canvas y of the slots' centre
ENV_SLOT_X = [98, 218, 338, 458]         # slot centres, from the section's left edge
SECTION_FILL = "#EDEDEB"


def env_level_y(level):
    return ENV_BOTTOM - (ENV_BOTTOM - ENV_TOP) * level


def env_time_width(n):
    """A time knob's position (0..1 of 0..5000 ms) -> how much of a slot its segment takes (short times visible)."""
    return ENV_SLOT * max(0.03, n ** 0.4)


def env_curve(x0, width, falling, steps=24):
    """Exponential segment points across width: rising 0 -> 1 (attack) or falling 1 -> 0."""
    k = 4.0
    pts = []
    for i in range(steps + 1):
        t = i / steps
        v = (1 - math.exp(-k * t)) / (1 - math.exp(-k))
        pts.append((x0 + t * width, env_level_y(1 - v if falling else v)))
    return pts


def env_frame(layer, n):
    """One frame (ENV_SLOT square) of a layer at knob position n."""
    line = '<path d="M%s" fill="none" stroke="%s" stroke-width="3" stroke-linejoin="round" stroke-linecap="round"/>'
    fill = '<path d="M%s Z" fill="%s"/>'
    pts = lambda ps: " L".join("%.1f %.1f" % p for p in ps)
    if layer == "attack":         # flat at zero, then the ramp up, ending at the slot's right edge
        w = env_time_width(n)
        ps = [(0, env_level_y(0))] + env_curve(ENV_SLOT - w, w, False)
        return line % (pts(ps), TEAL)
    if layer in ("decay", "release"):   # from the top down to zero over the time, then flat at zero
        w = env_time_width(n)
        ps = env_curve(0, w, True) + [(ENV_SLOT, env_level_y(0))]
        if layer == "decay":      # panel under the curve: hides the sustain line until the curve reaches it
            cover = fill % (pts(ps + [(ENV_SLOT, ENV_SLOT), (0, ENV_SLOT)]), SECTION_FILL)
        else:                     # panel over the curve: hides the sustain line once the curve has gone below it
            cover = fill % (pts(ps + [(ENV_SLOT, 0), (0, 0)]), SECTION_FILL)
        return cover + line % (pts(ps), TEAL)
    y = env_level_y(n)
    if layer == "sustain":        # the sustain level as a line across the slot
        return line % (pts([(0, y), (ENV_SLOT, y)]), TEAL)
    if layer == "below":          # panel below the sustain level: cuts the decay curve where it passes it
        return '<rect x="0" y="%.1f" width="%d" height="%d" fill="%s"/>' % (y + 2, ENV_SLOT, ENV_SLOT, SECTION_FILL)
    if layer == "above":          # panel above the sustain level: the release starts where its curve crosses it
        return '<rect x="0" y="0" width="%d" height="%.1f" fill="%s"/>' % (ENV_SLOT, max(0, y - 2), SECTION_FILL)
    raise ValueError(layer)


# Per slot, bottom to top: (layer, parameter suffix). D: the sustain line under the decay curve (whose panel fill hides
# it before the crossing), then a panel below the sustain level on top (hiding the curve after it). R: the sustain
# line, the release curve with panel above it, then a panel above the sustain level.
ENV_LAYERS = [[("attack", "attack")],
              [("sustain", "sustain"), ("decay", "decay"), ("below", "sustain")],
              [("sustain", "sustain")],
              [("sustain", "sustain"), ("release", "release"), ("above", "sustain")]]


def env_strip(layer):
    # each frame is its own clipped viewport, so a mask that runs past its square can't spill into the next frame
    frames = "".join('<svg x="0" y="%d" width="%d" height="%d" overflow="hidden">%s</svg>'
                     % (i * ENV_SLOT, ENV_SLOT, ENV_SLOT, env_frame(layer, i / (ENV_FRAMES - 1)))
                     for i in range(ENV_FRAMES))
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">%s</svg>'
            % (ENV_SLOT, ENV_SLOT * ENV_FRAMES, ENV_SLOT, ENV_SLOT * ENV_FRAMES, frames))


def env_guides(x0):
    """Static parts under the live ADSR: the zero line and the slot letters."""
    y0 = ENV_CY - ENV_SLOT // 2 - Y0
    o = ['<path d="M%g %g H%g" stroke="%s" stroke-width="1.5" stroke-dasharray="2 5"/>'
         % (x0 + ENV_SLOT_X[0] - ENV_SLOT / 2, y0 + env_level_y(0), x0 + ENV_SLOT_X[-1] + ENV_SLOT / 2, BAND)]
    for cx, t in zip(ENV_SLOT_X, "ADSR"):
        o.append('<text x="%g" y="%g" text-anchor="middle" font-family="Helvetica Neue" font-weight="700" '
                 'font-size="15" fill="%s">%s</text>' % (x0 + cx, y0 + ENV_SLOT + 16, INK_DIM, t))
    return "".join(o)


def svg_page(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="%d" height="%d" '
            'viewBox="0 0 %d %d"><defs>'
            '<radialGradient id="screw" cx="40%%" cy="35%%" r="70%%"><stop offset="0" stop-color="#fafaf8"/>'
            '<stop offset="1" stop-color="#c9c9c6"/></radialGradient>'
            '<filter id="glow" x="-1" y="-1" width="3" height="3"><feGaussianBlur stdDeviation="4"/></filter>'
            '</defs>%s</svg>' % (W, H, W, H, "".join(body)))


# Section rectangles per page (canvas coords) -- keep in sync with ../layout.conf.
LEFT, RIGHT, COL_W = 64, 660, 556
TOP, TOP_H, BOT, BOT_H = 186, 250, 468, 238
PAGES = {
    "voice": ("VOICE", [(LEFT, TOP, COL_W, TOP_H, "AMP"), (RIGHT, TOP, COL_W, TOP_H, "VOICES"),
                        (LEFT, BOT, COL_W, BOT_H, "PLAY"), (RIGHT, BOT, COL_W, BOT_H, "OUTPUT")]),
    "env": ("ENV", [(LEFT, TOP, COL_W, 520, "ENV 1 \u00b7 AMP IN ENV MODE"), (RIGHT, TOP, COL_W, 520, "ENV 2")]),
    "lfo": ("LFO", [(LEFT, TOP, COL_W, TOP_H, "LFO 1"), (RIGHT, TOP, COL_W, TOP_H, "LFO 2"),
                    (LEFT, BOT, COL_W, BOT_H, "CYCLE"), (RIGHT, BOT, COL_W, BOT_H, "RANDOM")]),
    "mod": ("MOD", [(LEFT, TOP, RIGHT + COL_W - LEFT, 520, "MOD MATRIX")]),
    "assign": ("ASSIGN", [(LEFT, TOP, RIGHT + COL_W - LEFT, 520, "ASSIGNABLE MATRIX")]),
}
GRID_COLS = [398, 634, 870, 1106]      # destination columns (canvas x)
GRID_ROWS = [296, 406, 516, 626]       # source rows (canvas y)
GRID_HEAD_Y, GRID_ROW_X = 222, 170     # column header row, row header centre
FIXED_DST = ["PITCH", "HARMONICS", "TIMBRE", "MORPH"]
FIXED_SRC = ["LFO 1", "ENV 2", "CYCLE", "RANDOM"]


def pill(cx, cy, text, w=None):
    """A dark legend tab with light text, centred on (cx, cy) canvas."""
    cy -= Y0
    w = w or 20 + 11.5 * len(text)
    return ('<rect x="%g" y="%g" width="%g" height="30" rx="15" fill="%s"/>'
            '<text x="%g" y="%g" text-anchor="middle" font-family="Helvetica Neue" font-weight="700" font-size="15" '
            'letter-spacing="1.2" fill="#F4F4F2">%s</text>' % (cx - w / 2, cy - 15, w, INK, cx, cy + 5.5, text))


def grid(fixed):
    """Matrix page: destinations across the top, sources down the side, dotted lines between the columns."""
    o = []
    for cx in GRID_COLS[:-1]:
        mx = cx + (GRID_COLS[1] - GRID_COLS[0]) / 2
        o.append('<path d="M%g %g V%g" stroke="%s" stroke-width="1.5" stroke-dasharray="2 6"/>' % (mx, GRID_HEAD_Y + 26 - Y0, 700 - Y0, BAND))
    if fixed:
        o += [pill(cx, GRID_HEAD_Y, t) for cx, t in zip(GRID_COLS, FIXED_DST)]
        o += [pill(GRID_ROW_X, cy - 6, t, 170) for cy, t in zip(GRID_ROWS, FIXED_SRC)]
    o.append('<text x="%g" y="%g" text-anchor="middle" font-family="Helvetica Neue" font-weight="700" font-size="13" '
             'letter-spacing="1.2" fill="%s">SOURCE \u2193  DEST \u2192</text>' % (GRID_ROW_X, GRID_HEAD_Y + 5 - Y0, INK_DIM))
    return o


def page(name):
    title, secs = PAGES[name]
    o = page_base(title)
    o += [section(*sec) for sec in secs]
    if name == "env":
        o += [env_guides(LEFT), env_guides(RIGHT)]
    if name in ("mod", "assign"):
        o += grid(name == "mod")
    return svg_page(o)


def background():
    o = page_base()
    for i, gx in enumerate(GRID_X):
        o.append(icon(gx, TEAL_Y, 17, P_TEAL_X, P_ROWS[i]))
        o.append(icon(gx, CORAL_Y, 17, P_CORAL_X, P_ROWS[i]))
        o.append(led(gx, LED_Y - Y0))
    px, py = POPUP
    o.append(icon(px - 240, py, 14, *P_WRENCH))
    o.append(icon(px + 240, py, 14, *P_DOTS))
    # attenuverter -/+ marks and centre tick, as on the panel
    for ax in ATT.values():
        y = ATT_Y - ATT_R - 30 - Y0
        o.append('<rect x="%g" y="%g" width="12" height="3.4" fill="%s"/>' % (ax - 34, y - 1.7, INK))
        o.append('<path d="M%g %g h12 M%g %g v12" stroke="%s" stroke-width="3.4"/>' % (ax + 22, y, ax + 28, y - 6, INK))
        o.append('<rect x="%g" y="%g" width="3.4" height="18" fill="%s"/>' % (ax - 1.7, y - 6, INK))
    # dotted connectors: TIMBRE -> its attenuverter (LPG COLOR), MORPH -> DECAY, MODEL -> FM
    tx, ty = TIMBRE
    cx = ATT["lpg_color"]
    o.append(dotted("M%g %g C %g %g, %g %g, %g %g" % (tx + KNOB_S_R + 10, ty - Y0, tx + 150, ty - Y0,
                                                      cx - 120, ATT_Y - Y0, cx - ATT_R - 10, ATT_Y - Y0)))
    mx, my = MORPH
    dx = ATT["lpg_decay"]
    o.append(dotted("M%g %g C %g %g, %g %g, %g %g" % (mx - KNOB_S_R - 10, my - Y0, mx - 150, my - Y0,
                                                      dx + 120, ATT_Y - Y0, dx + ATT_R + 10, ATT_Y - Y0)))
    o.append(dotted("M640 %g V %g" % (py + 36 - Y0, ATT_Y - ATT_R - 48 - Y0)))
    # OUT / AUX box around the mix knob, words taken from the panel
    ax, ay, ar = AUX
    # ends just above MPC's own name label (it sits at knob cy + r + 2)
    o.append('<rect x="%g" y="%g" width="240" height="%g" rx="10" fill="%s"/>' % (ax - 120, ay - ar - 12 - Y0, 2 * ar + 13, BOX))
    for (vx, vy, vw, vh), wx in ((P_OUT, ax - 98), (P_AUX, ax + 44)):
        o.append(knockout(piece(wx, ay - 14 - Y0, 56, 28, vx, vy, vw, vh)))
    return svg_page(o)


SHADOW = ('<defs><filter id="s" x="-0.3" y="-0.3" width="1.6" height="1.6"><feGaussianBlur stdDeviation="4"/></filter></defs>'
          '<circle cx="104" cy="108" r="88" fill="#000" opacity="0.38" filter="url(#s)"/>')


def svg(body, size=200):
    return '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 200 200">%s</svg>' % (size, size, body)


def big_knob():
    """VCV/Rogan-style: dark ribbed skirt, white cap, white index notch at the top."""
    ribs = "".join('<rect x="96" y="12" width="8" height="34" rx="3" fill="#161616" transform="rotate(%d 100 100)"/>' % a
                   for a in range(30, 360, 60))
    return svg('<defs><radialGradient id="k" cx="45%" cy="35%" r="70%"><stop offset="0" stop-color="#4a4a4a"/>'
               '<stop offset="1" stop-color="#1c1c1c"/></radialGradient>'
               '<radialGradient id="c" cx="42%" cy="35%" r="75%"><stop offset="0" stop-color="#ffffff"/>'
               '<stop offset="0.7" stop-color="#f1f1ef"/><stop offset="1" stop-color="#cfcfcc"/></radialGradient></defs>'
               '<circle cx="100" cy="100" r="90" fill="url(#k)" stroke="#0e0e0e" stroke-width="2"/>' + ribs +
               '<circle cx="100" cy="100" r="62" fill="#111"/>'
               '<circle cx="100" cy="100" r="58" fill="url(#c)"/>'
               '<rect x="92" y="4" width="16" height="36" rx="4" fill="#f4f4f2" stroke="#9c9c99" stroke-width="1.5"/>')


def small_knob():
    """Black trimmer-style knob with a white line, as the panel's attenuverters."""
    return svg('<defs><radialGradient id="k" cx="42%" cy="32%" r="75%"><stop offset="0" stop-color="#505050"/>'
               '<stop offset="0.6" stop-color="#1e1e1e"/><stop offset="1" stop-color="#0a0a0a"/></radialGradient></defs>'
               '<circle cx="100" cy="100" r="90" fill="url(#k)" stroke="#000" stroke-width="2"/>'
               '<rect x="93" y="10" width="14" height="62" rx="5" fill="#f4f4f2"/>')


def shadow():
    return svg(SHADOW)


def toggle_led(lit):
    """On/off switches as the panel's LEDs, lit in the skin's teal."""
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40" viewBox="0 0 40 40"><defs>'
            '<filter id="glow" x="-1" y="-1" width="3" height="3"><feGaussianBlur stdDeviation="3"/></filter></defs>'
            '%s</svg>' % led(20, 20, TEAL if lit else None).replace("r=\"15\"", "r=\"13\""))


def model_button(step, pressed):
    """The module's round model buttons, one either side of the MODEL popup, with a small step arrow."""
    tri = "M78 100 L118 76 L118 124 Z" if step < 0 else "M122 100 L82 76 L82 124 Z"
    return svg('<defs><radialGradient id="b" cx="42%%" cy="35%%" r="70%%"><stop offset="0" stop-color="%s"/>'
               '<stop offset="1" stop-color="#141414"/></radialGradient></defs>'
               '<circle cx="100" cy="100" r="94" fill="url(#b)" stroke="#000" stroke-width="3"/>'
               '<path d="%s" fill="#F4F4F2"/>' % ("#6a6a6a" if pressed else "#3e3e3e", tri), 104)


# visible model index (module.json "model" options order) -> (LED column, colour): the real module shows
# the bank with the LED colour -- here amber for the newer engines, green for the teal column, red for coral.
AMBER, GREEN, RED = "#FFB21E", "#57D63A", "#FF3434"
MODEL_LEDS = [(i, AMBER) for i in range(8)] + [(i, GREEN) for i in range(8)] + [(i, RED) for i in range(8)]
LEDS_W, LEDS_H = 528, 40


def led_picture(col, colour):
    body = "".join(led(33 + i * 66, 20, colour if i == col else None) for i in range(8))
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d"><defs>'
            '<filter id="glow" x="-1" y="-1" width="3" height="3"><feGaussianBlur stdDeviation="4"/></filter></defs>%s</svg>'
            % (LEDS_W, LEDS_H, LEDS_W, LEDS_H, body))


def main():
    out = os.path.join(HERE, "..", "images")
    os.makedirs(out, exist_ok=True)
    bg = os.path.join(HERE, "bg.svg")
    open(bg, "w").write(background())
    subprocess.check_call(["rsvg-convert", "-w", str(W), "-h", str(H), bg, "-o", os.path.join(out, "bg.png")])
    tmp = os.path.join(HERE, "page.svg")
    for name in PAGES:
        open(tmp, "w").write(page(name))
        subprocess.check_call(["rsvg-convert", "-w", str(W), "-h", str(H), tmp, "-o", os.path.join(out, "bg_%s.png" % name)])
    os.remove(tmp)
    for name, s in (("knob_big.svg", big_knob()), ("knob_small.svg", small_knob()), ("knob_shadow.svg", shadow()),
                    ("model_prev.svg", model_button(-1, False)), ("model_prev_on.svg", model_button(-1, True)),
                    ("model_next.svg", model_button(1, False)), ("model_next_on.svg", model_button(1, True)),
                    ("led_off.svg", toggle_led(False)), ("led_on.svg", toggle_led(True))):
        open(os.path.join(out, name), "w").write(s)
    for i, (col, colour) in enumerate(MODEL_LEDS):
        open(os.path.join(out, "led_%02d.svg" % i), "w").write(led_picture(col, colour))
    for layer in ("attack", "decay", "release", "sustain", "below", "above"):
        open(tmp, "w").write(env_strip(layer))
        subprocess.check_call(["rsvg-convert", tmp, "-o", os.path.join(out, "env_%s.png" % layer)])
    os.remove(tmp)
    print("wrote", out)


if __name__ == "__main__":
    main()
