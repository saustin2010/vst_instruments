#!/usr/bin/env python3
"""Incipience: folds convert.py's 26 pages (KICK A, KICK B, ... PAN, NOTE) into 13 tabs, each with two Q-Link
sub-pages, as the AlphaBase's PAGE A/B: a control the same on both pages (a shared row, the MIX panel) is there once;
one on a single page gets banks= (shadow_skin.py: only on that sub-page), and so does each page's background.

    python3 steve/tools/stitch/convert.py incipience      (in the workspace, see RESKINNING.md)
    python3 mpc/merge_ab.py                               (from originals/incipience)
"""
import json
import os
import re
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYOUT = os.path.join(ROOT, "layout.conf")
# what every screen in this repo carries since the design QA (Moog the model): MPC outlines each Q-Link column's
# controls, a row of the matrix
HEADER = ["art_css=", "qlink_bounds=column", "qlink_box=slot", "label_scale=1", "focus_ring=1"]


def parse(text):
    head, tabs = [], []
    for line in text.splitlines():
        m = re.match(r"\[tab (.+)\]$", line.strip())
        if m:
            tabs.append({"name": m.group(1), "lines": []})
        elif tabs:
            if line.strip():
                tabs[-1]["lines"].append(line)
        else:
            head.append(line)
    return head, tabs


def main():
    text = open(LAYOUT).read()
    head, tabs = parse(text)
    if len(tabs) != 26:
        raise SystemExit("layout.conf has %d tabs, not convert.py's 26 (already merged?)" % len(tabs))
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    shutil.copy(LAYOUT, os.path.join(ROOT, "build", "layout.pages.conf"))
    pages = json.load(open(os.path.join(ROOT, "build", "pages.json")))
    out = [l for l in head if l.strip() and not l.startswith("#")]
    out = ["# Incipience for MPC: designed in Stitch (design/stitch-kick.html), every page in its markup by mpc/design.py,",
           "# converted by steve/tools/stitch/convert.py and folded into tabs with A/B Q-Link sub-pages by mpc/merge_ab.py.",
           "# Generated: change those, not this."] + out + [h for h in HEADER if h != "art_css="]
    for t in tabs:
        # the matrix knobs' values smaller than MPC's 22, clear of the next row's title; the side panels' names at 15
        # (convert.py sizes a name from its key's length, which shrinks REV / DLY)
        t["lines"] = [l + " vs=17" if l.startswith("knob ") and " r=29 " in l and " vs=" not in l else l for l in t["lines"]]
        t["lines"] = [re.sub(r" ns=\d+", " ns=15", l) if l.startswith("knob ") and " r=20 " in l else l for l in t["lines"]]
    for i, pg in enumerate(pages):
        a, b = tabs[2 * i], tabs[2 * i + 1]
        ctl = lambda t: [l for l in t["lines"] if not l.startswith(("art ", "qlinks "))]
        qa = [l for l in a["lines"] if l.startswith("qlinks ")]
        qb = [l for l in b["lines"] if l.startswith("qlinks ")]
        la, lb = ctl(a), ctl(b)
        both = [l for l in la if l in lb]
        body = ['art %s banks="%s"' % (l[4:], a["name"]) for l in a["lines"] if l.startswith("art ")]
        body += ['art %s banks="%s"' % (l[4:], b["name"]) for l in b["lines"] if l.startswith("art ")]
        body += both
        body += ['%s banks="%s"' % (l, a["name"]) for l in la if l not in both]
        body += ['%s banks="%s"' % (l, b["name"]) for l in lb if l not in both]
        out += ["", "[tab %s]" % pg["tab"]] + body + qa + qb
    open(LAYOUT, "w").write("\n".join(out) + "\n")
    print("layout.conf: %d tabs, each with sub-pages (was build/layout.pages.conf)" % len(pages))


if __name__ == "__main__":
    main()
