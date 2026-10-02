"""Maze Lite (two Turing-machine style shift-register sequencers, Schwung MIDI FX): plays out of its own MIDI port for
other tracks. Bit Flip / Advance are triggers; the state readouts (running, bits, play heads) stay off the pages."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port
from _seq import info, tidy

p = tidy(Port("mazelite", "Maze Lite", ["SHIFT REGISTER", "MIDI SEQUENCER"]))
for k in ("running", "s1_bits", "s2_bits", "s1_play", "s2_play"):
    p.set(k, hidden=True)
for k in ("s1_bits", "s2_bits"):
    p.set(k, type=None, display="string", min=0, max=0)
for n in "12":
    p.set("s%s_bit_flip" % n, options=["OFF", "GO"])
    p.set("s%s_advance" % n, options=["OFF", "GO"])
    p.names({"s%s_corrupt" % n: "S%s CORRUPT" % n, "s%s_cv_range" % n: "S%s RANGE" % n,
             "s%s_length" % n: "S%s LENGTH" % n, "s%s_bit_flip" % n: "S%s BIT FLIP" % n,
             "s%s_advance" % n: "S%s ADVANCE" % n, "s%s_reset" % n: "S%s RESET" % n,
             "s%s_bits" % n: "S%s BITS" % n, "s%s_play" % n: "S%s PLAY" % n})
p.names({"trig_mix": "S1 TRIG MIX", "trig_mix_b": "S2 TRIG MIX", "g_reset": "RESET BOTH", "scale": "SCALE",
         "note_rate": "NOTE RATE", "note_length": "NOTE LENGTH", "running": "RUNNING"})
p.look({"bg": "0c0c0c", "panel": "171717", "line": "3d3d3d", "ink": "f4f4f4", "ink_dim": "cfcfcf", "ink_faint": "7a7a7a",
        "accent": "e8d23a", "accent_hi": "fff066", "knob_face": "f4f4f4", "knob_ring": "2a2a2a", "knob_dot": "fff066",
        "lcd": "070707", "seg_active": "fff066", "seg_inactive": "262626", "seg_active_tx": "0c0c0c", "box": "171717"},
       knob=None,
       led=dict(on="#fff066", off="#3a3510", cap=("#585858", "#3e3e3e", "#282828"), rim="#7a7a7a", well="#060606"))
p.page("MAIN", [("OUTPUT", 4, ["scale", "note_rate", "note_length", "g_reset"]), info("Maze Lite")],
       [("@logo", 8)])
S = lambda n, k: "s%s_%s" % (n, k)
p.page("SEQUENCES", *[[("SEQUENCE %s" % n, 8, [S(n, "corrupt"), S(n, "cv_range"), S(n, "length"),
                                               "trig_mix" if n == "1" else "trig_mix_b", S(n, "reset"),
                                               S(n, "bit_flip"), S(n, "advance")])] for n in "12"])
p.write()
