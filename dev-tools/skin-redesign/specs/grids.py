"""Grids (Mutable Instruments topographic drum sequencer, ported from Mutable's AVR source as a MIDI FX module;
GPL-3.0): plays kick/snare/hat notes out of its own MIDI port for a drum track. Graphite with a red accent. MAP X/Y
pick a spot on the drum map, CHAOS perturbs it, the FILLs set each part's density; in EUCLIDEAN mode the fills and
the three LENGTHs make Euclidean rhythms instead."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port
from _seq import info

p = Port("grids", "Grids", ["TOPOGRAPHIC", "DRUM SEQUENCER"])
p.names({"mode": "MODE", "map_x": "MAP X", "map_y": "MAP Y", "chaos": "CHAOS", "bd_fill": "BD FILL",
         "sd_fill": "SD FILL", "hh_fill": "HH FILL", "len_bd": "BD LENGTH", "len_sd": "SD LENGTH",
         "len_hh": "HH LENGTH", "resolution": "RESOLUTION", "swing": "SWING", "bd_note": "BD NOTE",
         "sd_note": "SD NOTE", "hh_note": "HH NOTE", "accent_vel": "ACCENT VEL", "normal_vel": "NORMAL VEL",
         "channel": "CHANNEL"})
p.look({"bg": "100c0c", "panel": "1b1414", "line": "4a3131", "ink": "fbf0f0", "ink_dim": "e0caca", "ink_faint": "8c7070",
        "accent": "e0463c", "accent_hi": "ff6b5f", "knob_face": "f4eeee", "knob_ring": "332424", "knob_dot": "ff6b5f",
        "lcd": "0a0707", "seg_active": "ff6b5f", "seg_inactive": "2c1f1f", "seg_active_tx": "100c0c", "box": "1b1414"},
       knob="metal",
       led=dict(on="#ff6b5f", off="#3a1512", cap=("#605656", "#443c3c", "#2d2727"), rim="#8c7070", well="#070505"))
p.page("GRIDS",
       [("MAP", 3, [{"key": k, "big": True} for k in ("map_x", "map_y", "chaos")]),
        ("FILL", 3, ["bd_fill", "sd_fill", "hh_fill"]), ("MODE", 2, ["mode", "swing"])],
       [("EUCLIDEAN LENGTHS", 3, ["len_bd", "len_sd", "len_hh"]), info("Grids", 5, last="PRESS PLAY: IT FOLLOWS MPC'S CLOCK")])
p.page("NOTES",
       [("NOTES", 3, ["bd_note", "sd_note", "hh_note"]), ("VELOCITY", 2, ["accent_vel", "normal_vel"]),
        ("MIDI", 3, ["channel", "resolution"])],
       [("@logo", 8)])
p.write()
