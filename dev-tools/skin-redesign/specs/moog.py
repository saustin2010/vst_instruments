"""Moog (RaffoSynth: 4 oscillators into a ladder filter). Black panel, white legends, orange accents, walnut
surround, black knurled knobs. 14 built-in presets. The wordmark says Raffo, the engine's own name."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("moog", "Raffo", ["4-OSCILLATOR", "LADDER MONOSYNTH"])
WAVES = ["TRIANGLE", "SAW", "SQUARE", "PULSE"]
for n in "1234":
    p.set("osc%s_wave" % n, options=WAVES, name="OSC%s WAVE" % n, display=None)
    # the engine takes the octave itself (-2..2): footage switches that send those values ("values")
    p.set("osc%s_range" % n, name="OSC%s RANGE" % n, options=["32'", "16'", "8'", "4'", "2'"],
          values=[-2, -1, 0, 1, 2], default=2, display=None)
    p.set("osc%s_volume" % n, name="OSC%s LEVEL" % n)
    if n != "1":
        p.set("osc%s_detune" % n, name="OSC%s DETUNE" % n)
p.names({"noise": "NOISE", "cutoff": "CUTOFF", "resonance": "EMPHASIS", "contour": "CONTOUR", "key_follow": "KEY TRACK",
         "attack": "AMP ATTACK", "decay": "AMP DECAY", "sustain": "AMP SUSTAIN", "release": "AMP RELEASE",
         "f_attack": "FLT ATTACK", "f_decay": "FLT DECAY", "f_sustain": "FLT SUSTAIN", "f_release": "FLT RELEASE",
         "glide": "GLIDE", "volume": "VOLUME", "lfo_rate": "LFO RATE", "lfo_pitch": "LFO PITCH", "lfo_filter": "LFO FILTER",
         "mod_filter": "WHEEL FILTER", "mod_pitch": "WHEEL PITCH", "bend_range": "BEND RANGE", "vel_sens": "VELOCITY"})
p.preset_browser(count=14)
p.look({"bg": "2a1a0f", "panel": "131313", "line": "3c3c3c", "ink": "f4f3ef", "ink_dim": "cfcdc6", "ink_faint": "85827a",
        "accent": "e0782a", "accent_hi": "ff9440", "knob_face": "efeee9", "knob_ring": "2a2a2a", "knob_dot": "ff9440",
        "lcd": "0a0a0a", "seg_active": "f4f3ef", "seg_inactive": "232323", "seg_active_tx": "131313", "box": "131313",
        "display_bg": "170b03", "display_cell": "261305", "display_off": "3a1d08", "display_ink": "ff9440",
        "display_bezel": "060606"},
       knob="moog",
       led=dict(on="#ff9440", off="#3a1d08", cap=("#f2f1ec", "#d6d4ce", "#aeaba3"), rim="#6d6a63", well="#050505"),
       css=".frame-border { stroke-width: 2; }\n")
p.page("MAIN",
       [("PROGRAM", 4, [{"key": "preset", "get": "preset_name", "caption": "14 PATCHES  ·  ARROWS OR Q-LINK TO BROWSE"}]),
        ("FILTER", 4, [{"key": "cutoff", "big": True}, "resonance", "contour", "key_follow"])],
       [("FILTER CONTOUR", 4, ["f_attack", "f_decay", "f_sustain", "f_release"]),
        ("LOUDNESS CONTOUR", 4, ["attack", "decay", "sustain", "release"])])
p.page("OSCILLATORS",
       [("OSC 1", 3, ["osc1_wave", {"key": "osc1_range", "kind": "enum_v"}, "osc1_volume"]),
        ("OSC 2", 4, ["osc2_wave", {"key": "osc2_range", "kind": "enum_v"}, "osc2_detune", "osc2_volume"]), ("NOISE", 1, ["noise"])],
       [("OSC 3", 4, ["osc3_wave", {"key": "osc3_range", "kind": "enum_v"}, "osc3_detune", "osc3_volume"]),
        ("OSC 4", 4, ["osc4_wave", {"key": "osc4_range", "kind": "enum_v"}, "osc4_detune", "osc4_volume"])])
p.page("MODULATION",
       [("LFO", 3, ["lfo_rate", "lfo_pitch", "lfo_filter"]), ("MOD WHEEL", 2, ["mod_filter", "mod_pitch"]),
        ("PLAYING", 3, ["glide", "bend_range", "vel_sens"])],
       [("OUTPUT", 2, ["volume"]), ("@logo", 6)])
p.write()
