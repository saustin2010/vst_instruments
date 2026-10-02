"""NuSaw (detuned multi-saw poly): trance-club neon, cyan LED patch display. 27 built-in presets."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("nusaw", "NuSaw", ["SUPERSAW", "POLYSYNTH"])
p.names({"cutoff": "CUTOFF", "resonance": "RESONANCE", "detune": "DETUNE", "spread": "SPREAD", "f_amount": "FILTER ENV",
         "attack": "AMP ATTACK", "decay": "AMP DECAY", "sustain": "AMP SUSTAIN", "release": "AMP RELEASE",
         "f_attack": "FLT ATTACK", "f_decay": "FLT DECAY", "f_sustain": "FLT SUSTAIN", "f_release": "FLT RELEASE",
         "volume": "VOLUME", "vel_sens": "VELOCITY", "bend_range": "BEND RANGE", "sub_level": "SUB LEVEL",
         "sub_octave": "SUB OCTAVE", "saw_count": "SAWS", "chorus_mix": "CHORUS", "chorus_depth": "CHORUS DEPTH",
         "delay_time": "DELAY TIME", "delay_fback": "DELAY FBK", "delay_mix": "DELAY", "delay_tone": "DELAY TONE"})
p.preset_browser(count=27)
p.look({"bg": "07060d", "panel": "110f1c", "line": "2f2a52", "ink": "f1efff", "ink_dim": "c6c1ea", "ink_faint": "6f6a99",
        "accent": "19c6e6", "accent_hi": "4fe6ff", "knob_face": "eef0ff", "knob_ring": "221f3d", "knob_dot": "4fe6ff",
        "lcd": "05040a", "seg_active": "4fe6ff", "seg_inactive": "1c1930", "seg_active_tx": "07060d", "box": "110f1c",
        "display_bg": "02101a", "display_cell": "051c2a", "display_off": "0a2b3f", "display_ink": "4fe6ff",
        "display_bezel": "030308"},
       knob=None,
       led=dict(on="#4fe6ff", off="#0a2b3f", cap=("#4c4870", "#353150", "#231f38"), rim="#6f6a99", well="#040309"))
S = lambda k: {"key": k, "kind": "slider"}
p.page("MAIN",
       [("PROGRAM", 4, [{"key": "preset", "get": "preset_name", "caption": "27 PATCHES  ·  ARROWS OR Q-LINK TO BROWSE"}]),
        ("SAWS", 4, ["saw_count", "detune", "spread", "sub_level"])],
       [("FILTER", 4, [{"key": "cutoff", "big": True}, "resonance", "f_amount"]),
        ("AMP ENVELOPE", 4, [S("attack"), S("decay"), S("sustain"), S("release")])])
p.page("MORE",
       [("FILTER ENVELOPE", 4, [S("f_attack"), S("f_decay"), S("f_sustain"), S("f_release")]),
        ("PLAYING", 4, ["sub_octave", "vel_sens", "bend_range", "volume"])],
       [("CHORUS", 2, ["chorus_mix", "chorus_depth"]), ("DELAY", 4, ["delay_mix", "delay_time", "delay_fback", "delay_tone"]),
        ("@logo", 2)])
p.write()
