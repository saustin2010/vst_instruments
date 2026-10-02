"""Mono Voice (Elektron-style digital machine voice: SuperWave, SID, DigiPRO, FM...): Monomachine-ish dark grey with
an amber accent. Names come from the module's own ui_hierarchy ("name" fields); the synth page's 16 controls change
meaning with the machine, so they stay numbered (SYN 1..16)."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("monovoice", "Mono Voice", ["DIGITAL", "MACHINE VOICE"])
levels = json.load(open(os.path.join(p.dir, "src/module.json")))["capabilities"]["ui_hierarchy"]["levels"]
section_of, given = {}, {}
for lv, d in levels.items():
    for q in d.get("params", []):
        if isinstance(q, dict) and "key" in q:
            section_of[q["key"]] = lv.replace("_shift", "")
            given[q["key"]] = q.get("name") or q["key"]
SEC = {"synth": "SYN", "amp": "AMP", "filter": "FLT", "effect": "FX", "lfo1": "LFO1", "lfo2": "LFO2", "lfo3": "LFO3"}
for k, v in list(p.byk.items()):
    if v.get("type"):
        p.set(k, type=None)
    if v.get("options"):
        p.set(k, options=[str(o).upper() for o in v["options"]])
    n = given.get(k, k)
    if k.startswith("syn"):
        n = "SYN " + k[3:]
    p.set(k, name=n)
pre = {k: SEC[s] for k, s in section_of.items() if s in SEC and not k.startswith("syn")}
p.short_names(prefix=pre, abbrev={"DESTINATION": "DEST", "MULTIPLIER": "MULT"})
p.set("machine", name="MACHINE")
for n in "123":
    p.option_stepper("lfo%s_1" % n)
p.look({"bg": "121212", "panel": "1d1d1d", "line": "444444", "ink": "f2f2f2", "ink_dim": "cfcfcf", "ink_faint": "7d7d7d",
        "accent": "e8a228", "accent_hi": "ffbf47", "knob_face": "f2f2f2", "knob_ring": "2e2e2e", "knob_dot": "ffbf47",
        "lcd": "0a0a0a", "seg_active": "ffbf47", "seg_inactive": "2a2a2a", "seg_active_tx": "121212", "box": "1d1d1d"},
       knob=None,
       led=dict(on="#ffbf47", off="#3a2a0a", cap=("#5a5a5a", "#404040", "#2a2a2a"), rim="#7d7d7d", well="#070707"))
row = lambda title, keys: (title, 8, keys)
grp = lambda pre, a, b: ["%s%d" % (pre, i) for i in range(a, b + 1)]
DEST = lambda n: {"key": "lfo%s_1" % n, "full": False, "span": 2, "w": 296, "h": 48, "dy": 150, "label": "LFO %s DEST" % n}
p.page("MACHINE", [("MACHINE", 2, [{"key": "machine", "span": 2, "w": 250}]), ("@logo", 6)],
       [("LFO DESTINATIONS", 8, [DEST(1), DEST(2), DEST(3)])])
p.page("SYNTH", [row("SYNTH", grp("syn", 1, 8))], [row("SYNTH", grp("syn", 9, 16))])
for title, pre, page in (("AMP", "amp", "AMP"), ("FILTER", "flt", "FILTER"), ("EFFECT", "fx", "EFFECT")):
    p.page(page, [row(title, grp(pre, 1, 8))], [row(title + " SHIFT", grp(pre, 9, 16))])
for n in "123":
    p.page("LFO " + n, [row("LFO " + n, ["lfo%s_%d" % (n, i) for i in range(2, 10)])],
           [row("LFO %s SHIFT" % n, ["lfo%s_%d" % (n, i) for i in range(10, 17)])])
p.write()
