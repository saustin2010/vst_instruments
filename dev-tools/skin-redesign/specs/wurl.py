"""Wurl (physically modelled Wurlitzer 200A, OpenWurli port): cream tolex and walnut, one page. Ten presets, named
from the engine's own preset table comments."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("wurl", "Wurl", ["ELECTRIC PIANO", "200A"])
p.names({"volume": "VOLUME", "tremolo": "TREMOLO", "attack": "ATTACK", "decay": "DECAY", "brightness": "BRIGHTNESS",
         "darken": "DARKEN", "bark": "BARK", "reverb": "REVERB", "speaker": "SPEAKER", "tune": "TUNE"})
p.set("preset", name="PRESET", options=["CLASSIC 200A", "DREAMY KEYS", "BARKY SOUL", "SURF SPRING", "DARK BALLAD",
                                         "PERC CLAV", "WARM PAD", "LO-FI TAPE", "BRIGHT BELL", "GOSPEL GROWL"],
      default=0, display=None)
p.look({"bg": "2b1d14", "panel": "efe6d2", "line": "b9a98a", "ink": "2a2018", "ink_dim": "4a3d30", "ink_faint": "8a7a62",
        "accent": "b5532b", "accent_hi": "d4622e", "knob_face": "efe9dc", "knob_ring": "6b5a45", "knob_dot": "b5532b",
        "lcd": "2a2018", "seg_active": "2a2018", "seg_inactive": "e2d7bf", "seg_active_tx": "f3ecdc", "box": "efe6d2"},
       knob="moog",
       led=dict(on="#ff5a2a", off="#5a2a18", cap=("#3d3127", "#2c231b", "#1e1812"), rim="#6b5a45", well="#1a130d"),
       css=".frame-border { stroke-width: 2; }\n")
p.page("WURL",
       [("PRESET", 2, [{"key": "preset", "span": 2, "w": 250}]), ("TONE", 4, ["brightness", "darken", "bark", "tune"]),
        ("@logo", 2)],
       [("AMP", 3, ["attack", "decay", "volume"]), ("CABINET", 5, ["tremolo", "speaker", "reverb"])])
p.write()
