"""Envelope displays for an MPC skin that follow their ADSR knobs (runs where Pillow is: the mpc-vst-html-art container).
   python3 envelope.py <spec.json>      spec: {"out": dir, "name": id, "w": W, "h": H, "color": "#rrggbb",
                                               "frames": 32, "bands": 11, "stages": 4}
Drawn the classic way: straight segments whose corners the knobs move. Attack rises from the floor to the peak, decay
falls to the sustain level, sustain holds, release falls to the floor; a dot marks each corner.

A display is four columns, A | D | S | R. Each is a filmstrip `meter` bound to its own parameter, so turning a knob
redraws its stage (MPC redraws whatever is bound to a parameter it changes); a stage's width stands for its time
(normalized knob position). The sustain LEVEL is shared by D, S and R, so those three come once per sustain band
(when=<sustain>:<b>/<bands>, MPC's IndexedEnabling on a continuous parameter): the three switch together, so the
segments always meet. 11 bands = the 0.0..1.0 readout's own steps.

Writes <out>/env_<name>_a.png and env_<name>_{d,s,r}<b>.png (frames stacked down, transparent; the S strips are four
identical frames, a picture shown per band) and prints the column width.
History: the first version (2026-10-02) drew exponential curves with a filled area and one S strip; on the device the
fill read as grey blocks and S could disagree with D/R by a band. See steve/tools/stitch/README.md."""
import json, os, sys
from PIL import Image, ImageDraw, ImageFilter

spec = json.load(open(sys.argv[1]))
W, H = int(spec["w"]), int(spec["h"])
F, B = int(spec.get("frames", 32)), int(spec.get("bands", 11))
N = int(spec.get("stages", 4))   # 3: an ADS envelope (no release), A | D | S
cw = W // N
col = tuple(int(spec["color"].lstrip("#")[i:i + 2], 16) for i in (0, 2, 4))
S = 4                       # supersampling
yt, yb = 0.18 * H, 0.84 * H  # peak and floor
x0, x1 = 3.0, cw - 3.0       # a stage's time runs from x0 (0) to x1 (1); the rest of the column holds its end level


def frame(points, dots=()):
    """One column frame: the segment polyline with a soft glow, and white corner dots."""
    big = (cw * S, H * S)
    pts = [(x * S, y * S) for x, y in points]
    glow = Image.new("RGBA", big, (0, 0, 0, 0))
    ImageDraw.Draw(glow).line(pts, fill=col + (120,), width=7 * S, joint="curve")
    glow = glow.filter(ImageFilter.GaussianBlur(3 * S))
    line = Image.new("RGBA", big, (0, 0, 0, 0))
    d = ImageDraw.Draw(line)
    d.line(pts, fill=col + (255,), width=int(2.5 * S), joint="curve")
    for x, y in dots:
        r = 3.6 * S
        d.ellipse([x * S - r, y * S - r, x * S + r, y * S + r], fill=(255, 255, 255, 255), outline=col + (255,),
                  width=S)
    return Image.alpha_composite(glow, line).resize((cw, H), Image.LANCZOS)


def segment(frm, to, t):
    """A straight segment from level frm to level to, taking t (0..1) of the column, then holding at to."""
    xe = x0 + (x1 - x0) * t
    return [(0, frm), (xe, to), (cw, to)], [(xe, to)]


def strip(name, frames):
    im = Image.new("RGBA", (cw, H * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        im.paste(f, (0, i * H))
    im.save(os.path.join(spec["out"], "env_%s_%s.png" % (spec["name"], name)), optimize=True)


level = lambda s: yb - (yb - yt) * s
strip("a", [frame(*segment(yb, yt, i / (F - 1))) for i in range(F)])
for b in range(B):
    s = level(b / (B - 1))
    strip("d%d" % b, [frame(*segment(yt, s, i / (F - 1))) for i in range(F)])
    if N == 4:
        strip("r%d" % b, [frame(*segment(s, yb, i / (F - 1))) for i in range(F)])
    held = frame([(0, s), (cw, s)])
    strip("s%d" % b, [held] * 4)   # 4 frames: a strip must be taller than wide to read as stacked down
print(cw)
