#!/usr/bin/env python3
"""Percolator's envelope displays: filmstrips MPC shows as `meter`s, one frame per knob position, so a display
redraws when its knob turns (nothing animates). Used by mpc/skin.py (in the mpc-vst-html-art container, which has
Pillow): strip({"name": id, "kind": "decay" | "attack" | "pitch", "w": W, "h": H, "color": "#rrggbb",
"attack": true}, frames) returns the filmstrip, frames stacked down, transparent.

decay   the amplitude envelope: a short attack (when "attack" is set; voice 4 has its own ATTACK display instead),
        then an exponential fall whose length follows DECAY; the top frame is DRONE (a held level).
attack  voice 4's ATTACK: the rise from the floor to the peak, then held, so it meets the decay display to its right.
pitch   voices 1-2's PITCH ENV (PARAM 2): how far the pitch starts above the note (depth^1.3, as the engine), falling
        back quickly; drawn thinner, in a lighter tint of the voice colour.
"""
import math
from PIL import Image, ImageDraw, ImageFilter

S = 4   # supersampling


def rgb(h):
    return tuple(int(h.lstrip("#")[i:i + 2], 16) for i in (0, 2, 4))


def tint(c, k):
    return tuple(int(v + (255 - v) * k) for v in c)


def draw(w, h, pts, col, width=2.6, glow=7, dots=(), dash=False):
    """One frame: the polyline with a soft glow in its colour, white dots at its corners."""
    big = (w * S, h * S)
    P = [(x * S, y * S) for x, y in pts]
    g = Image.new("RGBA", big, (0, 0, 0, 0))
    ImageDraw.Draw(g).line(P, fill=col + (150,), width=glow * S, joint="curve")
    g = g.filter(ImageFilter.GaussianBlur(3 * S))
    ln = Image.new("RGBA", big, (0, 0, 0, 0))
    d = ImageDraw.Draw(ln)
    if dash:   # dashes along the path, by length
        seg, on, acc = [], True, 0.0
        for (x0, y0), (x1, y1) in zip(P, P[1:]):
            n = max(1, int(math.hypot(x1 - x0, y1 - y0) / S))
            for i in range(n):
                a = (x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n)
                b = (x0 + (x1 - x0) * (i + 1) / n, y0 + (y1 - y0) * (i + 1) / n)
                if on:
                    d.line([a, b], fill=col + (255,), width=int(width * S))
                acc += 1
                if acc >= (7 if on else 5):
                    on, acc = not on, 0.0
    else:
        d.line(P, fill=col + (255,), width=int(width * S), joint="curve")
    for x, y in dots:
        r = 3.8 * S
        d.ellipse([x * S - r, y * S - r, x * S + r, y * S + r], fill=(255, 255, 255, 255), outline=col + (255,),
                  width=S)
    return Image.alpha_composite(g, ln).resize((w, h), Image.LANCZOS)


def frames(ds, F):
    w, h, col = int(ds["w"]), int(ds["h"]), rgb(ds["color"])
    yt, yb = 0.16 * h, 0.86 * h      # peak and floor
    x0, x1 = 4.0, w - 6.0            # the knob's travel maps onto x0..x1
    out = []
    for i in range(F):
        t = i / (F - 1)
        if ds["kind"] == "decay":
            xa = x0 + (0.05 * (x1 - x0) if ds.get("attack") else 0)
            start = [(0, yb), (xa, yt)] if ds.get("attack") else [(0, yt)]
            if t >= 0.985:   # DRONE: held for as long as the note
                out.append(draw(w, h, start + [(w, yt)], col, dots=[(xa, yt)]))
                continue
            xe = xa + (x1 - xa) * max(t, 0.02)
            curve = [(xa + (xe - xa) * k / 40, yb - (yb - yt) * math.exp(-5.0 * k / 40)) for k in range(41)]
            out.append(draw(w, h, start + curve + [(w, yb)], col, dots=[(xa, yt), (xe, yb)]))
        elif ds["kind"] == "attack":
            xe = x0 + (x1 - x0) * max(t, 0.02)
            # the engine's attack is a fast-then-slow rise; draw it as an exponential approach
            rise = [(x0 + (xe - x0) * k / 30, yb - (yb - yt) * (1 - math.exp(-4.0 * k / 30)) / (1 - math.exp(-4.0)))
                    for k in range(31)]
            out.append(draw(w, h, [(0, yb)] + rise + [(w, yt)], col, dots=[(xe, yt)]))
        else:   # pitch
            depth = t ** 1.3
            y0 = yb - (yb - yt) * depth
            xe = x0 + 0.45 * (x1 - x0)
            fall = [(x0 + (xe - x0) * k / 30, yb - (yb - y0) * math.exp(-5.0 * k / 30)) for k in range(31)]
            out.append(draw(w, h, [(0, y0)] + fall + [(w, yb)], tint(col, 0.55), width=2.0, glow=5,
                            dots=[(x0, y0)] if depth > 0.01 else (), dash=True))
    return w, h, out


def strip(ds, F):
    w, h, fr = frames(ds, F)
    im = Image.new("RGBA", (w, h * F), (0, 0, 0, 0))
    for i, f in enumerate(fr):
        im.paste(f, (0, i * h))
    return im
