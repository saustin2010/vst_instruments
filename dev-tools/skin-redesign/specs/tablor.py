"""Tablor (2-oscillator wavetable synth): deep teal glass with a mint wavetable display. Its wavetables ship to
/sdcard/vst/tablor/wavetables (build.sh tablor src/wavetables:wavetables); an engine patch (see VENDORED.md) scans
that folder and adds wt1_name/wt2_name, so each oscillator gets a stepper that shows its table's name."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("tablor", "Tablor", ["WAVETABLE", "SYNTHESIZER"])
for k, v in list(p.byk.items()):
    if v.get("options"):
        p.set(k, options=[str(o).upper() for o in v["options"]])
for osc in "12":
    p.add("wt%s_select" % osc, "WT%s TABLE" % osc, min=0, max=127, default=0, display="int", type="stepper")
    p.add("wt%s_select_prev" % osc, "WT%s PREV" % osc, momentary=True, step_of="wt%s_select" % osc, step_delta=-1)
    p.add("wt%s_select_next" % osc, "WT%s NEXT" % osc, momentary=True, step_of="wt%s_select" % osc, step_delta=1)
    p.add("wt%s_name" % osc, "WT%s NAME" % osc, min=0, max=0, display="string", type="readout")
p.short_names(abbrev={"FORMANT": "FORMNT"})
p.names({"wt1_pos": "WT1 POSITION", "wt2_pos": "WT2 POSITION", "flt_freq": "CUTOFF", "flt_res": "RESONANCE",
         "flt_type": "FILTER TYPE", "flt_env": "FILTER ENV", "flt_key": "KEY TRACK", "flt_vel": "VEL TRACK",
         "vca_vel": "VELOCITY", "pb_range": "BEND RANGE", "volume": "VOLUME"})
p.look({"bg": "05100f", "panel": "0b1c1b", "line": "21504b", "ink": "eafffb", "ink_dim": "bfe9e2", "ink_faint": "5f9a91",
        "accent": "2ec4a6", "accent_hi": "5ff0cf", "knob_face": "eafffb", "knob_ring": "143b37", "knob_dot": "5ff0cf",
        "lcd": "040b0a", "seg_active": "5ff0cf", "seg_inactive": "123230", "seg_active_tx": "05100f", "box": "0b1c1b",
        "display_bg": "031512", "display_cell": "06221d", "display_off": "0d3a32", "display_ink": "5ff0cf",
        "display_bezel": "020807"},
       knob=None,
       led=dict(on="#5ff0cf", off="#0d3a32", cap=("#3f5e5a", "#2c4542", "#1c2f2d"), rim="#5f9a91", well="#030908"))
WT = lambda o: {"key": "wt%s_select" % o, "get": "wt%s_name" % o, "full": False, "span": 4, "w": 600, "h": 64, "dy": 128,
                "label": "WAVETABLE %s" % o}
p.page("MAIN",
       [("OSC 1", 8, [WT(1), "wt1_pos", "wt1_level", "wt1_tune", "wt1_uni"])],
       [("OSC 2", 8, [WT(2), "wt2_pos", "wt2_level", "wt2_tune", "wt2_uni"])])
placed = {"wt1_select", "wt2_select", "wt1_pos", "wt1_level", "wt1_tune", "wt1_uni", "wt2_pos", "wt2_level", "wt2_tune",
          "wt2_uni"}
secs = [("FILTER", ["flt_freq", "flt_res", "flt_type", "flt_env", "flt_key", "flt_vel"]),
        ("SUB / NOISE", ["sub_level", "sub_wave", "sub_tune", "noise_level", "noise_type"]),
        ("UNISON", ["wt1_detune", "wt1_spread", "wt1_pan", "wt2_detune", "wt2_spread", "wt2_pan"]),
        ("SHAPE", ["wt1_bend", "wt1_formant", "wt2_bend", "wt2_formant"]),
        ("AMP ENV", [{"key": k, "kind": "slider"} for k in ("vca_a", "vca_d", "vca_s", "vca_r")] + ["vca_vel"]),
        ("FILTER ENV", [{"key": k, "kind": "slider"} for k in ("flt_a", "flt_d", "flt_s", "flt_r")]),
        ("MOD ENV 1", ["me1_a", "me1_d", "me1_s", "me1_r", "me1_dst", "me1_amt"]),
        ("MOD ENV 2", ["me2_a", "me2_d", "me2_s", "me2_r", "me2_dst", "me2_amt"]),
        ("VOICE", ["voice_mode", "voices", "glide", "glide_mode", "legato", "pb_range", "volume"])]
p.auto_pages(secs, names=["FILTER", "SHAPE", "ENVELOPES", "MOD ENVS", "VOICE"])
p.write(module_dir="/sdcard/vst/tablor")
