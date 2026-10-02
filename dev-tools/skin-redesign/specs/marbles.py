"""Marbles (Mutable Instruments random sampler, from Mutable's own source as a MIDI FX module): three voices out of its
own MIDI port, T1/T2/T3 rhythms playing X1/X2/X3 pitches (channels 1/2/3, or all on 1), clocked by MPC's tempo.
Graphite with a violet accent. Page 1 is the module's panel; SETUP holds what the module keeps in its menus."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port
from _seq import info

p = Port("marbles", "Marbles", ["RANDOM", "SAMPLER"])
p.names({"t_model": "T MODEL", "clock_div": "CLOCK DIV", "t_bias": "T BIAS", "jitter": "JITTER", "gate_len": "GATE LEN",
         "deja_vu": "DEJA VU", "length": "LENGTH", "t_deja_vu": "T DEJA VU", "spread": "SPREAD", "x_bias": "X BIAS",
         "steps": "STEPS", "scale": "SCALE", "x_deja_vu": "X DEJA VU", "rate_base": "RATE BASE", "t_range": "T RANGE",
         "gate_rand": "GATE RAND", "x_range": "X RANGE", "x_mode": "X MODE", "base_note": "BASE NOTE",
         "velocity": "VELOCITY", "channels": "CHANNELS", "t1_out": "T1 > X1", "t2_out": "T2 > X2", "t3_out": "T3 > X3"})
p.look({"bg": "0e0c12", "panel": "18151f", "line": "3d3550", "ink": "f4f0fb", "ink_dim": "d2c9e3", "ink_faint": "7d7092",
        "accent": "8f6ae0", "accent_hi": "b394ff", "knob_face": "f1eef6", "knob_ring": "2a2436", "knob_dot": "b394ff",
        "lcd": "08060b", "seg_active": "b394ff", "seg_inactive": "241f30", "seg_active_tx": "0e0c12", "box": "18151f"},
       knob="metal",
       led=dict(on="#b394ff", off="#2a1f45", cap=("#5a5566", "#3f3b49", "#292630"), rim="#7d7092", well="#060508"))
p.page("MARBLES",
       [("T  RHYTHM", 5, ["t_model", "clock_div", {"key": "t_bias", "big": True}, "jitter", "gate_len"]),
        ("DEJA VU", 3, [{"key": "deja_vu", "big": True}, "length", "t_deja_vu"])],
       [("X  PITCH", 5, [{"key": "spread", "big": True}, "x_bias", "steps", "scale", "x_deja_vu"]),
        info("Marbles", 3, last="PRESS PLAY: IT FOLLOWS MPC'S CLOCK")])
p.page("SETUP",
       [("CLOCK", 3, ["rate_base", "t_range", "gate_rand"]), ("X", 2, ["x_range", "x_mode"]),
        ("MIDI", 3, ["base_note", "velocity", "channels"])],
       [("OUTPUTS", 3, ["t1_out", "t2_out", "t3_out"]), ("@logo", 5)])
p.write()
