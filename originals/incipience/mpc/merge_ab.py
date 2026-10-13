#!/usr/bin/env python3
"""Incipience: folds convert.py's pages (KICK A, KICK B, KICK MIX, ... PAN, NOTE) into 13 tabs, each with its pages as
Q-Link sub-pages: the AlphaBase's PAGE A/B, and on an instrument a third whose Q-Links turn its MIX panel and extras
(FM: FM VOICE). A control the same on every page of a tab (a shared row, the MIX panel) is there once; one on some
pages gets banks= (shadow_skin.py: only on those sub-pages), and so does each page's background.

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
    pages = json.load(open(os.path.join(ROOT, "build", "pages.json")))
    want = sum(len(pg["subs"]) for pg in pages)
    if len(tabs) != want:
        raise SystemExit("layout.conf has %d tabs, not convert.py's %d (already merged?)" % (len(tabs), want))
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    shutil.copy(LAYOUT, os.path.join(ROOT, "build", "layout.pages.conf"))
    out = [l for l in head if l.strip() and not l.startswith("#")]
    out = ["# Incipience for MPC: designed in Stitch (design/stitch-kick.html), every page in its markup by mpc/design.py,",
           "# converted by steve/tools/stitch/convert.py and folded into tabs with A/B Q-Link sub-pages by mpc/merge_ab.py.",
           "# Generated: change those, not this."] + out + [h for h in HEADER if h != "art_css="]
    for t in tabs:
        # the matrix knobs' values smaller than MPC's 22, clear of the next row's title; the side panels' names at 15
        # (convert.py sizes a name from its key's length, which shrinks REV / DLY)
        t["lines"] = [l + " vs=17" if l.startswith("knob ") and " r=29 " in l and " vs=" not in l else l for l in t["lines"]]
        t["lines"] = [re.sub(r" ns=\d+", " ns=15", l) if l.startswith("knob ") and " r=20 " in l else l for l in t["lines"]]
    at = 0
    for pg in pages:
        group = tabs[at:at + len(pg["subs"])]
        at += len(group)
        ctl = lambda t: [l for l in t["lines"] if not l.startswith(("art ", "qlinks "))]
        body, rest, order = [], {}, []
        for t in group:
            body.append('art %s banks="%s"' % (next(l for l in t["lines"] if l.startswith("art "))[4:], t["name"]))
            for l in ctl(t):
                if l not in rest:
                    rest[l] = []
                    order.append(l)
                rest[l].append(t["name"])
        for l in order:   # on every page: as it is; on some: only there
            body.append(l if len(rest[l]) == len(group) else '%s banks="%s"' % (l, "|".join(rest[l])))
        body += [l for t in group for l in t["lines"] if l.startswith("qlinks ")]
        out += ["", "[tab %s]" % pg["tab"]] + body
    open(LAYOUT, "w").write("\n".join(out) + "\n")
    print("layout.conf: %d tabs, each with sub-pages (was build/layout.pages.conf)" % len(pages))


if __name__ == "__main__":
    main()
