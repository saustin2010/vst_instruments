#!/usr/bin/env python3
"""Stevequencer's PNG pictures (Pillow, Titillium Web from the framework's renderer fonts), into images/:
    hd/NN_off.png, hd/NN_on.png      each step's header (its on/off toggle): number + ON/OFF pill, 238 x 36
    hd/set_<key>_off/on.png          SETUP's toggle headers (KEY TRANSP, STEP LIGHT, AUDITION)
    arc.png                          the value arc, 128 frames (a knob filmstrip); blank.png the same, empty
    btn.png, btn_on.png              tool buttons (the label is drawn on top by the skin builder)
Run in the mpc-vst-html-art image from the repo root (it has Pillow):
    docker run --rm -v "$PWD:$PWD" -w "$PWD" mpc-vst-html-art python3 originals/stevequencer/mpc/make_png.py
Everything is drawn at 2x and scaled down, for smooth edges."""
import math
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(os.path.dirname(HERE))
FONTS = os.path.join(REPO, "framework", "mpc-vst-plugins", "tools", "html_art", "fonts")
IMG = os.path.join(HERE, "images")
S = 2   # supersampling
W, H = 238, 36
AMBER, AMBER_INK, OFF_BG, OFF_INK, PILL_OFF = "#ffb020", "#1d1404", "#22262c", "#5f6873", "#2d3238"


def font(weight, px):
    return ImageFont.truetype(os.path.join(FONTS, "TitilliumWeb-%s.ttf" % weight), px * S)


def header(left, on, right=None, pill=True, left_size=18, left_ink=None):
    im = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([S, S, (W - 1) * S, (H + 12) * S], radius=9 * S, fill=AMBER if on else OFF_BG)   # top corners round
    im = im.crop((0, 0, W * S, H * S))
    d = ImageDraw.Draw(im)
    f = font("Bold", left_size)
    d.text((12 * S, H * S / 2), left, font=f, fill=(AMBER_INK if on else (left_ink or OFF_INK)), anchor="lm")
    if pill:
        pw, ph = 56, 22
        x0, y0 = W - 12 - pw, (H - ph) / 2
        d.rounded_rectangle([x0 * S, y0 * S, (x0 + pw) * S, (y0 + ph) * S], radius=ph * S / 2, fill=AMBER_INK if on else PILL_OFF)
        d.text(((x0 + pw / 2) * S, (y0 + ph / 2) * S), "ON" if on else "OFF", font=font("Bold", 14),
               fill=AMBER if on else OFF_INK, anchor="mm")
    if right:
        d.text(((W - 12) * S, H * S / 2), right, font=font("Bold", 12), fill=AMBER_INK if on else OFF_INK, anchor="rm")
    return im.resize((W, H), Image.LANCZOS)


def arc_strip(frames=128, size=58, empty=False):
    n = size * S
    strip = Image.new("RGBA", (n, n * frames), (0, 0, 0, 0))
    if not empty:
        for i in range(frames):
            fr = Image.new("RGBA", (n, n), (0, 0, 0, 0))
            d = ImageDraw.Draw(fr)
            pad, wid = 6 * S, 6 * S
            box = [pad, pad, n - pad, n - pad]
            d.arc(box, 135, 405, fill="#2b3037", width=wid)
            t = i / (frames - 1)
            if t > 0.004:
                d.arc(box, 135, 135 + 270 * t, fill=AMBER, width=wid)
            for a in (135, 135 + 270 * t) if t > 0.004 else (135,):   # round ends
                r = (n - 2 * pad) / 2 - wid / 2
                cx, cy = n / 2 + r * math.cos(math.radians(a)), n / 2 + r * math.sin(math.radians(a))
                d.ellipse([cx - wid / 2, cy - wid / 2, cx + wid / 2, cy + wid / 2], fill=AMBER if t > 0.004 else "#2b3037")
            strip.paste(fr, (0, i * n))
    return strip.resize((size, size * frames), Image.LANCZOS)


def button(on, w=104, h=56):
    im = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([S, S, (w - 1) * S, (h - 1) * S], radius=8 * S, fill=AMBER if on else "#23272d",
                        outline=AMBER if on else "#3a4049", width=S)
    return im.resize((w, h), Image.LANCZOS)


def main():
    os.makedirs(os.path.join(IMG, "hd"), exist_ok=True)
    for s in range(1, 65):
        for on in (0, 1):
            header("%02d" % s, on).save(os.path.join(IMG, "hd", "%02d_%s.png" % (s, "on" if on else "off")))
    for key, name in (("key_transpose", "KEY TRANSP"), ("step_light", "STEP LIGHT"), ("audition", "AUDITION")):
        for on in (0, 1):
            tag = "PITCH" if key == "key_transpose" else "OUTPUT"
            header(name, on, right=tag, pill=False, left_size=16, left_ink="#a3abb5").save(
                os.path.join(IMG, "hd", "set_%s_%s.png" % (key, "on" if on else "off")))
    arc_strip().save(os.path.join(IMG, "arc.png"))
    arc_strip(empty=True).save(os.path.join(IMG, "blank.png"))
    button(0).save(os.path.join(IMG, "btn.png"))
    button(1).save(os.path.join(IMG, "btn_on.png"))
    print("images: 128 step headers, 6 setup headers, arc.png, blank.png, btn.png, btn_on.png")


if __name__ == "__main__":
    main()
