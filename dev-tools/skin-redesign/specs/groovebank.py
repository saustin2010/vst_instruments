"""Groove Bank (retriggers the chord you hold in a library groove, Schwung MIDI FX): plays out of its own MIDI port for
other tracks. Its groove library (src/patterns, *.groove) ships to /sdcard/vst/groovebank/patterns."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port
from _seq import info, tidy

p = tidy(Port("groovebank", "Groove Bank", ["CHORD", "GROOVE LIBRARY"]))
p.preset_browser(key="pattern", name_key="pattern_label", count=600, name="GROOVE")
p.set("latch", options=["OFF", "ON"], display=None)
p.names({"variant": "VARIANT", "swing": "SWING", "gate": "GATE", "strum": "STRUM", "accent": "ACCENT", "latch": "LATCH"})
p.look({"bg": "120d08", "panel": "1d1610", "line": "4a3b2b", "ink": "fbf3ea", "ink_dim": "e0d2c1", "ink_faint": "8b7863",
        "accent": "e0a040", "accent_hi": "ffc266", "knob_face": "fbf3ea", "knob_ring": "33281d", "knob_dot": "ffc266",
        "lcd": "0a0705", "seg_active": "ffc266", "seg_inactive": "2c2219", "seg_active_tx": "120d08", "box": "1d1610",
        "display_bg": "140d02", "display_cell": "241806", "display_off": "3a280b", "display_ink": "ffc266",
        "display_bezel": "080604"},
       knob=None,
       led=dict(on="#ffc266", off="#3a280b", cap=("#5e5244", "#433a30", "#2c261f"), rim="#8b7863", well="#080604"))
p.page("MAIN",
       [("GROOVE", 4, [{"key": "pattern", "get": "pattern_label", "caption": "HOLD A CHORD  ·  ARROWS OR Q-LINK TO BROWSE"}]),
        info("Groove Bank")],
       [("FEEL", 6, ["variant", "swing", "gate", "strum", "accent", "latch"]), ("@logo", 2)])
p.write(module_dir="/sdcard/vst/groovebank")
