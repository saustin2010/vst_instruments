"""Helm (Matt Tytel's polysynth, headless JUCE build): Helm's own dark slate with its orange-yellow accent. 275
factory patches (CC BY 4.0, by Matt Tytel and contributors) ship to /sdcard/vst/helm/helm-data/patches (see the
build.sh data arguments in its README). Steps 17-32 of the step sequencer stay off the pages (still automatable)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("helm", "Helm", ["POLYPHONIC", "SYNTHESIZER"])
for k, v in list(p.byk.items()):
    if v.get("options"):
        p.set(k, options=[str(o).upper() for o in v["options"]])
for i in range(16, 32):
    p.set("step_seq_%02d" % i, hidden=True)
p.set("filter_type", display="int")
p.short_names(abbrev={"UNISON": "UNI", "WAVEFORM": "WAVE", "FREQUENCY": "FREQ", "RETRIGGER": "RETRIG",
                      "TRANSPOSE": "TRANSP", "SATURATION": "SAT", "HARMONIZE": "HARM", "AMPLITUDE": "AMP"})
p.names({"cutoff": "CUTOFF", "resonance": "RESONANCE", "volume": "VOLUME", "polyphony": "POLYPHONY",
         "octave_transpose": "OCTAVE", "legato": "LEGATO", "amp_attack": "AMP ATTACK", "amp_decay": "AMP DECAY",
         "amp_sustain": "AMP SUSTAIN", "amp_release": "AMP RELEASE", "filter_type": "FILTER TYPE"})
p.preset_browser(count=275)
p.look({"bg": "16191d", "panel": "20252b", "line": "3d4651", "ink": "eef2f5", "ink_dim": "c4ccd4", "ink_faint": "78838e",
        "accent": "e8a33a", "accent_hi": "ffc35c", "knob_face": "eef2f5", "knob_ring": "2c333b", "knob_dot": "ffc35c",
        "lcd": "0e1013", "seg_active": "ffc35c", "seg_inactive": "2a3038", "seg_active_tx": "16191d", "box": "20252b",
        "display_bg": "151003", "display_cell": "241b06", "display_off": "3a2c0b", "display_ink": "ffc35c",
        "display_bezel": "08090b"},
       knob=None,
       led=dict(on="#ffc35c", off="#3a2c0b", cap=("#56606c", "#3e4650", "#2a3038"), rim="#78838e", well="#0a0c0e"))
S = lambda k: {"key": k, "kind": "slider"}
MAIN = ["preset", "volume", "polyphony", "octave_transpose", "legato", "cutoff", "resonance", "filter_type",
        "amp_attack", "amp_decay", "amp_sustain", "amp_release"]
p.page("MAIN",
       [("PATCH", 4, [{"key": "preset", "get": "preset_name", "caption": "275 PATCHES  ·  ARROWS OR Q-LINK TO BROWSE"}]),
        ("VOICE", 4, ["volume", "polyphony", "octave_transpose", "legato"])],
       [("FILTER", 4, [{"key": "cutoff", "big": True}, "resonance", "filter_type"]),
        ("AMP ENVELOPE", 4, [S("amp_attack"), S("amp_decay"), S("amp_sustain"), S("amp_release")])])
placed = set(MAIN)
def pick(*pre):
    ks = [k for k in p.keys(*pre) if k not in placed]
    placed.update(ks)
    return ks
p.auto_pages([("OSC 1", pick("osc_1_", "unison_1_")), ("OSC 2", pick("osc_2_", "unison_2_")),
              ("OSC MIX", pick("osc_mix", "cross_modulation", "osc_feedback_")), ("SUB / NOISE", pick("sub_", "noise_")),
              ("FILTER", pick("filter_", "keytrack", "fil_env_depth")), ("FORMANT", pick("formant_")),
              ("FILTER ENV", pick("fil_")), ("MOD ENV", pick("mod_attack", "mod_decay", "mod_sustain", "mod_release")),
              ("MONO LFO 1", pick("mono_lfo_1_")), ("MONO LFO 2", pick("mono_lfo_2_")), ("POLY LFO", pick("poly_lfo_")),
              ("STEP SEQ", pick("num_steps", "step_frequency", "step_sequencer_", "step_smoothing")),
              ("STEPS", pick("step_seq_")), ("ARPEGGIATOR", pick("arp_")), ("DISTORTION", pick("distortion_")),
              ("DELAY", pick("delay_")), ("REVERB", pick("reverb_")), ("STUTTER", pick("stutter_")),
              ("PLAYING", pick("portamento", "pitch_bend_range", "velocity_track", "bpm", "sync_bpm")),
              ("MOD AMOUNTS", pick("mod_"))])
p.write(module_dir="/sdcard/vst/helm")
