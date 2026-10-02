"""MPC screen grab: raw /dev/fb0 (800 x 3840, 32 bpp: an 800 x 1280 portrait panel, three buffers) -> PNGs, each
buffer rotated to the landscape view.  python3 fbshot.py fb0.raw out_prefix   (steve/tools/stitch, 2026-10-02)"""
import sys
from PIL import Image
raw = open(sys.argv[1], "rb").read()
W, H = 800, 1280
for b in range(len(raw) // (W * H * 4)):
    chunk = raw[b * W * H * 4:(b + 1) * W * H * 4]
    im = Image.frombytes("RGBA", (W, H), chunk, "raw", "BGRA").convert("RGB")
    im.rotate(90, expand=True).save("%s_%d.png" % (sys.argv[2], b))
    print("buffer", b, im.getextrema())
