"""libpo32 (PO-32-style drum synth: 16 pads on notes 36-51): circuit-board green, gold legends, a grey-green LCD kit
display. Kits are folders, shipped to /sdcard/vst/libpo32/kits (build.sh libpo32 src/kits:kits); the bundled kits
fill pads 1-8 (tonic) or 1-4 (tape, acid), so the mixer/tune pages show pads 1-8 and EDIT reaches all 16.
The engine's per-pad editor is stateful: "inst" picks a pad, the inst_* parameters edit that pad (0-100)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("libpo32", "PO-32", ["DRUM SYNTH", "16 PADS · NOTES 36-51"])
SHORT = {"freq": "PITCH", "atk": "ATTACK", "dcy": "DECAY", "mrate": "MRATE", "mamt": "BEND", "nffrq": "NFREQ",
         "nfq": "NQ", "neatk": "NATK", "nedcy": "NDCY", "mix": "NOISE", "dist": "DIST", "lvl": "LEVEL"}
for k in list(p.byk):
    if k[0] == "v" and k[1:3].isdigit():
        n, f = int(k[1:3]), k[4:]
        p.set(k, name="PAD%d %s" % (n, SHORT[f]), type=None, hidden=not (f in ("freq", "dcy", "lvl") and n <= 8))
p.set("level", name="LEVEL", type=None).set("decay", name="DECAY SCALE", type=None)
p.preset_browser(key="kit", name_key="kit_name", count=3, name="KIT")
p.add("randomize", "RANDOM KIT", momentary=True)
p.add("inst", "EDIT PAD", options=["PAD %d" % i for i in range(1, 17)], default=0)
for key, name, kw in [("inst_wave", "WAVE", {"options": ["SINE", "TRI", "SAW"]}),
                      ("inst_freq", "PITCH", {}), ("inst_dcy", "DECAY", {}),
                      ("inst_mod_mode", "MOD MODE", {"options": ["DECAY", "SINE", "NOISE"]}),
                      ("inst_mod_amt", "MOD AMOUNT", {}), ("inst_noise", "NOISE MIX", {}),
                      ("inst_noise_filt", "NOISE FILTER", {"options": ["LP", "BP", "HP"]}),
                      ("inst_noise_env", "NOISE ENV", {"options": ["EXP", "LIN", "MOD"]}),
                      ("inst_dist", "DISTORTION", {}), ("inst_level", "PAD LEVEL", {})]:
    if "options" not in kw:
        kw.update(min=0, max=100, display="int")
    p.add(key, name, default=0, **kw)
p.look({"bg": "071f19", "panel": "0d3128", "line": "2a6a55", "ink": "f1faf5", "ink_dim": "c3e2d4", "ink_faint": "6fa08c",
        "accent": "d9b43c", "accent_hi": "f2cf55", "knob_face": "f1faf5", "knob_ring": "1c4a3d", "knob_dot": "f2cf55",
        "lcd": "06150f", "seg_active": "f2cf55", "seg_inactive": "15443a", "seg_active_tx": "071f19", "box": "0d3128",
        "display_bg": "a9b39a", "display_cell": "9ea88f", "display_off": "939d84", "display_ink": "1a1f14",
        "display_bezel": "0a0d08"},
       knob="cap",
       led=dict(on="#ff4a3a", off="#3b1510", cap=("#d9d3bf", "#bcb6a2", "#99937f"), rim="#5f5a4c", well="#04110d"),
       css=".slider-fill { fill: var(--accent-hi); }\n")
S = lambda k: {"key": k, "kind": "slider"}
pads = range(1, 9)
p.page("KIT",
       [("KIT", 4, [{"key": "kit", "get": "kit_name", "caption": "3 KITS  ·  PADS 1-16 = NOTES 36-51"}]),
        ("MASTER", 4, ["level", "decay", "randomize"])],
       [("PAD LEVEL", 8, [S("v%02d_lvl" % n) for n in pads])])
p.page("EDIT",
       [("PAD", 2, ["inst"]), ("OSCILLATOR", 3, ["inst_wave", "inst_freq", "inst_dcy"]),
        ("MODULATION", 3, ["inst_mod_mode", "inst_mod_amt"])],
       [("NOISE", 4, ["inst_noise", "inst_noise_filt", "inst_noise_env"]), ("OUTPUT", 4, ["inst_dist", "inst_level"])])
p.page("TUNE",
       [("PAD PITCH", 8, ["v%02d_freq" % n for n in pads])],
       [("PAD DECAY", 8, ["v%02d_dcy" % n for n in pads])])
p.write(module_dir="/sdcard/vst/libpo32")
