"""Check a built skin (what MPC will load) against the port's layout and parameters:
   python3 steve/tools/stitch/check_skin.py <port> [<port> ...] | all      [-v: list every page]
Reads deploy/Synths/<skin>/Plugin Skins/{TUI.json,Q-Links.json} and build/params.h (parameter numbers), and reports
  BIND    a layout control whose parameter no widget on its page is bound to, or a widget bound to a wrong/unknown number
  QLINK   a Q-Link on a parameter that isn't on that page, Q-Links out of the layout's order, or > 16 on a page
  TOUCH   two controls' touch boxes overlapping (MPC gives the touch to one of them): knobs, sliders and toggles carry
          their name and value in the box, so close neighbours overlap
  EDGE    a control's box past the 1280 x 628 screen
  OPTS    a switch or pop-up showing a different number of option labels than its parameter has
Exit status 1 if anything but TOUCH overlaps under 12 px was found."""
import glob, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEVE = os.path.dirname(os.path.dirname(HERE))
MV = os.path.dirname(STEVE)
sys.path.insert(0, os.path.join(MV, "tools"))
import shadow_skin  # noqa: E402

CONTROLS = ("knob", "slider_v", "slider_h", "toggle", "button", "enum_h", "enum_v", "popup", "stepper", "list", "menu")


def rect(b):
    x, y, w, h = (int(float(v)) for v in b.split())
    return x, y, w, h


def overlap(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return max(0, min(ax + aw, bx + bw) - max(ax, bx)), max(0, min(ay + ah, by + bh) - max(ay, by))


def check(port, verbose=False):
    D = os.path.join(STEVE, "schwung-ports", port)
    skins = glob.glob(os.path.join(D, "deploy", "Synths", "*", "Plugin Skins"))
    if not skins or not os.path.exists(os.path.join(D, "build", "params.h")):
        return ["not built here yet (build it first: skin-redesign/build.sh %s, or the repo's tools/build.sh)" % port], 0
    sk = skins[0]
    tui = json.load(open(os.path.join(sk, "TUI.json")))["pageData"]
    qmap = json.load(open(os.path.join(sk, "Q-Links.json")))["Screen Mode Q-Links"]["map"]
    keys = re.findall(r'^\s*\{"([^"]+)",', open(os.path.join(D, "build", "params.h")).read(), re.M)
    layout, _ = shadow_skin.parse_layout(os.path.join(D, "layout.conf"))
    lay = {t["name"]: t for t in layout}
    defs = {e["key"]: e["value"] for e in tui["componentDefinitions"]["localComponentDefinitions"]}
    out, minor = [], 0
    popts = {p["key"]: p.get("options") for p in json.load(open(os.path.join(D, "params.json")))["params"]}
    for t in layout:   # a switch's / pop-up's own option labels must line up with the parameter's options
        for w in t["widgets"]:
            if w["kind"] in ("enum_h", "enum_v", "popup") and w.get("options") and popts.get(w.get("key")) is not None:
                if len(w["options"]) != len(popts[w["key"]]):
                    out.append("OPTS  %s: %s shows %d options, the parameter has %d" % (
                        t["name"], w["key"], len(w["options"]), len(popts[w["key"]])))
    page_order = [t["name"] for t in layout]
    subs = {}
    for ti, tab in enumerate(tui["tabs"]):
        page_name = tab["componentName"].split("|")[0]
        lt = lay.get(page_name)
        sub = subs[page_name] = subs.get(page_name, -1) + 1   # which Q-Link bank of the page this tab is
        page_no = page_order.index(page_name) + 1 if page_name in page_order else ti + 1
        comps = defs[tab["componentName"]]["componentsData"]
        bound = {}   # key -> [(rect, kind name)]
        for c in comps:
            m = {e["key"]: e["value"] for e in c["handle remapping"]["map"]}
            if "Data" not in m or c["bounds"].get("acceptsHWFocus") != "Yes":
                continue
            ks = []
            for v in m.values():   # Data plus any second handle (a pop-up's Data is its "<key>__open" flag)
                n = int(v.split()[1])
                if n >= len(keys):
                    out.append("BIND  %s: widget %r bound to Parameter %d (only %d)" % (
                        tab["tabName"], c["componentData"]["name"], n, len(keys)))
                    continue
                k = keys[n][:-len("__open")] if keys[n].endswith("__open") else keys[n]
                if k not in ks:
                    ks.append(k)
            for k in ks:   # (one widget's own handles, e.g. a stepper and the name it shows, never "overlap")
                bound.setdefault(k, []).append((rect(c["bounds"]["bounds"]), id(c)))
        if lt:
            want = {w["key"] for w in lt["widgets"] if w["kind"] in CONTROLS and "key" in w}
            for k in sorted(want - set(bound)):
                out.append("BIND  %s: %s is in the layout but no widget on the page is bound to it" % (page_name, k))
        # touch boxes of different parameters overlapping
        items = [(k, r, nm) for k, rs in bound.items() for r, nm in rs]
        seen = set()
        for i in range(len(items)):
            for j in range(i + 1, len(items)):
                (k1, r1, n1), (k2, r2, n2) = items[i], items[j]
                if k1 == k2 or n1 == n2:
                    continue
                ox, oy = overlap(r1, r2)
                if ox > 0 and oy > 0 and (k1, k2) not in seen:
                    seen.add((k1, k2))
                    if min(ox, oy) < 12:
                        minor += 1
                        if verbose:
                            out.append("touch %s: %s / %s overlap %dx%d px" % (page_name, k1, k2, ox, oy))
                    else:
                        out.append("TOUCH %s: %s and %s overlap %dx%d px" % (page_name, k1, k2, ox, oy))
        for k, rs in bound.items():
            for (x, y, w, h), nm in rs:
                if x < -2 or y < -2 or x + w > 1282 or y + h > 630:
                    out.append("EDGE  %s: %s box %d,%d %dx%d leaves the screen" % (page_name, k, x, y, w, h))
        # Q-Links of this tab (sub-tab = which qlinks line)
        for e in qmap:
            if e["Tab"] != page_no or e.get("SubTab", 1) != sub + 1:
                continue
            slots = {}
            for name, n in e["Q-Links"].items():
                if n >= 0:
                    slots[int(name.split()[1])] = keys[n]
            order = [slots[shadow_skin.qlink_for_slot(s)] for s in range(16) if shadow_skin.qlink_for_slot(s) in slots]
            for k in order:
                if k not in bound:
                    out.append("QLINK %s: Q-Link on %s, which isn't on the page" % (tab["tabName"], k))
            if lt and lt["qlinks"]:
                if sub < len(lt["qlinks"]):
                    exp = [k for k in lt["qlinks"][sub][1]]
                    if order != exp:
                        out.append("QLINK %s: order %s, layout says %s" % (tab["tabName"], ",".join(order), ",".join(exp)))
        if verbose:
            out.append("page  %-14s %2d controls, %2d Q-Links" % (tab["tabName"], len(bound),
                                                                sum(1 for e in qmap if e["Tab"] == page_no and
                                                                    e.get("SubTab", 1) == sub + 1
                                                                    for v in e["Q-Links"].values() if v >= 0)))
    return out, minor


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    verbose = "-v" in sys.argv
    if args == ["all"]:
        args = sorted(p for p in os.listdir(os.path.join(STEVE, "schwung-ports"))
                      if os.path.isdir(os.path.join(STEVE, "schwung-ports", p, "deploy")))
    bad = 0
    for p in args:
        issues, minor = check(p, verbose)
        serious = [i for i in issues if not i.startswith(("page", "touch"))]
        bad += bool(serious)
        print("%-13s %s%s" % (p, "OK" if not serious else "%d issue(s)" % len(serious),
                              "  (%d small touch overlaps < 12 px)" % minor if minor else ""))
        for i in issues:
            print("    " + i)
    sys.exit(1 if bad else 0)
