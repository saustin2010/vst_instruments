"""Skin redesign generator (2026-10-01): one spec per plugin -> that plugin's params.json, layout.conf, <port>.css,
images/ and vst.json changes, using the geometry proven on the OB-Xd redesign.

Run a spec:  python3 steve/tools/skin-redesign/specs/<port>.py
It writes into steve/schwung-ports/<port>/. Afterwards layout.conf is the thing to edit (Skin Studio); rerunning
a spec overwrites layout.conf, the stylesheet, images/ and params.json.

Geometry (Force Shadow coordinates, the layout's own): two rows of frames per page, y=92 and y=404, 304 tall;
eight 158-px slots across. A frame spanning slots [a, a+n) sits at x=10+158a, w=158n-6. Controls are spread evenly
across their frame by their span (slots)."""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEVE = os.path.dirname(os.path.dirname(HERE))
MV = os.path.dirname(STEVE)
sys.path.insert(0, os.path.join(MV, "tools"))
import params as P  # noqa: E402

SLOT, X0, ROWS, FH = 158, 10, (92, 404), 304


def frame_rect(a, n, y0):
    return X0 + SLOT * a, y0, SLOT * n - 6, FH


def kind_for(p):
    if p.get("type") in ("stepper", "readout"):
        return p["type"]
    if p.get("momentary"):
        return "button"
    opts = p.get("options")
    if opts:
        low = [str(o).lower() for o in opts]
        if len(opts) == 2 and low[0] in ("off", "no", "0", "free", "disabled"):
            return "toggle"
        return "enum_v" if len(opts) <= 4 else "popup"
    return "knob"


def q(s):
    return '"%s"' % str(s).replace('"', "'")


class Port:
    def __init__(self, port, title, subtitle=(), vendor_dir=None):
        self.port, self.title, self.subtitle = port, title, list(subtitle)
        self.dir = os.path.join(STEVE, "schwung-ports", port)
        self.cfg = json.load(open(os.path.join(self.dir, "vst.json")))
        # The port's ORIGINAL parameter list (module.json's, or its hand-extracted params.json), saved once as
        # params.base.json: this generator writes params.json, so a rerun must not start from its own output.
        base = os.path.join(self.dir, "params.base.json")
        if not os.path.exists(base):
            src, _ = P.source(self.cfg)
            ps, _ = P.load(os.path.join(self.dir, src))
            json.dump({"name": self.cfg["name"], "params": ps}, open(base, "w"), indent=1)
        self.params = [dict(p) for p in json.load(open(base))["params"]]
        self.byk = {p["key"]: p for p in self.params}
        self.theme, self.css, self.looks, self.pages = {}, "", {}, []
        self.led = dict(on="#ff5646", off="#3a1310", cap=("#5a5d63", "#3b3d42", "#25272b"), rim="#74777d", well="#070708")
        self.module_dir = None

    # ---- parameters
    def set(self, key, **kw):
        """Override one parameter (name, min, max, default, options, display, type...). Keeps its VST index."""
        p = self.byk[key]
        for k, v in kw.items():
            if v is None:
                p.pop(k, None)
            else:
                p[k] = v
        if "options" in kw and kw["options"]:
            for k in ("min", "max", "unit"):
                p.pop(k, None)
        if "min" in kw and "options" not in kw:
            p.pop("options", None)
        return self

    def rename(self, old, new, **kw):
        """Give a parameter a new key (same VST index), e.g. to retire one the engine should no longer see."""
        p = self.byk.pop(old)
        p["key"] = new
        self.byk[new] = p
        return self.set(new, **kw)

    def names(self, table):
        for k, n in table.items():
            self.byk[k]["name"] = n
        return self

    def add(self, key, name, **kw):
        """Append a parameter (new ones go at the end so existing VST indices -- saved automation -- stay put)."""
        if key in self.byk:
            raise SystemExit("%s already exists" % key)
        p = dict(key=key, name=name, **kw)
        self.params.append(p)
        self.byk[key] = p
        return self

    def option_stepper(self, key):
        """Show a long option list as a stepper (arrows + Q-Link, the option's name in the box) instead of a pop-up
        that would not fit the screen. Adds hidden <key>_prev/<key>_next triggers."""
        self.set(key, type="stepper")
        name = self.byk[key].get("name", key)
        self.add(key + "_prev", ("PREV " + name)[:24], momentary=True, step_of=key, step_delta=-1)
        self.add(key + "_next", ("NEXT " + name)[:24], momentary=True, step_of=key, step_delta=1)
        return self

    def preset_browser(self, key="preset", name_key="preset_name", count=None, name="PATCH"):
        """A numeric preset index stepped by PREV/NEXT triggers, showing name_key's text (the OB-Xd pattern)."""
        if key in self.byk:
            self.set(key, name=name, min=0, max=count - 1, default=0, display="int", type="stepper", options=None, unit=None)
        else:
            self.add(key, name, min=0, max=count - 1, default=0, display="int", type="stepper")
        self.add(key + "_prev", "PREV " + name, momentary=True, step_of=key, step_delta=-1)
        self.add(key + "_next", "NEXT " + name, momentary=True, step_of=key, step_delta=1)
        if name_key not in self.byk:
            self.add(name_key, name + " NAME", min=0, max=0, display="string", type="readout")
        return self

    # ---- look
    def look(self, theme, css="", knob="moog", led=None):
        self.theme = theme
        self.css = css
        self.looks = {"knob_look": knob} if knob else {}
        if led:
            self.led.update(led)
        return self

    # ---- pages
    def page(self, name, row1, row2=()):
        self.pages.append((name, [row1, row2]))
        return self

    @staticmethod
    def _span(it):
        return it.get("span", 1) if isinstance(it, dict) else 1

    def auto_pages(self, sections, names=None):
        """Pack [(title, [items])] onto pages: two rows of eight slots, a section wider than a row split into
        "TITLE", "TITLE 2"..., a page named after its first section (or names[i]). For big parameter sets."""
        frames = []
        for title, items in sections:
            chunk, used, part = [], 0, 1
            for it in items:
                if used + self._span(it) > 8:
                    frames.append((title if part == 1 else "%s %d" % (title, part), used, chunk))
                    chunk, used, part = [], 0, part + 1
                chunk.append(it)
                used += self._span(it)
            if chunk:
                frames.append((title if part == 1 else "%s %d" % (title, part), used, chunk))
        rows, row, used = [], [], 0
        for f in frames:
            if used + f[1] > 8:
                rows.append(row)
                row, used = [], 0
            row.append(f)
            used += f[1]
        if row:
            rows.append(row)
        taken = {n for n, _ in self.pages}
        for i in range(0, len(rows), 2):
            pair = rows[i:i + 2]
            given = names[i // 2] if names and i // 2 < len(names) else None
            name = given or re.sub(r" \d+$", "", pair[0][0][0])[:12].rstrip()
            base, k = name, 2
            while name in taken:
                name, k = "%s %d" % (base, k), k + 1
            taken.add(name)
            self.page(name, *[self._stretch(r) for r in pair])
        return self

    @staticmethod
    def _stretch(row):
        """Widen a row's frames to fill eight slots (spare slots go to the widest frame)."""
        spare = 8 - sum(f[1] for f in row)
        row = [list(f) for f in row]
        if spare > 0 and row:
            max(row, key=lambda f: f[1])[1] += spare
        return [tuple(f) for f in row]

    def keys(self, *prefixes, exclude=(), placed=None):
        """Parameter keys starting with any of the prefixes (in VST order), minus exclude and hidden ones."""
        out = [p["key"] for p in self.params if any(p["key"].startswith(x) for x in prefixes)
               and p["key"] not in exclude and not p.get("hidden") and not p.get("step_of") and p.get("type") != "readout"]
        return [k for k in out if placed is None or k not in placed]

    def module_sections(self):
        """The module's own ui_hierarchy sections [(label, [keys])], when it has them."""
        src = os.path.join(self.dir, self.cfg.get("module", ""))
        try:
            _, secs = P.load(src)
        except SystemExit:
            secs = None
        return secs or []

    def short_names(self, abbrev=None, keys=None, prefix=None):
        """Upper-case, readable names of at most 12 characters (the width under a knob), unique across the plugin.
        abbrev: extra {word: short} pairs; prefix: {key: "PREFIX"} to disambiguate repeats (e.g. per section)."""
        A = {"FREQUENCY": "FREQ", "RESONANCE": "RESO", "ATTACK": "ATK", "DECAY": "DCY", "RELEASE": "REL",
             "SUSTAIN": "SUS", "AMOUNT": "AMT", "ENVELOPE": "ENV", "FILTER": "FLT", "OSCILLATOR": "OSC", "MODULATION": "MOD",
             "VELOCITY": "VEL", "PRESSURE": "PRES", "FEEDBACK": "FBK", "DETUNE": "DTUN", "VOLUME": "VOL", "PORTAMENTO": "PORTA",
             "WAVEFORM": "WAVE", "DESTINATION": "DEST", "POSITION": "POS", "KEYTRACK": "KEYTRK", "DISTORTION": "DIST",
             "STRUCTURE": "STRUCT", "TENSION": "TENS", "CUTOFF": "CUTOFF", "SPREAD": "SPRD"}
        A.update(abbrev or {})
        def shorten(n):
            n = " ".join(n.upper().replace("_", " ").replace("->", ">").replace("→", ">").split())
            if len(n) <= 12:
                return n
            words = [A.get(w, w) for w in n.split()]
            n = " ".join(words)
            while len(n) > 12:   # trim the longest word a letter at a time: "AMP ATK CURV", not "AMPATKCURVE"
                ws = n.split()
                i = max(range(len(ws)), key=lambda j: len(ws[j]))
                if len(ws[i]) <= 3:
                    return n[:12].rstrip()
                ws[i] = ws[i][:-1]
                n = " ".join(ws)
            return n
        todo = [p for p in self.params if (keys is None or p["key"] in keys) and not p.get("hidden")]
        for p in todo:
            pre = (prefix or {}).get(p["key"])
            p["name"] = shorten(("%s %s" % (pre, p.get("name") or p["key"])) if pre else (p.get("name") or p["key"]))
        seen = {}
        for p in self.params:
            seen.setdefault(p.get("name"), []).append(p)
        for n, ps in seen.items():
            if len(ps) > 1:
                for j, p in enumerate(ps):
                    tag = str(j + 1)
                    p["name"] = (n[:12 - len(tag) - 1] + " " + tag) if len(n) + len(tag) + 1 > 12 else "%s %s" % (n, tag)
        return self

    def _widget(self, it, cx, y0, fw):
        if isinstance(it, str):
            it = {"key": it}
        k = it["key"]
        p = self.byk[k]
        kind = it.get("kind") or kind_for(p)
        lab = it.get("label") or p.get("name") or k
        span = it.get("span", 1)
        if kind == "knob":
            big = it.get("big")
            r = 46 if big else 38
            return 'knob cx=%d cy=%d r=%d label=%s key=%s' % (cx, y0 + (137 if big else 145), r, q(lab), k)
        if kind == "toggle":
            return 'toggle cx=%d cy=%d w=64 h=50 label=%s key=%s' % (cx, y0 + 156, q(lab), k)
        if kind == "button":
            return 'button cx=%d cy=%d label=%s key=%s' % (cx, y0 + 150, q(it.get("label") or lab[:12]), k)
        if kind == "slider":
            return 'slider_v cx=%d cy=%d w=44 h=150 label=%s key=%s' % (cx, y0 + 140, q(lab), k)
        if kind == "enum_v":
            n = len(p["options"])
            opts = it.get("opts") or p["options"]
            return 'enum_v cx=%d cy=%d label=%s key=%s options=%s' % (cx, y0 + {4: 140, 5: 160}.get(n, 150), q(lab), k, q(",".join(map(str, opts))))
        if kind == "popup":
            w = it.get("w") or SLOT * span - 24
            s = 'popup cx=%d cy=%d w=%d h=48 label=%s key=%s' % (cx, y0 + 150, w, q(lab), k)
            if it.get("opts"):   # drawn labels only; the parameter keeps the engine's own option text
                s += " options=%s" % q(",".join(it["opts"]))
            return s
        if kind == "picture":   # one image per option of the param, MPC shows the current one (e.g. its waveform)
            w, h = it["w"], it["h"]
            return 'picture x=%d y=%d w=%d h=%d key=%s files=%s' % (cx - w // 2, y0 + it.get("dy", 170), w, h, k,
                                                                  q(",".join(it["files"])))
        if kind in ("stepper", "readout"):
            w = it.get("w") or (fw - 46 if it.get("full", True) else SLOT * span - 30)
            h = it.get("h", 80 if kind == "stepper" else 56)
            cy = y0 + it.get("dy", 158)
            s = '%s cx=%d cy=%d w=%d h=%d label=%s key=%s' % (kind, cx, cy, w, h, q(it.get("label", lab)), k)
            if it.get("get"):
                s += " get=%s" % it["get"]
            if it.get("dot", True):
                s += " style=dotmatrix"
            return s
        raise SystemExit("unknown kind %s" % kind)

    def _row(self, frames, y0, out, qkeys):
        a = 0
        for fr in frames:
            title, n, items = fr[0], fr[1], (fr[2] if len(fr) > 2 else [])
            x, y, w, h = frame_rect(a, n, y0)
            a += n
            if title.startswith("@info:"):   # a titled frame of plain text lines ("@info:TITLE|LINE 1|LINE 2"), e.g. routing
                head, *lines = title[6:].split("|")
                out.append('frame x=%d y=%d w=%d h=%d title=%s' % (x, y, w, h, q(head)))
                fn = "images/info_%d.svg" % len(self._files)
                body = "".join('<text x="%d" y="%d" text-anchor="middle" style="font-family:var(--font);font-weight:%d;'
                               'font-size:%dpx;letter-spacing:0.08em;fill:var(--%s)">%s</text>\n'
                               % (w // 2, 100 + 36 * j, 700 if j == 0 else 600, 21 if j == 0 else 17,
                                  "accent-hi" if j == 0 else "ink-dim", t) for j, t in enumerate(lines))
                self._files[fn] = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="304" viewBox="0 0 %d 304">\n'
                                   '%s</svg>\n' % (w, w, body))
                out.append("art file=%s x=%d y=%d w=%d h=%d" % (fn, x, y, w, h))
                continue
            if title == "@logo":
                out.append("art file=images/logo_%d.svg x=%d y=%d w=%d h=%d" % (w, x, y, w, h))
                self._files["images/logo_%d.svg" % w] = self._logo(w)
                continue
            out.append('frame x=%d y=%d w=%d h=%d title=%s' % (x, y, w, h, q(title)))
            pics = [it for it in items if isinstance(it, dict) and it.get("kind") == "picture"]
            items = [it for it in items if it not in pics]
            for it in pics:   # displays sit at the frame's centre under the controls; no Q-Link
                out.append(self._widget(it, x + w // 2, y0, w))
            spans = [(it.get("span", 1) if isinstance(it, dict) else 1) for it in items]
            total, start = max(1, sum(spans)), 0
            for it, sp in zip(items, spans):
                cx = int(x + w * (start + sp / 2) / total)
                start += sp
                out.append(self._widget(it, cx, y0, w))
                key = it if isinstance(it, str) else it["key"]
                qkeys.append(key)
                if isinstance(it, dict) and it.get("repeat"):   # deliberately on more than one page
                    self._repeat.add(key)
                if isinstance(it, dict) and it.get("caption"):
                    out.append("art file=images/caption_%s.svg x=%d y=%d w=%d h=30" % (key, x + 23, y0 + 218, w - 46))
                    self._caption(key, it["caption"], w - 46)

    # ---- files
    def _caption(self, key, text, w):
        self._files["images/caption_%s.svg" % key] = (
            '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="30" viewBox="0 0 %d 30">\n'
            ' <text x="%d" y="20" text-anchor="middle" style="font-family:var(--font);font-weight:600;font-size:15px;'
            'letter-spacing:0.14em;fill:var(--ink-faint)">%s</text>\n</svg>\n' % (w, w, w // 2, text))

    def _logo(self, W=310):
        t = self.title
        size = min(84, int((W - 50) / max(1, 0.6 * len(t))))
        cx = W // 2
        subs = "".join(' <text x="%d" y="%d" text-anchor="middle" style="font-family:var(--font);font-weight:600;'
                       'font-size:16px;letter-spacing:0.22em;fill:var(--ink-dim)">%s</text>\n' % (cx, 206 + 24 * i, s)
                       for i, s in enumerate(self.subtitle))
        head, tail = (t[:-1], t[-1]) if len(t) > 1 else (t, "")
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="304" viewBox="0 0 %d 304">\n'
                ' <rect x="0.75" y="0.75" width="%g" height="302.5" rx="8" style="fill:var(--panel);stroke:var(--line);stroke-width:1.5"/>\n'
                ' <text x="%d" y="%d" text-anchor="middle" style="font-family:var(--font);font-weight:700;font-size:%dpx;'
                'letter-spacing:0.02em;fill:var(--ink)">%s<tspan style="fill:var(--accent-hi)">%s</tspan></text>\n'
                ' <rect x="%d" y="172" width="226" height="3" style="fill:var(--accent)"/>\n%s</svg>\n'
                % (W, W, W - 1.5, cx, 150 if size > 60 else 140, size, t and head, tail, cx - 113, subs))

    def _buttons(self):
        L = self.led
        grad = lambda gid, c: ('<linearGradient id="%s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/>'
                               '<stop offset="0.45" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>' % ((gid,) + c))
        off = ('<svg xmlns="http://www.w3.org/2000/svg" width="56" height="44" viewBox="0 0 56 44">\n'
               ' <defs>%s</defs>\n'
               ' <circle cx="28" cy="8" r="4.5" fill="%s" stroke="#000" stroke-width="1"/>\n'
               ' <rect x="3" y="18" width="50" height="25" rx="4" fill="%s"/>\n'
               ' <rect x="5" y="18.5" width="46" height="21" rx="3" fill="url(#%s-cap-off)" stroke="%s" stroke-width="0.8"/>\n'
               '</svg>\n' % (grad(self.port + "-cap-off", L["cap"]), L["off"], L["well"], self.port, L["rim"]))
        lit = tuple(_shade(c, 1.18) for c in L["cap"])
        on = ('<svg xmlns="http://www.w3.org/2000/svg" width="56" height="44" viewBox="0 0 56 44">\n'
              ' <defs>%s<filter id="%s-glow" x="-2" y="-2" width="5" height="5"><feGaussianBlur stdDeviation="2.4"/></filter></defs>\n'
              ' <circle cx="28" cy="8" r="6.5" fill="%s" opacity="0.85" filter="url(#%s-glow)"/>\n'
              ' <circle cx="28" cy="8" r="4.5" fill="%s" stroke="#000" stroke-width="1"/>\n'
              ' <circle cx="26.6" cy="6.6" r="1.4" fill="#fff" opacity="0.75"/>\n'
              ' <rect x="3" y="18" width="50" height="25" rx="4" fill="%s"/>\n'
              ' <rect x="5" y="20" width="46" height="21" rx="3" fill="url(#%s-cap-on)" stroke="%s" stroke-width="0.8"/>\n'
              '</svg>\n' % (grad(self.port + "-cap-on", lit), self.port, L["on"], self.port, L["on"], L["well"], self.port, L["rim"]))
        return off, on

    def write(self, module_dir=None, data_note="", defines=None):
        self._files = {}
        self._repeat = set()
        out = ["# %s for MPC: %d pages, one Q-Link bank each. Browser renderer (\"art\": \"html\"), restyled by %s.css;"
               % (self.title, len(self.pages), self.port),
               "# generated by steve/tools/skin-redesign (2026-10-01); edit this file (Skin Studio) from here on."]
        out += ["theme_%s=%s" % (k, v.lstrip("#")) for k, v in self.theme.items()]
        out += ["art_css=%s.css" % self.port] + ["%s=%s" % kv for kv in self.looks.items()]
        out += ["toggle_img=images/btn_off.svg", "toggle_img_on=images/btn_on.svg"]
        placed = set()
        for name, rows in self.pages:
            out += ["", "[tab %s]" % name]
            qkeys = []
            for r, frames in enumerate(rows):
                self._row(frames, ROWS[r], out, qkeys)
            if len(qkeys) > 16:
                raise SystemExit("page %s: %d controls, over one Q-Link bank" % (name, len(qkeys)))
            dup = (placed & set(qkeys)) - self._repeat
            if dup:
                raise SystemExit("page %s repeats %s" % (name, dup))
            placed |= set(qkeys)
            out.append('qlinks "%s" = %s' % (name, ",".join(qkeys)))
        hidden = {k for k, p in self.byk.items() if p.get("step_of") or p.get("type") == "readout" or p.get("hidden")}
        missing = [k for k in self.byk if k not in placed and k not in hidden]
        if missing:
            raise SystemExit("not on any page: %s" % missing)
        names = [p.get("name", p["key"]) for p in self.params]
        dups = {n for n in names if names.count(n) > 1}
        if dups:
            raise SystemExit("duplicate parameter names: %s" % sorted(dups))
        for p in self.params:
            p.pop("hidden", None)
        self._files["layout.conf"] = "\n".join(out) + "\n"
        self._files["%s.css" % self.port] = CSS_BASE + self.css
        self._files["images/btn_off.svg"], self._files["images/btn_on.svg"] = self._buttons()
        self._files["params.json"] = json.dumps({"name": self.cfg["name"], "params": self.params}, indent=1) + "\n"
        cfg = dict(self.cfg)
        cfg.update({"params": "params.json", "layout": "layout.conf", "art": "html"})
        if module_dir:
            cfg["defines"] = dict(cfg.get("defines", {}), MODULE_DIR='"%s"' % module_dir)
        if defines:
            cfg["defines"] = dict(cfg.get("defines", {}), **defines)
        self._files["vst.json"] = json.dumps(cfg, indent=2) + "\n"
        os.makedirs(os.path.join(self.dir, "images"), exist_ok=True)
        for rel, text in self._files.items():
            path = os.path.join(self.dir, rel)
            tmp = path + ".new"
            open(tmp, "w").write(text)
            os.replace(tmp, path)
        print("%s: %d params, %d pages -> %s" % (self.port, len(self.params), len(self.pages), self.dir))


def _shade(hexc, f):
    h = hexc.lstrip("#")
    return "#%02x%02x%02x" % tuple(min(255, int(int(h[i:i + 2], 16) * f)) for i in (0, 2, 4))


CSS_BASE = """/* generated by steve/tools/skin-redesign (base, shared by every redesigned port); loaded after
 * tools/html_art/default.css. Colours come from layout.conf's theme_* lines. */
:root { --title-size: 22px; --label-size: 17px; --seg-size: 16px; --button-size: 16px; --radius: 5px; --sheen: 0.06; }
.frame-border { fill: var(--panel); stroke: var(--line); stroke-width: 1.5; rx: 8px; }
.frame-title { fill: var(--ink); letter-spacing: 0.16em; }
.frame-rule { stroke: var(--accent); stroke-width: 2; }
.text { fill: var(--ink) !important; letter-spacing: 0.1em; }   /* group labels: coloured inline by the builder */
.box-label { fill: var(--ink); letter-spacing: 0.14em; }
.seg-tx { letter-spacing: 0.08em; }
.arrow-bg { fill: var(--panel); }
.arrow { fill: var(--accent-hi); }
.dot-arrow { fill: var(--accent-hi); }
"""
