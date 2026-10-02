"""Live dot-scope artwork and layout lines (steve/tools/stitch, 2026-10-02; runs where Pillow is: the
mpc-vst-html-art container).
   python3 scope.py <port> --x X --y Y --w W --h H [--columns 48] [--levels 32] [--first scope_01]
The scope is COLUMNS display-only parameters the wrapper sets from the plugin's own output (vst.json "scope":
{"param": <first>, "columns": N, "levels": L}), one `meter` each across the box. Writes
  images/waves/scope_bg.png   the box: theme_lcd background and theme_line grid (drawn into the page background)
  images/waves/scope_dot.png  one strip shared by every column: L frames, frame k a glowing dot at height k
                              (0 = bottom), in theme_accent_hi
and prints the layout lines (one `art` and COLUMNS `meter`s) to put where the box was.
History: the first version scrolled a recorded OSC 1 wave, so it never showed the real sound (the filter, the other
oscillators); the user asked for a real scope."""
import argparse, os, re
from PIL import Image, ImageDraw, ImageFilter

ap = argparse.ArgumentParser()
ap.add_argument("port")
for k in ("x", "y", "w", "h"):
    ap.add_argument("--" + k, type=int, required=True)
ap.add_argument("--columns", type=int, default=48)
ap.add_argument("--levels", type=int, default=32)
ap.add_argument("--first", default="scope_01")
a = ap.parse_args()

STEVE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
D = os.path.join(STEVE, "schwung-ports", a.port)
theme = dict(re.findall(r"^theme_(\w+)=(\w+)", open(os.path.join(D, "layout.conf")).read(), re.M))
rgb = lambda h: tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
accent = rgb(theme.get("accent_hi", theme.get("accent", "4fe6ff")))
lcd, grid = rgb(theme.get("lcd", "05070a")), rgb(theme.get("line", "2a3038"))
W, H, N, L, S = a.w, a.h, a.columns, a.levels, 4
os.makedirs(os.path.join(D, "images", "waves"), exist_ok=True)

bg = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
d = ImageDraw.Draw(bg)
d.rounded_rectangle([0, 0, W * S - 1, H * S - 1], radius=6 * S, fill=lcd + (255,))
for x in range(0, W, W // 8):
    d.line([(x * S, 0), (x * S, H * S)], fill=grid + (255,), width=S)
for y in (H // 4, H // 2, 3 * H // 4):
    d.line([(0, y * S), (W * S, y * S)], fill=grid + (255,), width=S)
bg.resize((W, H), Image.LANCZOS).save(os.path.join(D, "images", "waves", "scope_bg.png"))

cw = W // N                     # a column's width; the meters sit at their exact centres across the box
pad = 8
strip = Image.new("RGBA", (cw, H * L), (0, 0, 0, 0))
for k in range(L):
    y = pad + (H - 2 * pad) * (1 - k / (L - 1))
    f = Image.new("RGBA", (cw * S, H * S), (0, 0, 0, 0))
    g = Image.new("RGBA", f.size, (0, 0, 0, 0))
    r = min(cw * 0.5, 4.2)   # 96 columns: 5 px wide, dots touching
    ImageDraw.Draw(g).ellipse([(cw / 2 - r * 1.8) * S, (y - r * 1.8) * S, (cw / 2 + r * 1.8) * S, (y + r * 1.8) * S],
                              fill=accent + (110,))
    f = Image.alpha_composite(f, g.filter(ImageFilter.GaussianBlur(2.0 * S)))
    dot = Image.new("RGBA", f.size, (0, 0, 0, 0))
    ImageDraw.Draw(dot).ellipse([(cw / 2 - r) * S, (y - r) * S, (cw / 2 + r) * S, (y + r) * S], fill=accent + (255,))
    f = Image.alpha_composite(f, dot).resize((cw, H), Image.LANCZOS)
    strip.paste(f, (0, k * H))
strip.save(os.path.join(D, "images", "waves", "scope_dot.png"), optimize=True)

base, num = re.match(r"(.*?)(\d+)$", a.first).groups()
print("# live scope: %d columns the wrapper sets from the plugin's output (vst.json \"scope\"; steve/tools/stitch/scope.py)" % N)
print("art file=images/waves/scope_bg.png x=%d y=%d w=%d h=%d" % (a.x, a.y, W, H))
for c in range(N):
    cx = round(a.x + (c + 0.5) * W / N)
    print("meter cx=%d cy=%d w=%d h=%d key=%s%0*d strip=images/waves/scope_dot.png frames=%d"
          % (cx, a.y + H // 2, cw, H, base, len(num), int(num) + c, L))
