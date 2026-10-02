"""Hank (2-op FM): one page. Warm amber on dark brown, an amber LED patch display. 32 built-in presets."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("hank", "Hank", ["2-OP FM"])
p.names({"ratio": "RATIO", "bright": "BRIGHT", "bite": "BITE", "tone": "TONE", "attack": "ATTACK", "decay": "DECAY",
         "sustain": "SUSTAIN", "noise": "NOISE", "voice_count": "VOICES", "glide": "GLIDE", "pitch": "TRANSPOSE",
         "volume": "VOLUME"})
p.set("voice_count", display="int").set("pitch", display="int")
p.preset_browser(count=32)
p.look({"bg": "120e0b", "panel": "1e1813", "line": "4a3b2c", "ink": "f4ecdb", "ink_dim": "d6c8aa", "ink_faint": "8c7c61",
        "accent": "d9982f", "accent_hi": "ffbe4a", "knob_face": "efe6d2", "knob_ring": "3a2f24", "knob_dot": "ffbe4a",
        "lcd": "0c0906", "seg_active": "f4ecdb", "seg_inactive": "2a221b", "seg_active_tx": "120e0b", "box": "1e1813",
        "display_bg": "171004", "display_cell": "271b08", "display_off": "3b2a0f", "display_ink": "ffb238",
        "display_bezel": "070503"},
       knob="chicken",
       led=dict(on="#ffb238", off="#3b2a0f", cap=("#5c5248", "#3e362e", "#28221c"), rim="#7a6e60", well="#080604"))
p.page("HANK",
       [("PROGRAM", 4, [{"key": "preset", "get": "preset_name", "caption": "32 PATCHES  ·  ARROWS OR Q-LINK TO BROWSE"}]),
        ("OPERATOR", 4, ["ratio", "bright", "bite", "tone"])],
       [("ENVELOPE", 3, ["attack", "decay", "sustain"]),
        ("VOICE", 5, ["noise", "glide", "voice_count", "pitch", "volume"])])
p.write()
