"""Mr Hyde (Plaits: 17 models + LPG, filter, a 6x6 modulation matrix): near-black violet with a toxic green accent.
No presets in this engine. Its engine reports options as TEXT (matched case-insensitively): parameter options keep
the engine's words (upper-cased); popups/segments draw friendlier labels via "opts"."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("mrhyde", "Mr Hyde", ["MACRO", "OSCILLATOR"])
for k, v in list(p.byk.items()):
    if v.get("options"):
        p.set(k, options=[str(o).upper() for o in v["options"]])
    if v.get("type") in ("int", "float"):
        p.set(k, type=None)
    if v.get("display") is None and k.endswith("_ms"):
        p.set(k, display="int")
p.set("polyphony", display="int").set("unison", display="int")
p.names({"model": "MODEL", "pitch": "PITCH", "harmonics": "HARMONICS", "timbre": "TIMBRE", "morph": "MORPH",
         "fm_amount": "FM AMOUNT", "aux_mix": "AUX MIX", "lpg_decay": "LPG DECAY", "lpg_color": "LPG COLOR",
         "filter_mode": "FILTER MODE", "filter_cutoff": "CUTOFF", "filter_resonance": "RESONANCE",
         "assign1_target": "A1 TARGET", "assign2_target": "A2 TARGET",
         "lfo_shape": "LFO SHAPE", "lfo_rate": "LFO RATE", "lfo_rate_mode": "LFO SYNC", "lfo_retrig": "LFO RETRIG",
         "lfo_phase": "LFO PHASE", "env_attack_ms": "ENV ATTACK", "env_decay_ms": "ENV DECAY", "env_sustain": "ENV SUSTAIN",
         "env_release_ms": "ENV RELEASE", "env_retrig": "ENV RETRIG", "cycle_attack_ms": "CYC ATTACK",
         "cycle_decay_ms": "CYC DECAY", "cycle_shape": "CYC SHAPE", "cycle_sync": "CYC SYNC", "cycle_retrig": "CYC RETRIG",
         "cycle_bipolar": "CYC BIPOLAR", "random_mode": "RND MODE", "random_rate": "RND RATE", "random_rate_mode": "RND SYNC",
         "random_slew": "RND SLEW", "random_retrig": "RND RETRIG", "velocity_curve": "VEL CURVE",
         "poly_aftertouch_curve": "AT CURVE", "volume": "VOLUME", "pan": "PAN", "voice_mode": "VOICE MODE",
         "polyphony": "POLYPHONY", "unison": "UNISON", "detune": "DETUNE", "spread": "SPREAD", "glide_ms": "GLIDE"})
SRC = [("lfo", "LFO"), ("env", "ENV"), ("cycle_env", "CYCLE"), ("random", "RANDOM"), ("velocity", "VEL"),
       ("poly_aftertouch", "AT")]
DEST = [("assign1", "A1"), ("assign2", "A2"), ("pitch", "PITCH"), ("harmonics", "HARM"), ("timbre", "TIMB"), ("cutoff", "CUT")]
for d, dn in DEST:
    for s, sn in SRC:
        p.set("%s_mod_%s_amt" % (d, s), name="%s %s" % (dn, sn))
TARGETS = ["OFF", "MORPH", "FM AMOUNT", "LPG DECAY", "LPG COLOR", "FILTER RES", "PITCH", "HARMONICS", "TIMBRE",
           "CUTOFF", "VOLUME", "PAN", "DETUNE", "SPREAD"]
p.look({"bg": "0a0910", "panel": "15131d", "line": "3a3350", "ink": "f2eff9", "ink_dim": "c9c1dc", "ink_faint": "7c7394",
        "accent": "6fc43d", "accent_hi": "93ee5a", "knob_face": "ece8f5", "knob_ring": "2b2640", "knob_dot": "93ee5a",
        "lcd": "07060b", "seg_active": "93ee5a", "seg_inactive": "221e30", "seg_active_tx": "0a0910", "box": "15131d",
        "display_bg": "071005", "display_cell": "0c1c08", "display_off": "15300d", "display_ink": "93ee5a",
        "display_bezel": "040306"},
       knob="metal",
       led=dict(on="#93ee5a", off="#16300d", cap=("#5b5670", "#3f3b50", "#2a2737"), rim="#7d7896", well="#060509"))
mod_row = lambda d, dn: [("%s MOD" % dn if d not in ("assign1", "assign2") else ("ASSIGN %s" % dn[1]), 8,
                          ([{"key": "%s_target" % d, "span": 2, "w": 250, "opts": TARGETS}] if d.startswith("assign") else [])
                          + ["%s_mod_%s_amt" % (d, s) for s, _ in SRC])]
p.page("MAIN",
       [("OSCILLATOR", 6, [{"key": "model", "span": 2, "w": 250}, "pitch", "harmonics", "timbre", "morph"]),
        ("LPG", 2, ["lpg_decay", "lpg_color"])],
       [("FILTER", 4, [{"key": "filter_mode", "opts": ["LP", "BP", "HP"]}, {"key": "filter_cutoff", "big": True},
                       "filter_resonance"]),
        ("FM / AUX", 2, ["fm_amount", "aux_mix"]), ("OUTPUT", 2, ["volume", "pan"])])
p.page("LFO ENV",
       [("LFO", 6, [{"key": "lfo_shape", "opts": ["SINE", "TRIANGLE", "SAW", "SQUARE", "RANDOM", "SMOOTH RND"]},
                    "lfo_rate", "lfo_rate_mode", "lfo_retrig", "lfo_phase"]),
        ("CURVES", 2, ["velocity_curve", "poly_aftertouch_curve"])],
       [("ENVELOPE", 8, ["env_attack_ms", "env_decay_ms", "env_sustain", "env_release_ms", "env_retrig"])])
p.page("CYC RAND",
       [("CYCLE ENVELOPE", 8, ["cycle_attack_ms", "cycle_decay_ms",
                               {"key": "cycle_shape", "opts": ["LINEAR", "EXPO", "LOG"]},
                               "cycle_sync", "cycle_retrig", "cycle_bipolar"])],
       [("RANDOM", 8, [{"key": "random_mode", "opts": ["S & H", "SMOOTH", "DRIFT"]}, "random_rate", "random_rate_mode",
                       "random_slew", "random_retrig"])])
p.page("ASSIGN", mod_row("assign1", "A1"), mod_row("assign2", "A2"))
p.page("PITCH HARM", mod_row("pitch", "PITCH"), mod_row("harmonics", "HARMONICS"))
p.page("TIMB CUT", mod_row("timbre", "TIMBRE"), mod_row("cutoff", "CUTOFF"))
p.page("VOICE",
       [("VOICE", 8, [{"key": "voice_mode", "opts": ["MONO", "POLY", "LEGATO"]}, "polyphony", "unison", "detune",
                      "spread", "glide_ms"])],
       [("@logo", 8)])
p.write()
