"""Braids (macro oscillator, 47 algorithms): cool steel blue, a blue LED patch display, and under it the
selected algorithm's real waveform (one picture per algorithm, rendered from the engine; 2026-10-02). Presets are files, shipped
to /sdcard/vst/braids/presets (build.sh braids src/presets:presets)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("braids", "Braids", ["MACRO", "OSCILLATOR"])
p.names({"engine": "ALGORITHM", "timbre": "TIMBRE", "color": "COLOR", "attack": "AMP ATTACK", "decay": "AMP DECAY",
         "sustain": "AMP SUSTAIN", "release": "AMP RELEASE", "cutoff": "CUTOFF", "resonance": "RESONANCE",
         "filt_env": "FILTER ENV", "fm": "FM", "volume": "VOLUME", "octave_transpose": "OCTAVE",
         "f_attack": "FLT ATTACK", "f_decay": "FLT DECAY", "f_sustain": "FLT SUSTAIN", "f_release": "FLT RELEASE"})
p.preset_browser(count=10)
p.look({"bg": "0b0f15", "panel": "131a23", "line": "2f3b4a", "ink": "eef3f8", "ink_dim": "b9c5d3", "ink_faint": "6b7b8e",
        "accent": "2f8fe0", "accent_hi": "5cb3ff", "knob_face": "e8eef5", "knob_ring": "26313e", "knob_dot": "5cb3ff",
        "lcd": "070b10", "seg_active": "eef3f8", "seg_inactive": "1d2632", "seg_active_tx": "0b0f15", "box": "131a23",
        "display_bg": "06101a", "display_cell": "0b1a2a", "display_off": "13283d", "display_ink": "6cc4ff",
        "display_bezel": "03070b"},
       knob="metal",
       led=dict(on="#5cc0ff", off="#10263a", cap=("#56606c", "#3a424c", "#252b33"), rim="#737e8b", well="#05080c"))
# the algorithm's real waveform (engine output at the default TIMBRE/COLOR): steve/tools/stitch/waveforms.py
WAVES = ["images/waves/engine_%d.svg" % i for i in range(47)]
p.page("BRAIDS",
       [("PROGRAM", 4, [{"key": "preset", "get": "preset_name", "dy": 108},
                        {"key": "engine", "kind": "picture", "w": 580, "h": 108, "dy": 164, "files": WAVES}]),
        ("OSCILLATOR", 4, [{"key": "engine", "span": 2, "w": 200}, "timbre", "color"])],
       [("FILTER", 3, [{"key": "cutoff", "big": True}, "resonance", "filt_env"]), ("MOD", 1, ["fm"]),
        ("OUTPUT", 2, ["octave_transpose", "volume"]), ("@logo", 2)])
p.page("ENVELOPES",
       [("AMP ENVELOPE", 8, [{"key": k, "kind": "slider"} for k in ("attack", "decay", "sustain", "release")])],
       [("FILTER ENVELOPE", 8, [{"key": k, "kind": "slider"} for k in ("f_attack", "f_decay", "f_sustain", "f_release")])])
p.write(module_dir="/sdcard/vst/braids")
