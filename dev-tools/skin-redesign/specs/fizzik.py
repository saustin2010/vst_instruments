"""Fizzik (two coupled physical-model resonators: string/beam/plate/membrane): brushed-copper on graphite. Presets
are a named list; the three randomise triggers are buttons. Options keep the engine's text (upper-cased)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("fizzik", "Fizzik", ["PHYSICAL", "MODELLING SYNTH"])
for k, v in list(p.byk.items()):
    if v.get("type"):
        p.set(k, type=None)
    if v.get("options") and not v.get("momentary"):
        p.set(k, options=[str(o).upper() for o in v["options"]])
for k in ("a_tune", "b_tune"):
    p.set(k, display="int")
p.short_names()
p.names({"preset": "PRESET", "rnd_patch": "RANDOM ALL", "rnd_exc": "RND EXCITER", "rnd_reson": "RND RESON",
         "a_model": "MODEL A", "b_model": "MODEL B", "couple": "COUPLE", "balance": "BALANCE", "cutoff": "CUTOFF",
         "resonance": "RESONANCE", "ftype": "FILTER TYPE", "voicing": "VOICING", "level": "LEVEL"})
p.look({"bg": "0d0b0a", "panel": "181412", "line": "4a3a30", "ink": "fbf1ea", "ink_dim": "e1cfc3", "ink_faint": "8a7466",
        "accent": "c8743a", "accent_hi": "f0925a", "knob_face": "fbf1ea", "knob_ring": "33281f", "knob_dot": "f0925a",
        "lcd": "090706", "seg_active": "f0925a", "seg_inactive": "2a221d", "seg_active_tx": "0d0b0a", "box": "181412"},
       knob="metal",
       led=dict(on="#f0925a", off="#3a2414", cap=("#6a564a", "#4a3c33", "#302722"), rim="#8a7466", well="#080605"))
p.page("MAIN",
       [("PATCH", 4, [{"key": "preset", "span": 2, "w": 280}, "rnd_patch", "rnd_exc"]),
        ("RESONATORS", 4, ["a_model", "b_model", "couple", "balance"])],
       [("FILTER", 4, [{"key": "cutoff", "big": True}, "resonance", "ftype", "voicing"]),
        ("OUT", 4, ["rnd_reson", "drive", "width", "level"])])
placed = {"preset", "rnd_patch", "rnd_exc", "rnd_reson", "a_model", "b_model", "couple", "balance", "cutoff", "resonance",
          "ftype", "voicing", "drive", "width", "level"}
pick = lambda *pre: [k for k in p.keys(*pre) if k not in placed]
p.auto_pages([("EXCITER", pick("exc_", "vel_")), ("RESONATOR A", pick("a_")), ("RESONATOR B", pick("b_")),
              ("VOICE", pick("glide", "amp_", "spread")), ("REVERB", pick("rev_")), ("DELAY", pick("dly_")),
              ("TONE", pick("eq_", "cho_", "comp_", "lim_")), ("LFO 1", pick("lfo1_")), ("LFO 2", pick("lfo2_")),
              ("AFTERTOUCH", pick("at_"))])
p.write()
