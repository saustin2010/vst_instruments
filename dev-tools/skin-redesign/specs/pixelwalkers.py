"""Pixel Walkers (bouncing walkers on platforms that play notes when they land, Schwung MIDI FX): plays out of its own
MIDI port for other tracks. Randomize and Kill All are triggers."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port
from _seq import info, tidy

p = tidy(Port("pixelwalkers", "Pixel Walkers", ["GENERATIVE", "MIDI WALKERS"]))
for k in ("randomize", "kill_all"):
    p.set(k, options=["-", "GO"])
p.names({"birth_note": "BIRTH NOTE", "birth_level": "BIRTH LEVEL", "hit_level": "HIT LEVEL", "hit_decay": "HIT DECAY",
         "randomize": "RANDOMIZE", "bounce": "BOUNCE", "hardness": "HARDNESS", "kill_all": "KILL ALL",
         "tombola": "TOMBOLA"})
p.look({"bg": "0a0a14", "panel": "13132a", "line": "34345e", "ink": "f0f0ff", "ink_dim": "c8c8f0", "ink_faint": "7474a8",
        "accent": "ff4fa3", "accent_hi": "ff7cc0", "knob_face": "f0f0ff", "knob_ring": "232345", "knob_dot": "ff7cc0",
        "lcd": "06060c", "seg_active": "ff7cc0", "seg_inactive": "20203d", "seg_active_tx": "0a0a14", "box": "13132a"},
       knob=None,
       led=dict(on="#ff7cc0", off="#3d1530", cap=("#50507a", "#38385a", "#24243d"), rim="#7474a8", well="#05050a"))
p.page("MAIN",
       [("WALKERS", 4, ["birth_note", "birth_level", "hit_level", "hit_decay"]), info("Pixel Walkers")],
       [("WORLD", 3, ["bounce", "hardness", "tombola"]), ("ACTIONS", 2, ["randomize", "kill_all"]), ("@logo", 3)])
p.write()
