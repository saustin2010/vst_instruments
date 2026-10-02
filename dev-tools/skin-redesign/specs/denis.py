"""Denis (West Coast / Serge-inspired mono): Serge-panel black with banana-jack colours, a 4x8 modulation matrix.
Presets are a named list in the engine. Randomise/reset actions become buttons; the Move-only pad mode is hidden."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("denis", "Denis", ["WEST COAST", "MONOSYNTH"])
for k in ("matrix_reset", "rnd_denis", "rnd_mod", "rnd_patch"):
    p.set(k, momentary=True, min=0, max=1, default=0)
p.set("patch_mode", hidden=True)
for k in ("filter_type", "noise_type", "preset", "legato"):
    p.set(k, options=[str(o).upper() for o in p.byk[k]["options"]])
p.names({"osc1_freq": "OSC1 FREQ", "osc1_timbre": "OSC1 TIMBRE", "osc2_pitch": "OSC2 FREQ", "osc2_harmonics": "HARMONICS",
         "osc_mix": "OSC MIX", "fold_depth": "FOLD DEPTH", "fold_type": "FOLD TYPE", "filter_cutoff": "CUTOFF",
         "filter_q": "Q", "filter_type": "FILTER TYPE", "vel_to_filter": "VEL>FILTER", "attack": "ATTACK", "decay": "DECAY",
         "sustain": "SUSTAIN", "release": "RELEASE", "noise_mix": "NOISE MIX", "noise_type": "NOISE TYPE",
         "lfo_rate": "LFO RATE", "sh_rate": "S&H RATE", "mod_depth_env": "ENV DEPTH", "mod_depth_noise": "NOISE DEPTH",
         "preset": "PRESET", "portamento": "PORTAMENTO", "legato": "LEGATO", "matrix_reset": "RESET MATRIX",
         "rnd_denis": "RANDOM SOUND", "rnd_mod": "RANDOM MOD", "rnd_patch": "RANDOM ALL", "patch_mode": "PAD MODE"})
SRC = [("0", "ENV"), ("1", "LFO"), ("2", "S&H"), ("3", "NOISE")]
DST = ["PITCH1", "TIMBRE", "PITCH2", "HARM", "FOLD", "FTYPE", "CUTOFF", "LEVEL"]
for s, sn in SRC:
    for d, dn in enumerate(DST):
        p.set("mat_%s_%d" % (s, d), name=("%s>%s" % (sn, dn))[:12])
p.look({"bg": "0c0c0c", "panel": "151515", "line": "3a3a3a", "ink": "f0f0ea", "ink_dim": "c8c8c0", "ink_faint": "7a7a72",
        "accent": "2f9fd8", "accent_hi": "f0c020", "knob_face": "f0f0ea", "knob_ring": "2a2a2a", "knob_dot": "f0c020",
        "lcd": "080808", "seg_active": "f0c020", "seg_inactive": "222222", "seg_active_tx": "0c0c0c", "box": "151515"},
       knob="metal",
       led=dict(on="#f0c020", off="#3a3008", cap=("#d84a2a", "#b53a20", "#8a2c18"), rim="#5a2010", well="#050505"))
p.page("DENIS",
       [("PATCH", 3, [{"key": "preset", "span": 2, "w": 250}, "rnd_patch"]),
        ("OSCILLATORS", 5, ["osc1_freq", "osc1_timbre", "osc2_pitch", "osc2_harmonics", "osc_mix"])],
       [("WAVEFOLDER", 2, ["fold_depth", "fold_type"]),
        ("FILTER", 4, [{"key": "filter_cutoff", "big": True}, "filter_q", "filter_type", "vel_to_filter"]),
        ("VOICE", 2, ["portamento", "legato"])])
p.page("ENV / MOD",
       [("ENVELOPE", 4, ["attack", "decay", "sustain", "release"]), ("NOISE", 2, ["noise_mix", "noise_type"]),
        ("RANDOM", 2, ["rnd_denis", "rnd_mod"])],
       [("MODULATORS", 4, ["lfo_rate", "sh_rate", "mod_depth_env", "mod_depth_noise"]), ("MATRIX", 2, ["matrix_reset"]),
        ("@logo", 2)])
p.page("ENV LFO", [("ENV TO", 8, ["mat_0_%d" % d for d in range(8)])], [("LFO TO", 8, ["mat_1_%d" % d for d in range(8)])])
p.page("S&H NOISE", [("S&H TO", 8, ["mat_2_%d" % d for d in range(8)])], [("NOISE TO", 8, ["mat_3_%d" % d for d in range(8)])])
p.write()
