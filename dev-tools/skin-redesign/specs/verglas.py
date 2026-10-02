"""Verglas (Mutable Instruments Clouds granular processor; Move port by fillioning): an AUDIO EFFECT, the first of
these ports (vst.json "effect": true; insert it on a track or bus). Icy blue on near-black. FREEZE holds the buffer;
MODE switches granular / stretch / looper / spectral."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port
from _seq import tidy

p = tidy(Port("verglas", "Verglas", ["CLOUDS", "GRANULAR FX"]))
p.set("pitch", display="int")
p.names({"position": "POSITION", "size": "SIZE", "pitch": "PITCH", "density": "DENSITY", "texture": "TEXTURE",
         "feedback": "FEEDBACK", "reverb": "REVERB", "dry_wet": "DRY/WET", "mode": "MODE", "freeze": "FREEZE",
         "quality": "QUALITY", "stereo_spread": "SPREAD", "filter_hp": "HIGH PASS", "filter_lp": "LOW PASS",
         "limiter_on": "LIMITER", "limiter_pre": "LIM DRIVE", "limiter_post": "LIM OUTPUT", "low_boost": "LOW BOOST",
         "low_freq": "LOW FREQ", "low_q": "LOW Q"})
p.look({"bg": "090c10", "panel": "111720", "line": "2c3a4c", "ink": "eef5fc", "ink_dim": "c3d2e2", "ink_faint": "6c7f94",
        "accent": "6fb6ff", "accent_hi": "a3d2ff", "knob_face": "eef4fa", "knob_ring": "1f2a36", "knob_dot": "a3d2ff",
        "lcd": "06080b", "seg_active": "a3d2ff", "seg_inactive": "1a2330", "seg_active_tx": "090c10", "box": "111720"},
       knob="metal",
       led=dict(on="#a3d2ff", off="#132a40", cap=("#56606c", "#3b434d", "#262c33"), rim="#6c7f94", well="#05070a"))
p.page("VERGLAS",
       [("GRAINS", 5, [{"key": "position", "big": True}, {"key": "size", "big": True}, "pitch",
                       {"key": "density", "big": True}, {"key": "texture", "big": True}]),
        ("MODE", 3, ["mode", "freeze", "quality"])],
       [("BLEND", 4, [{"key": "dry_wet", "big": True}, "feedback", "reverb", "stereo_spread"]),
        ("FILTER", 2, ["filter_hp", "filter_lp"]), ("@logo", 2)])
p.page("TONE",
       [("LOW SHELF", 3, ["low_boost", "low_freq", "low_q"]), ("LIMITER", 3, ["limiter_on", "limiter_pre", "limiter_post"])])
p.write()
