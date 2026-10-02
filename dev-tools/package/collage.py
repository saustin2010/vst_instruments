"""A contact sheet of screenshots with names under them, for the repo README (run in the mpc-vst-html-art image):
    python3 collage.py <out.png> <columns> <font.ttf> <png> <label> [<png> <label> ...]"""
import sys
from PIL import Image, ImageDraw, ImageFont

out, cols, font = sys.argv[1], int(sys.argv[2]), sys.argv[3]
items = list(zip(sys.argv[4::2], sys.argv[5::2]))
TW, TH, GAP, LAB = 320, 157, 14, 30
rows = (len(items) + cols - 1) // cols
im = Image.new("RGB", (GAP + cols * (TW + GAP), GAP + rows * (TH + LAB + GAP)), (14, 15, 18))
d = ImageDraw.Draw(im)
f = ImageFont.truetype(font, 17)
for n, (png, label) in enumerate(items):
    x = GAP + (n % cols) * (TW + GAP)
    y = GAP + (n // cols) * (TH + LAB + GAP)
    im.paste(Image.open(png).convert("RGB").resize((TW, TH), Image.LANCZOS), (x, y))
    d.rectangle([x - 1, y - 1, x + TW, y + TH], outline=(48, 50, 56))
    w = d.textlength(label, font=f)
    d.text((x + (TW - w) / 2, y + TH + 5), label, font=f, fill=(214, 214, 220))
im.save(out, optimize=True)
