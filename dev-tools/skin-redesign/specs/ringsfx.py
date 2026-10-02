"""Rings FX (Mutable Instruments resonator as an AUDIO EFFECT, from Mutable's own source): the track's audio excites
the resonator and its onsets strum it, at NOTE. Same amber look as Rings, with an INPUT and MIX section."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("ringsfx", "Rings FX", ["RESONATOR", "AUDIO EFFECT"])
p.names({"model": "MODEL", "polyphony": "POLYPHONY", "structure": "STRUCTURE", "brightness": "BRIGHTNESS",
         "damping": "DAMPING", "position": "POSITION", "note": "NOTE", "fine": "FINE", "input_gain": "INPUT",
         "mix": "MIX", "width": "WIDTH", "volume": "VOLUME"})
p.look({"bg": "0f0d0b", "panel": "1a1714", "line": "463b30", "ink": "f8f2ec", "ink_dim": "dccfc2", "ink_faint": "8a7a6a",
        "accent": "e39a3b", "accent_hi": "ffb85c", "knob_face": "f2ede8", "knob_ring": "302821", "knob_dot": "ffb85c",
        "lcd": "0a0807", "seg_active": "ffb85c", "seg_inactive": "2a231d", "seg_active_tx": "0f0d0b", "box": "1a1714"},
       knob="metal",
       led=dict(on="#ffb85c", off="#3a2810", cap=("#5f574f", "#433d37", "#2c2824"), rim="#8a7a6a", well="#070605"))
p.page("RINGS FX",
       [("MODEL", 3, [{"key": "model", "span": 2, "w": 280}, "polyphony"]),
        ("RESONATOR", 5, [{"key": k, "big": True} for k in ("structure", "brightness", "damping", "position")])],
       [("PITCH", 2, ["note", "fine"]), ("INPUT / OUTPUT", 4, ["input_gain", "mix", "width", "volume"]), ("@logo", 2)])
p.write()
