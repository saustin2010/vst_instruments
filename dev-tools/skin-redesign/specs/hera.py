"""Hera (Juno-60): all 26 of the engine's parameters (the first build exposed 10), faders for levels and the envelope,
a red LED patch display. 56 preset files, shipped to /sdcard/vst/hera/presets (build.sh hera src/presets:presets)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("hera", "Hera", ["POLYPHONIC", "DCO SYNTHESIZER"])
p.names({"volume": "VOLUME", "vcf_cutoff": "VCF FREQ", "vcf_resonance": "VCF RES", "vcf_env": "VCF ENV",
         "attack": "ATTACK", "decay": "DECAY", "sustain": "SUSTAIN", "release": "RELEASE", "lfo_rate": "LFO RATE"})
# the engine's other 18 parameters, appended so the first build's indices stay put (plain values, as its table says)
for key, name, kw in [
        ("saw_level", "SAW", {}), ("pulse_level", "PULSE", {}), ("sub_level", "SUB OSC", {}),
        ("noise_level", "NOISE", {}), ("pwm_depth", "PWM", {}),
        ("pwm_mod", "PWM MODE", {"options": ["MAN", "LFO", "ENV"]}),
        ("pitch_range", "RANGE", {"options": ["16'", "8'", "4'"], "default": 1}),
        ("pitch_mod", "DCO LFO", {}), ("vcf_lfo", "VCF LFO", {}), ("vcf_key", "VCF KYBD", {}),
        ("vcf_bend", "VCF BEND", {}), ("vca_depth", "VCA LEVEL", {"default": 0.5}),
        ("vca_type", "VCA MODE", {"options": ["ENV", "GATE"]}), ("lfo_delay", "LFO DELAY", {}),
        ("lfo_trigger", "LFO TRIG", {"options": ["MANUAL", "AUTO"], "default": 1}), ("hpf", "HPF", {}),
        ("chorus_i", "CHORUS I", {"options": ["OFF", "ON"]}), ("chorus_ii", "CHORUS II", {"options": ["OFF", "ON"]})]:
    if "options" not in kw:
        kw.update(min=0, max=1)
    kw.setdefault("default", 0)
    p.add(key, name, **kw)
p.add("octave_transpose", "OCTAVE", min=-3, max=3, default=0, display="int")
p.preset_browser(count=56)
p.look({"bg": "0d0d0e", "panel": "19191b", "line": "3a3a3e", "ink": "f2f2ef", "ink_dim": "cacac6", "ink_faint": "7e7e84",
        "accent": "e2542b", "accent_hi": "ff6b3d", "knob_face": "f2f2ef", "knob_ring": "2f2f33", "knob_dot": "ff6b3d",
        "lcd": "090909", "seg_active": "f2f2ef", "seg_inactive": "242427", "seg_active_tx": "0d0d0e", "box": "19191b",
        "display_bg": "180504", "display_cell": "290a07", "display_off": "3f120c", "display_ink": "ff4a30",
        "display_bezel": "050505"},
       knob="cap",
       led=dict(on="#ff4a30", off="#3f120c", cap=("#eeeeea", "#cfcfca", "#a9a9a4"), rim="#707074", well="#050505"),
       css=".slider-thumb { fill: #f2f2ef; stroke: #2f2f33; }\n.slider-fill { fill: var(--accent-hi); }\n")
S = lambda k: {"key": k, "kind": "slider"}
p.page("MAIN",
       [("PROGRAM", 4, [{"key": "preset", "get": "preset_name", "caption": "56 PATCHES  ·  ARROWS OR Q-LINK TO BROWSE"}]),
        ("CHORUS", 2, ["chorus_i", "chorus_ii"]), ("OUTPUT", 2, ["volume", "octave_transpose"])],
       [("VCF", 4, [{"key": "vcf_cutoff", "big": True}, "vcf_resonance", "vcf_env", "vcf_lfo"]),
        ("ENV", 4, [S("attack"), S("decay"), S("sustain"), S("release")])])
p.page("DCO / LFO",
       [("DCO", 8, ["pitch_range", "pitch_mod", "pwm_depth", "pwm_mod", S("pulse_level"), S("saw_level"),
                    S("sub_level"), S("noise_level")])],
       [("LFO", 3, ["lfo_rate", "lfo_delay", "lfo_trigger"]), ("HPF", 1, ["hpf"]),
        ("VCF MOD", 2, ["vcf_key", "vcf_bend"]), ("VCA", 2, ["vca_type", "vca_depth"])])
p.write(module_dir="/sdcard/vst/hera")
