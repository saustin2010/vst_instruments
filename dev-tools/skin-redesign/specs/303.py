"""303 (Open303 + Devilfish): the TB-303's silver panel, black knobs, orange accents. No presets in this engine."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("303", "303", ["BASS LINE", "+ DEVILFISH MOD"])
p.names({"cutoff": "CUTOFF", "resonance": "RESONANCE", "env_mod": "ENV MOD", "decay": "DECAY", "accent": "ACCENT",
         "volume": "VOLUME", "waveform": "WAVEFORM", "tuning": "TUNING", "devil_mod_switch": "DEVILFISH",
         "normal_decay": "NORMAL DECAY", "accent_decay": "ACCENT DECAY", "feedback_hpf": "FEEDBACK HPF",
         "soft_attack": "SOFT ATTACK", "slide_time": "SLIDE TIME", "tanh_shaper_drive": "SHAPER DRIVE",
         "drive_model": "DRIVE MODEL", "drive": "DRIVE", "drive_mix": "DRIVE MIX"})
p.set("waveform", options=["SAW", "SQUARE"]).set("drive_model", options=["SOFT", "RAT"])
p.set("devil_mod_switch", options=["OFF", "ON"])
p.look({"bg": "aeb1b4", "panel": "d3d5d6", "line": "8e9196", "ink": "17181b", "ink_dim": "34373c", "ink_faint": "5f6369",
        "accent": "d9531e", "accent_hi": "f0621f", "knob_face": "e9e7e0", "knob_ring": "7d8085", "knob_dot": "d9531e",
        "lcd": "1b1c1f", "seg_active": "1c1d20", "seg_inactive": "e3e4e5", "seg_active_tx": "f3f3f1", "box": "d3d5d6"},
       knob="moog",
       led=dict(on="#ff4a1c", off="#5a2416", cap=("#f4f4f2", "#d8d9da", "#b4b6b9"), rim="#6f7277", well="#4a4d52"),
       css=".frame-border { stroke-width: 2; }\n")
p.page("303",
       [("VCO", 2, ["waveform", "tuning"]), ("VCF", 4, [{"key": "cutoff", "big": True}, "resonance", "env_mod", "decay"]),
        ("ACCENT", 1, ["accent"]), ("OUT", 1, ["volume"])],
       [("DRIVE", 5, ["drive_model", "drive", "drive_mix", "tanh_shaper_drive"]), ("@logo", 3)])
p.page("DEVILFISH",
       [("DEVILFISH ENVELOPES", 8, ["devil_mod_switch", "normal_decay", "accent_decay", "soft_attack"])],
       [("FILTER / SLIDE", 8, ["feedback_hpf", "slide_time"])])
p.write()
