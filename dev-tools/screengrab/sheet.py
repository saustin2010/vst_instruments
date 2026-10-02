"""Raw drmgrab frames -> one contact sheet (thumbnails with their names). python3 sheet.py out.png cols raw...
(steve/tools/screengrab, 2026-10-02)"""
import os, sys
from PIL import Image, ImageDraw
out, cols, raws = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
tw, th = 426, 266
sheet = Image.new("RGB", (cols * tw, -(-len(raws) // cols) * (th + 16)), (30, 30, 30))
d = ImageDraw.Draw(sheet)
for i, r in enumerate(raws):
    data = open(r, "rb").read(); nl = data.index(b"\n")
    w, h, pitch = (int(x) for x in data[:nl].split()[:3])
    im = Image.frombuffer("RGBA", (w, h), data[nl + 1:], "raw", "BGRA", pitch, 1).convert("RGB").rotate(270, expand=True)
    x, y = (i % cols) * tw, (i // cols) * (th + 16)
    sheet.paste(im.resize((tw - 4, th - 4)), (x + 2, y + 2))
    d.text((x + 4, y + th), os.path.basename(r), fill=(255, 255, 0))
sheet.save(out); print(out)
