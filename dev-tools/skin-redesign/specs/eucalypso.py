"""Eucalypso (four-lane Euclidean MIDI sequencer, Schwung MIDI FX): plays the notes you hold on its own track out of
its own MIDI port, for other tracks to use as their input. Starts in sync=clock so it follows MPC's transport
(MIDIFX_INIT); the module only parses its own option words, so they ride along as "send" strings."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port
from _seq import info, tidy, GREEN_LED

p = tidy(Port("eucalypso", "Eucalypso", ["EUCLIDEAN", "MIDI SEQUENCER"]), text_options=True)
p.set("sync", default=1)
p.set("root_note", options=["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"], display=None)
p.names({"play_mode": "PLAY MODE", "rate": "RATE", "retrigger_mode": "RETRIGGER", "sync": "SYNC", "bpm": "BPM",
         "swing": "SWING", "max_voices": "VOICES", "global_velocity": "VELOCITY", "global_v_rnd": "VEL RANDOM",
         "global_gate": "GATE", "global_g_rnd": "GATE RANDOM", "global_rnd_seed": "RANDOM SEED",
         "rand_cycle": "RAND CYCLE", "register_mode": "REGISTER", "held_order": "NOTE ORDER",
         "held_order_seed": "ORDER SEED", "missing_note_policy": "MISSING NOTE", "missing_note_seed": "MISSING SEED",
         "scale_mode": "SCALE", "scale_rng": "SCALE RANGE", "root_note": "ROOT", "octave": "OCTAVE"})
LANE = {"enabled": "ON", "steps": "STEPS", "pulses": "PULSES", "rotation": "ROTATE", "drop": "DROP",
        "drop_seed": "DROP SEED", "note": "NOTE", "n_rnd": "NOTE RND", "n_seed": "NOTE SEED", "octave": "OCTAVE",
        "oct_rnd": "OCT RND", "oct_seed": "OCT SEED", "oct_rng": "OCT RANGE", "velocity": "VELOCITY", "gate": "GATE"}
for n in "1234":
    p.names({"lane%s_%s" % (n, k): "L%s %s" % (n, v) for k, v in LANE.items()})
p.look({"bg": "0b0f0d", "panel": "141a17", "line": "34433b", "ink": "eef7f1", "ink_dim": "c3d3c9", "ink_faint": "71857a",
        "accent": "3fcf6e", "accent_hi": "7cf29a", "knob_face": "eef7f1", "knob_ring": "26302b", "knob_dot": "7cf29a",
        "lcd": "070a08", "seg_active": "7cf29a", "seg_inactive": "1f2823", "seg_active_tx": "0b0f0d", "box": "141a17"},
       knob=None, led=GREEN_LED)
p.page("MAIN", [("CLOCK", 4, ["sync", "rate", "bpm", "swing"]), info("Eucalypso")],
       [("PLAY", 4, ["play_mode", "retrigger_mode", "max_voices", "rand_cycle"]),
        ("FEEL", 4, ["global_velocity", "global_v_rnd", "global_gate", "global_g_rnd"])])
for n in "1234":
    L = lambda k: "lane%s_%s" % (n, k)
    p.page("LANE " + n,
           [("LANE %s RHYTHM" % n, 8, [L(k) for k in ("enabled", "steps", "pulses", "rotation", "drop", "drop_seed",
                                                      "velocity", "gate")])],
           [("LANE %s NOTES" % n, 8, [L(k) for k in ("note", "n_rnd", "n_seed", "octave", "oct_rnd", "oct_seed",
                                                     "oct_rng")])])
p.page("NOTES",
       [("NOTE REGISTER", 8, ["register_mode", "held_order", "missing_note_policy", {"key": "scale_mode", "span": 2},
                              "root_note", "scale_rng", "octave"])],
       [("SEEDS", 4, ["held_order_seed", "missing_note_seed", "global_rnd_seed"]), ("@logo", 4)])
p.write(defines={"MIDIFX_INIT": '"sync=clock"'})
