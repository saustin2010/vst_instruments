"""drmgrab output ("w h pitch bpp depth plane" line, then pixels) -> PNG, rotated to the landscape view the MPC
shows (its panel is 800 x 1280 portrait).  python3 rawpng.py shot.raw out.png [rot]   (rot: 270 default)"""
import sys
from PIL import Image
data = open(sys.argv[1], "rb").read()
nl = data.index(b"\n")
w, h, pitch, bpp, depth, plane = (int(x) for x in data[:nl].split())
px = data[nl + 1:]
im = Image.frombuffer("RGBA", (w, h), px, "raw", "BGRA", pitch, 1).convert("RGB")
rot = int(sys.argv[3]) if len(sys.argv) > 3 else 270
im = im.rotate(rot, expand=True)
im.save(sys.argv[2]); print(w, h, "->", im.size, sys.argv[2])
