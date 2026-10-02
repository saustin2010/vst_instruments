"""Stitch designs -> steve/stitch_layouts/<port>.md (+ stitch_png/<port>.png), named after the port they're for.
   python3 steve/tools/stitch/pull.py <list_screens.json> [--force] [port ...]
<list_screens.json> is the Stitch MCP's list_screens result for the project (Claude saves it when it lists the
screens; the download links in it last about an hour). A screen belongs to the port its title starts with ("MonkSynth
— Formant ..." -> monksynth; ALIAS for the rest); a screen whose title ends in "— page N" or "(page N)" is that port's
page N and goes to <port>.pN.md; a second design for the same page goes to <port>.v2.md. An existing <port>.md is kept (it may be hand-edited) unless --force; the new copy
goes to incoming/<port>.md instead. Every pull is recorded in stitch_layouts/index.json (port -> screen ids)."""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SL = os.path.join(os.path.dirname(os.path.dirname(HERE)), "stitch_layouts")
PORTS = os.path.join(os.path.dirname(SL), "schwung-ports")
ALIAS = {"raffo": "moog", "hushone": "hush1", "monovoicehelm": "monovoice"}


def port_of(title):
    head = re.split(r"\s+[—–-]\s+", title)[0]
    n = ALIAS.get(re.sub(r"[^a-z0-9]", "", head.lower()), re.sub(r"[^a-z0-9]", "", head.lower()))
    if not os.path.isdir(os.path.join(PORTS, n)):
        return None, None
    m = re.search(r"page\s*(\d+)\)?\s*$", title, re.I)
    return n, (int(m.group(1)) if m and m.group(1) != "1" else None)


def get(url, dst):
    subprocess.run(["curl", "-sfL", url, "-o", dst + ".new"], check=True)
    os.replace(dst + ".new", dst)


def main():
    args = sys.argv[1:]
    force = "--force" in args
    args = [a for a in args if a != "--force"]
    screens = json.load(open(args[0]))["screens"]
    only = set(args[1:])
    idx_f = os.path.join(SL, "index.json")
    idx = json.load(open(idx_f)) if os.path.exists(idx_f) else {}
    os.makedirs(os.path.join(SL, "stitch_png"), exist_ok=True)
    os.makedirs(os.path.join(SL, "incoming"), exist_ok=True)
    seen = {}
    for s in screens:
        if s.get("htmlCode", {}).get("mimeType") != "text/html":
            continue
        port, page = port_of(s.get("title", ""))
        if not port or (only and port not in only):
            continue
        name = port + (".p%d" % page if page else "")
        seen[name] = seen.get(name, 0) + 1
        if seen[name] > 1:   # a second design for the same page: a variant
            name += ".v%d" % seen[name]
        dst = os.path.join(SL, name + ".md")
        if os.path.exists(dst) and not force:
            dst = os.path.join(SL, "incoming", name + ".md")
        rel = os.path.relpath(dst, SL)
        get(s["htmlCode"]["downloadUrl"], dst)
        if s.get("screenshot", {}).get("downloadUrl"):   # incoming/x.md -> stitch_png/incoming_x.png
            get(s["screenshot"]["downloadUrl"] + "=w1280", os.path.join(SL, "stitch_png", rel[:-3].replace("/", "_") + ".png"))
        idx[rel[:-3]] = {"screen": s["name"], "title": s["title"]}
        print("%-16s %-48s -> %s" % (name, s["title"][:48], rel))
    json.dump(idx, open(idx_f, "w"), indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
