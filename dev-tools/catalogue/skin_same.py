#!/usr/bin/env python3
"""Is a plugin's new skin the one in its deploy/ folder? Compares by content, so renamed files and component keys
(newer tools name filmstrips <name>_f<frames>.png and knobs ..._ns11_vs16_bw91, and share identical pop-up option
images) don't count as differences:
    python3 dev-tools/catalogue/skin_same.py <plugin folder> [<max differences shown>]
Old: <folder>/deploy/Synths/*/Plugin Skins. New: <folder>/build/skin/*/Plugin Skins. Images are compared by their
pixels when Pillow is there (check.sh runs this in the mpc-vst-html-art image): a re-encoded PNG, or one whose
anti-aliasing moved by a level or two (mean difference under 0.5 of 255), is the same image. Without Pillow, by bytes.
Prints each JSON file as "same" or its differences (path | old -> new) and the images only on one side; exit 0 when
all is the same."""
import glob
import hashlib
import json
import os
import re
import sys


def digest(path):
    return hashlib.md5(open(path, "rb").read()).hexdigest()


def image_ids(A, B):
    """{old name: id}, {new name: id}: equal ids for the same picture. By pixels with Pillow (exact, else the closest
    same-size new image within a mean difference of 0.5 per channel), else by file bytes."""
    old = {f: digest(os.path.join(A, f)) for f in os.listdir(A) if not f.endswith(".json")}
    new = {f: digest(os.path.join(B, f)) for f in os.listdir(B) if not f.endswith(".json")}
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return old, new
    def load(d, f):
        try:
            return Image.open(os.path.join(d, f)).convert("RGBA")
        except Exception:
            return None
    nim = {f: load(B, f) for f in new}
    by_pixels = {}
    for f, im in nim.items():
        if im is not None:
            new[f] = by_pixels.setdefault((im.size, hashlib.md5(im.tobytes()).hexdigest()), new[f])
    for f in old:
        im = load(A, f)
        if im is None:
            continue
        key = (im.size, hashlib.md5(im.tobytes()).hexdigest())
        if key in by_pixels:
            old[f] = by_pixels[key]
            continue
        best = None
        for g, j in nim.items():
            if j is not None and j.size == im.size:
                hist = ImageChops.difference(im, j).histogram()   # 4 x 256 bins
                mean = sum((i % 256) * n for i, n in enumerate(hist)) / (im.size[0] * im.size[1] * 4.0)
                if best is None or mean < best[0]:
                    best = (mean, g)
        if best and best[0] < 0.5:
            old[f] = new[best[1]]
    return old, new


def norm_key(s):
    """A newer tool's component/filmstrip name -> the older one's (same component, new naming)."""
    s = re.sub(r"_ns(\d+)", r"_n\1", s)
    s = re.sub(r"_vs(\d+)", r"v\1", s)
    s = re.sub(r"_bw(\d+)", r"_b\1", s)
    s = re.sub(r"_f\d+(\.png)?$", r"\1", s)
    return s


def canon(o, images):
    """JSON with image names replaced by their content hash and keys normalised."""
    if isinstance(o, dict):
        return {k: canon(v, images) for k, v in o.items()}
    if isinstance(o, list):
        return [canon(v, images) for v in o]
    if isinstance(o, str):
        return "IMG:" + images[o] if o in images else norm_key(o)
    return o


def walk(a, b, path=""):
    if type(a) is not type(b):
        return [(path, a, b)]
    if isinstance(a, dict):
        out = []
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                out.append((path + "/" + k, a.get(k, "<missing>"), b.get(k, "<missing>")))
            else:
                out += walk(a[k], b[k], path + "/" + k)
        return out
    if isinstance(a, list):
        out = [(path + " len", len(a), len(b))] if len(a) != len(b) else []
        for i, (x, y) in enumerate(zip(a, b)):
            out += walk(x, y, "%s[%d]" % (path, i))
        return out
    return [] if a == b else [(path, a, b)]


def main():
    d = sys.argv[1]
    show = int(sys.argv[2]) if len(sys.argv) > 2 else 15
    old = glob.glob(os.path.join(d, "deploy", "Synths", "*", "Plugin Skins"))
    new = glob.glob(os.path.join(d, "build", "skin", "*", "Plugin Skins"))
    if len(old) != 1 or len(new) != 1:
        raise SystemExit("%s: need one skin in deploy/Synths and one in build/skin" % d)
    A, B = old[0], new[0]
    ia, ib = image_ids(A, B)
    same = True
    for label, mine, other in (("old", ia, ib), ("new", ib, ia)):
        for f, h in sorted(mine.items()):
            if h not in set(other.values()):
                print("image only in %s: %s" % (label, f))
                same = False
    for f in sorted(x for x in os.listdir(A) if x.endswith(".json")):
        if not os.path.exists(os.path.join(B, f)):
            print("%s: missing in the new skin" % f)
            same = False
            continue
        diffs = walk(canon(json.load(open(os.path.join(A, f))), ia), canon(json.load(open(os.path.join(B, f))), ib))
        print("%s: %s" % (f, "same" if not diffs else "%d differences" % len(diffs)))
        for p, x, y in diffs[:show]:
            print("   %s | %s -> %s" % (p[-100:], json.dumps(x)[:80], json.dumps(y)[:80]))
        same = same and not diffs
    sys.exit(0 if same else 1)


if __name__ == "__main__":
    main()
