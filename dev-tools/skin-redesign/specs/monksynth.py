"""MonkSynth (formant/FOF singing voice, homage to Monk-style vocal synths): deep plum with gold, one page. Its 12
"characters" are its presets (face = preset)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("monksynth", "MonkSynth", ["SINGING", "FORMANT VOICE"])
for k, v in list(p.byk.items()):
    if v.get("type") in ("int", "float", "canvas", "enum"):
        p.set(k, type=None)
p.set("face", hidden=True)            # the same choice as the preset browser
p.set("big_face", hidden=True)        # a Move screen drawing, not a sound parameter
p.set("bend_range", display="int")
p.set("pressure_routing", options=[o.upper() for o in p.byk["pressure_routing"]["options"]])
p.names({"vowel": "VOWEL", "head_size": "HEAD SIZE", "aspiration": "BREATH", "glide": "GLIDE", "vibrato": "VIBRATO",
         "delay": "DELAY", "level": "LEVEL", "vibrato_rate": "VIBRATO RATE", "attack": "ATTACK", "decay": "DECAY",
         "sustain": "SUSTAIN", "release": "RELEASE", "unison": "UNISON", "unison_detune": "DETUNE",
         "unison_spread": "SPREAD", "delay_rate": "DELAY RATE", "pressure_routing": "PRESSURE TO", "bend_range": "BEND RANGE",
         "pressure_depth": "PRES DEPTH", "face": "CHARACTER", "big_face": "FACE"})
p.preset_browser(count=12, name="SINGER")
p.look({"bg": "120a14", "panel": "1d1220", "line": "4a3352", "ink": "f8eefa", "ink_dim": "dcc8e0", "ink_faint": "8c7392",
        "accent": "d9a63a", "accent_hi": "f5c451", "knob_face": "f8eefa", "knob_ring": "3a2741", "knob_dot": "f5c451",
        "lcd": "0b060c", "seg_active": "f5c451", "seg_inactive": "2c1d31", "seg_active_tx": "120a14", "box": "1d1220",
        "display_bg": "160e02", "display_cell": "261a05", "display_off": "3b2a0a", "display_ink": "f5c451",
        "display_bezel": "070408"},
       knob="cap",
       led=dict(on="#f5c451", off="#3b2a0a", cap=("#5c4a62", "#423446", "#2c2230"), rim="#8c7392", well="#080409"))
p.page("SINGER",
       [("CHARACTER", 4, [{"key": "preset", "get": "preset_name", "caption": "12 SINGERS  ·  ARROWS OR Q-LINK TO CHANGE"}]),
        ("VOICE", 4, ["vowel", "head_size", "aspiration", "level"])],
       [("ENVELOPE", 4, ["attack", "decay", "sustain", "release"]),
        ("EXPRESSION", 4, ["glide", "vibrato", "vibrato_rate", "bend_range"])])
p.page("CHOIR",
       [("UNISON", 3, ["unison", "unison_detune", "unison_spread"]), ("ECHO", 2, ["delay", "delay_rate"]),
        ("PRESSURE", 3, ["pressure_routing", "pressure_depth"])],
       [("@logo", 8)])
p.write()
