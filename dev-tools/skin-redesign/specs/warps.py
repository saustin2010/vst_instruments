"""Warps (Mutable Instruments meta-modulator, from Mutable's own source): an AUDIO EFFECT. Its internal oscillator
(CARRIER sine/triangle/saw at NOTE) is crossed with the track's audio along ALGORITHM (crossfade, fold, ring mods,
XOR, comparator, spectral, morph, vocoder); EXTERNAL crosses the input's left with its right. MODE's second entry is
the module's hidden frequency shifter. Graphite with a cyan accent."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("warps", "Warps", ["META", "MODULATOR"])
p.names({"mode": "MODE", "algorithm": "ALGORITHM", "timbre": "TIMBRE", "carrier": "CARRIER", "note": "NOTE",
         "fine": "FINE", "level_1": "CARRIER LVL", "level_2": "MOD LEVEL", "shift": "SHIFT", "output": "OUTPUT",
         "mix": "MIX", "volume": "VOLUME"})
p.look({"bg": "0a0e10", "panel": "12191c", "line": "2c4148", "ink": "eef8fa", "ink_dim": "c3d8dd", "ink_faint": "6d868d",
        "accent": "35b6d6", "accent_hi": "68d8f2", "knob_face": "eef5f7", "knob_ring": "20303a", "knob_dot": "68d8f2",
        "lcd": "060a0b", "seg_active": "68d8f2", "seg_inactive": "1a272b", "seg_active_tx": "0a0e10", "box": "12191c"},
       knob="metal",
       led=dict(on="#68d8f2", off="#0f2f38", cap=("#55606a", "#3a434b", "#262c32"), rim="#6d868d", well="#05080a"))
p.page("WARPS",
       [("MODULATION", 4, ["mode", {"key": "algorithm", "big": True}, {"key": "timbre", "big": True}, "shift"]),
        ("CARRIER", 4, ["carrier", "note", "fine", "level_1"])],
       [("INPUT", 1, ["level_2"]), ("OUTPUT", 3, ["output", "mix", "volume"]), ("@logo", 4)])
p.write()
