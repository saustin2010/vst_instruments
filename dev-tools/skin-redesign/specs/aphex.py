"""Aphex (Korg MS-10/MS-20 hybrid mono: KORG-35/OTA filters, patchbay, ESP): MS-20 black panel with white legends
and the patch-cable orange. Presets are a named list. Randomise/mutate/reset are buttons. Option text is the
engine's own (it reports options as text; footage labels like 8' are matched as labels)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("aphex", "Aphex", ["MS-STYLE", "MONOSYNTH"])
for k, v in list(p.byk.items()):
    if v.get("type"):
        p.set(k, type=None)
    if v.get("options"):
        p.set(k, options=[str(o).upper() for o in v["options"]])
for k in ("trigger", "rnd_patch", "mutate", "rnd_mod", "reset_patch"):
    p.set(k, momentary=True, display=None)
p.set("octave", display="int")
p.short_names(abbrev={"PORTAMENTO": "PORTA"})
p.names({"lpf_cut": "LPF CUTOFF", "lpf_reso": "LPF PEAK", "hpf_cut": "HPF CUTOFF", "hpf_reso": "HPF PEAK",
         "mg_freq": "MG FREQ", "mg_depth": "MG DEPTH", "master_tune": "TUNE", "volume": "VOLUME", "preset": "PRESET",
         "octave": "OCTAVE", "portamento": "PORTAMENTO", "trigger": "TRIGGER", "rnd_patch": "RANDOM", "mutate": "MUTATE",
         "rnd_mod": "RANDOM MOD", "reset_patch": "RESET", "v1_pitch": "VCO1 SCALE", "v1_wave": "VCO1 WAVE",
         "v1_pw": "VCO1 PW", "v2_pitch": "VCO2 SCALE", "v2_fine": "VCO2 PITCH", "v2_wave": "VCO2 WAVE", "v2_pw": "VCO2 PW",
         "v2_sync": "VCO2 SYNC", "v2_xmod": "VCO2 FM", "v2_detune": "VCO2 DETUNE", "mix_v1": "VCO1 LEVEL",
         "mix_v2": "VCO2 LEVEL", "mix_sub": "SUB LEVEL", "mix_noise": "NOISE LEVEL", "noise_color": "NOISE COLOR",
         "mix_esp": "ESP LEVEL", "mix_fb": "FEEDBACK", "filter_mode": "FILTER MODE", "filter_rev": "FILTER REV",
         "ms10_mode": "MS-10 MODE", "drift": "DRIFT", "drive": "DRIVE", "key_track": "KEY TRACK"})
MAIN = ["preset", "rnd_patch", "mutate", "lpf_cut", "lpf_reso", "hpf_cut", "hpf_reso", "mg_freq",
        "mg_depth", "octave", "portamento", "volume", "master_tune", "trigger", "reset_patch"]
p.look({"bg": "0b0b0b", "panel": "141414", "line": "3c3c3c", "ink": "f5f5f0", "ink_dim": "cfcfc8", "ink_faint": "7c7c74",
        "accent": "e8792a", "accent_hi": "ff9a45", "knob_face": "f5f5f0", "knob_ring": "2a2a2a", "knob_dot": "ff9a45",
        "lcd": "080808", "seg_active": "f5f5f0", "seg_inactive": "242424", "seg_active_tx": "0b0b0b", "box": "141414"},
       knob="moog",
       led=dict(on="#ff9a45", off="#3a2410", cap=("#f2f1ec", "#d2d0ca", "#aaa7a0"), rim="#6d6a63", well="#050505"))
p.page("MAIN",
       [("PATCH", 4, [{"key": "preset", "span": 2, "w": 280}, "rnd_patch", "mutate"]),
        ("FILTERS", 4, [{"key": "lpf_cut", "big": True}, "lpf_reso", "hpf_cut", "hpf_reso"])],
       [("MG", 2, ["mg_freq", "mg_depth"]), ("VOICE", 4, ["octave", "portamento", "volume", "master_tune"]),
        ("ACTIONS", 2, ["trigger", "reset_patch"])])
order = [("VCO", p.keys("v1_", "v2_", "vco_")), ("MIXER", p.keys("mix_", "noise_")),
         ("FILTER", p.keys("hpf_mg", "hpf_eg", "lpf_mg", "lpf_eg", "filter_mode", "filter_rev", "key_track", "drive")),
         ("ENVELOPES", p.keys("e1_", "e2_")), ("MG", p.keys("mg_shape", "mg_pw")), ("ESP", p.keys("esp_")),
         ("PATCHBAY", p.keys("pb_")), ("MODERN", p.keys("drift", "ms10_mode", "x_mod_amt", "mw_", "vel_amt", "rnd_mod"))]
placed = set(MAIN)
secs = []
for title, ks in order:
    ks = [k for k in ks if k not in placed]
    placed |= set(ks)
    secs.append((title, ks))
p.auto_pages(secs)
p.write()
