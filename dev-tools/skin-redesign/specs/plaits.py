"""Plaits (Mutable Instruments macro oscillator, 24 models incl. the three 6-op FM banks; Move port by Justin Joe):
graphite with Plaits' green LED. FM PATCH browses the active 6-op bank by number (a small engine patch adds
fm_preset_index) and shows the patch's name; option words go to the engine as "send" strings (its LEGATO only
parses "on"/"off")."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port
from _seq import tidy

p = Port("plaits", "Plaits", ["MACRO", "OSCILLATOR"])
p.rename("fm_preset", "fm_preset_index")
p.preset_browser(key="fm_preset_index", name_key="fm_preset", count=32, name="FM PATCH")
tidy(p, text_options=True)
p.names({"engine": "MODEL", "harmonics": "HARMONICS", "timbre": "TIMBRE", "morph": "MORPH", "decay": "LPG DECAY",
         "lpg_colour": "LPG COLOUR", "attack": "ATTACK", "fm_amount": "FM", "timbre_mod": "TIMBRE MOD",
         "morph_mod": "MORPH MOD", "aux_mix": "AUX MIX", "octave_transpose": "OCTAVE", "legato": "LEGATO",
         "velocity_sensitivity": "VELOCITY"})
p.look({"bg": "0d0f0e", "panel": "171a18", "line": "3a423d", "ink": "f1f5f2", "ink_dim": "c9d2cc", "ink_faint": "77847b",
        "accent": "5fc44a", "accent_hi": "8ae673", "knob_face": "eef2ef", "knob_ring": "29302b", "knob_dot": "8ae673",
        "lcd": "080a09", "seg_active": "8ae673", "seg_inactive": "222824", "seg_active_tx": "0d0f0e", "box": "171a18",
        "display_bg": "071207", "display_cell": "0d200d", "display_off": "163416", "display_ink": "8ae673",
        "display_bezel": "040604"},
       knob="metal",
       led=dict(on="#8ae673", off="#163416", cap=("#59605b", "#3e4440", "#282c29"), rim="#77847b", well="#050605"))
p.page("PLAITS",
       [("MODEL", 2, [{"key": "engine", "span": 2, "w": 280}]),
        ("SOUND", 6, [{"key": "harmonics", "big": True}, {"key": "timbre", "big": True}, {"key": "morph", "big": True}])],
       [("LOW PASS GATE", 3, ["decay", "lpg_colour", "attack"]), ("MODULATION", 3, ["fm_amount", "timbre_mod", "morph_mod"]),
        ("OUTPUT", 2, ["aux_mix", "octave_transpose"])])
p.page("PLAY",
       [("6-OP FM PATCH", 4, [{"key": "fm_preset_index", "get": "fm_preset",
                               "caption": "6-OP MODELS  ·  32 PATCHES PER BANK"}]),
        ("PLAYING", 2, ["legato", "velocity_sensitivity"]), ("@logo", 2)])
p.write()
