"""Elements (Mutable Instruments modal synthesis voice, from Mutable's own source; engine wrapper in
mpc/elements_engine.cc): graphite with a teal accent. One voice as on the module: notes drive gate, pitch and
strength (velocity). Page 1 is the module's panel (exciter top, resonator bottom); MODEL's fourth entry is its hidden
"Ominous" voice. SIGNATURE reseeds the per-unit variations the module takes from its serial number."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("elements", "Elements", ["MODAL", "SYNTHESIS"])
p.names({"contour": "CONTOUR", "bow": "BOW", "bow_timbre": "BOW TIMBRE", "blow": "BLOW", "flow": "FLOW",
         "blow_timbre": "BLOW TIMBRE", "strike": "STRIKE", "mallet": "MALLET", "strike_timbre": "STRK TIMBRE",
         "model": "MODEL", "geometry": "GEOMETRY", "brightness": "BRIGHTNESS", "damping": "DAMPING",
         "position": "POSITION", "space": "SPACE", "octave": "OCTAVE", "fine": "FINE", "signature": "SIGNATURE",
         "legato": "LEGATO", "bend_range": "BEND RANGE", "velocity": "VELOCITY", "volume": "VOLUME"})
p.look({"bg": "0b0f0f", "panel": "131a1a", "line": "2f4544", "ink": "eef8f7", "ink_dim": "c4d8d6", "ink_faint": "6f8886",
        "accent": "2fb8ad", "accent_hi": "5ee0d4", "knob_face": "eef5f4", "knob_ring": "243231", "knob_dot": "5ee0d4",
        "lcd": "070a0a", "seg_active": "5ee0d4", "seg_inactive": "1d2928", "seg_active_tx": "0b0f0f", "box": "131a1a"},
       knob="metal",
       led=dict(on="#5ee0d4", off="#0f3330", cap=("#55605f", "#3b4443", "#262d2c"), rim="#6f8886", well="#050707"))
p.page("ELEMENTS",
       [("BOW", 2, ["bow", "bow_timbre"]), ("BLOW", 3, ["blow", "flow", "blow_timbre"]),
        ("STRIKE", 3, ["strike", "mallet", "strike_timbre"])],
       [("ENV", 1, ["contour"]),
        ("RESONATOR", 6, ["model", "geometry", "brightness", "damping", "position"]), ("SPACE", 1, ["space"])])
p.page("PLAY",
       [("PITCH", 3, ["octave", "fine", "bend_range"]), ("PLAYING", 3, ["legato", "velocity", "signature"]),
        ("OUTPUT", 2, ["volume"])],
       [("@logo", 8)])
p.write()
