"""Chordism (one key in, a four-voice chord out; morphing oscillators, FM, filter, lo-fi, delay, reverb, an
arpeggiator): dusk violet with warm pink. 135 parameters: a hand-made MAIN page, the rest packed by section.
Options keep the engine's own text (upper-cased)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("chordism", "Chordism", ["CHORD", "SYNTHESIZER"])
for k, v in list(p.byk.items()):
    if v.get("options"):
        p.set(k, options=[str(o).upper() for o in v["options"]])
p.short_names(abbrev={"MORPH": "MRPH", "SHIMMER": "SHIMR"})
p.names({"chord_type": "CHORD", "tuning_mode": "TUNING", "scale_index": "SCALE", "scale_root": "ROOT",
         "filter_cutoff": "CUTOFF", "filter_resonance": "RESONANCE", "filter_mode": "FILTER MODE", "filter_slope": "SLOPE",
         "attack": "ATTACK", "release": "RELEASE", "vca_mode": "VCA MODE", "volume": "VOLUME", "detune": "DETUNE",
         "width": "WIDTH", "chord_spread": "SPREAD", "chord_rotation": "ROTATION"})
p.look({"bg": "100a14", "panel": "1b1222", "line": "4a3358", "ink": "fbeefa", "ink_dim": "e2cbe4", "ink_faint": "8e7296",
        "accent": "e2588f", "accent_hi": "ff7aac", "knob_face": "fbeefa", "knob_ring": "36243f", "knob_dot": "ff7aac",
        "lcd": "0a060c", "seg_active": "ff7aac", "seg_inactive": "2b1d33", "seg_active_tx": "100a14", "box": "1b1222"},
       knob=None,
       led=dict(on="#ff7aac", off="#3d1428", cap=("#5c4a68", "#43354c", "#2d2333"), rim="#8e7296", well="#09050b"))
MAIN = ["chord_type", "tuning_mode", "scale_index", "scale_root", "detune", "width", "chord_spread", "chord_rotation",
        "filter_cutoff", "filter_resonance", "filter_mode", "filter_slope", "attack", "release", "vca_mode", "volume"]
p.page("MAIN",
       [("CHORD", 4, ["chord_type", "tuning_mode", "scale_index", "scale_root"]),
        ("VOICES", 4, ["detune", "width", "chord_spread", "chord_rotation"])],
       [("FILTER", 4, [{"key": "filter_cutoff", "big": True}, "filter_resonance", "filter_mode", "filter_slope"]),
        ("AMP", 4, ["attack", "release", "vca_mode", "volume"])])
placed = set(MAIN)
def pick(*pre, exclude=()):
    ks = [k for k in p.keys(*pre) if k not in placed and k not in exclude]
    placed.update(ks)
    return ks
p.auto_pages([("OSCILLATORS", pick("wave_", "mix_")), ("SHAPE", pick("shape", "lfo_phase_")),
              ("MORPH", pick("morph_", "pan_morph_")), ("FM", pick("fm_")),
              ("FILTER ENV", pick("filter_env_", "fenv_", "drive", "quality_position")), ("FILTER LFO", pick("filter_lfo_")),
              ("LFO", pick("lfo_")), ("VIBRATO", pick("vib_")), ("SWEEP", pick("sweep_")),
              ("LEVEL MORPH LFO", pick("lm_lfo_")), ("PAN MORPH LFO", pick("pm_lfo_")), ("TREMOLO", pick("amp_lfo_")),
              ("VCA / GLIDE", pick("vca_", "glide_")), ("LO-FI", pick("grind", "bit_shift", "decimator")),
              ("DELAY", pick("delay_")), ("REVERB", pick("reverb_")), ("CHORD MAP", pick("chord_pc_")),
              ("INTERVALS", pick("interval_")), ("CONTROLLER", pick("ctrl_")), ("ARPEGGIATOR", pick("arp_"))])
p.write()
