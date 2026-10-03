"""Draw each page's Q-Link column outlines on its screenshot, as a 4-knob MPC highlights them (steve/tools/stitch).
   python3 steve/tools/stitch/qlink_overlay.py <plugin dir> [...]        (Pillow: run it in the mpc-vst-html-art image)
Reads the built skin (deploy/Synths/*/Plugin Skins/TUI.json: each page's qlinkBoundsData, one "x y w h" box per
column, in screenshot coordinates) and screenshots/page_<n>.png, and writes build/qlinks/page_<n>[_<bank>].png:
column 1-4 boxes in four colours, numbered, so a column whose outline takes in another panel shows at a glance.
A page with more than 16 Q-Links has one image per bank. Prints any two columns whose boxes overlap."""
import glob, json, os, sys
from PIL import Image, ImageDraw

COLS = [(255, 64, 64), (64, 200, 255), (255, 200, 40), (120, 255, 120)]


def overlap(a, b):
    w = min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0])
    h = min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1])
    return max(0, w) * max(0, h)


def main():
    for d in sys.argv[1:]:
        tui = glob.glob(os.path.join(d, "deploy", "Synths", "*", "Plugin Skins", "TUI.json"))
        if not tui:
            print("%s: no built skin in deploy/" % d)
            continue
        out = os.path.join(d, "build", "qlinks")
        os.makedirs(out, exist_ok=True)
        for tab in json.load(open(tui[0]))["pageData"]["tabs"]:
            n, sub = tab["fnKeyIndex"], tab.get("fnKeySubIndex", 0)
            shot = os.path.join(d, "screenshots", "page_%d.png" % n)
            if not os.path.exists(shot):
                continue
            im = Image.open(shot).convert("RGB")
            dr = ImageDraw.Draw(im)
            boxes = [tuple(int(v) for v in b.split()) for b in tab.get("qlinkBoundsData", [])]
            for c, (x, y, w, h) in enumerate(boxes):
                if w <= 0 or h <= 0:
                    continue
                col = COLS[c % 4]
                for k in range(3):   # 3 px, inset by column so touching outlines stay apart
                    dr.rectangle([x + k + c, y + k + c, x + w - k - c, y + h - k - c], outline=col)
                dr.rectangle([x + c, y + c, x + c + 26, y + c + 22], fill=col)
                dr.text((x + c + 8, y + c + 5), str(c + 1), fill=(0, 0, 0))
            for i in range(len(boxes)):
                for j in range(i + 1, len(boxes)):
                    if boxes[i][2] > 0 and boxes[j][2] > 0 and overlap(boxes[i], boxes[j]) > 0:
                        print("%s %s: columns %d and %d overlap" % (os.path.basename(d), tab["tabName"], i + 1, j + 1))
            name = "page_%d%s.png" % (n, "_%d" % (sub + 1) if sub else "")
            im.save(os.path.join(out, name))
        print("%s: %s" % (os.path.basename(d), out))


if __name__ == "__main__":
    main()
