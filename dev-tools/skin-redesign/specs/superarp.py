"""Super Arp (pattern/rhythm arpeggiator, Schwung MIDI FX): arpeggiates the chord you hold on its own track out of its
own MIDI port, for other tracks to use as their input. Starts in sync=clock (MIDIFX_INIT). The 40-entry pattern and
rhythm lists are steppers (no pop-up fits 40 names); option words go to the module as "send" strings."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port
from _seq import info, tidy

p = tidy(Port("superarp", "Super Arp", ["PATTERN", "ARPEGGIATOR"]), text_options=True)
p.set("sync", default=1)
p.names({"rate": "RATE", "bpm": "BPM", "triplet": "TRIPLET", "gate": "GATE", "velocity": "VELOCITY", "sync": "SYNC",
         "swing": "SWING", "latch": "LATCH", "max_voices": "VOICES", "octave_range": "OCTAVES",
         "progression_mode": "MODE", "progression_seed": "MODE SEED", "progression_trigger": "MODE TRIGGER",
         "missing_note_policy": "MISSING NOTE", "pattern_preset": "PATTERN", "random_pattern_length": "RND LENGTH",
         "random_pattern_chords": "RND CHORDS", "random_pattern_chord_seed": "RND CH SEED",
         "rhythm_trigger": "RHY TRIGGER", "rhythm_preset": "RHYTHM", "drop_amount": "DROP", "drop_seed": "DROP SEED",
         "velocity_random_amount": "VEL RANDOM", "velocity_seed": "VEL SEED", "gate_random_amount": "GATE RANDOM",
         "gate_seed": "GATE SEED", "modifier_loop_length": "MOD LOOP", "modifier_trigger": "MOD TRIGGER",
         "random_octave_amount": "OCT RANDOM", "random_octave_range": "OCT RANGE", "random_octave_seed": "OCT SEED",
         "random_note_amount": "NOTE RANDOM", "random_note_seed": "NOTE SEED"})
for k in ("pattern_preset", "rhythm_preset"):
    p.option_stepper(k)
p.look({"bg": "0e0a12", "panel": "18121e", "line": "3f3150", "ink": "f6effc", "ink_dim": "d5c7e3", "ink_faint": "7f6d92",
        "accent": "a764e8", "accent_hi": "c993ff", "knob_face": "f6effc", "knob_ring": "2b2236", "knob_dot": "c993ff",
        "lcd": "08060a", "seg_active": "c993ff", "seg_inactive": "261d30", "seg_active_tx": "0e0a12", "box": "18121e",
        "display_bg": "0d0716", "display_cell": "1a0f2a", "display_off": "2a1a40", "display_ink": "c993ff",
        "display_bezel": "060408"},
       knob=None,
       led=dict(on="#c993ff", off="#2a1a40", cap=("#54486a", "#3b3150", "#262036"), rim="#7f6d92", well="#060408"))
STEP = lambda k: {"key": k, "span": 3, "w": 430, "h": 80}
p.page("MAIN", [("CLOCK", 4, ["sync", "rate", "triplet", "bpm"]), info("Super Arp")],
       [("PLAY", 8, ["latch", "octave_range", "gate", "velocity", "swing", "max_voices"])])
p.page("PATTERN",
       [("PROGRESSION", 8, ["progression_mode", STEP("pattern_preset"), "progression_trigger", "missing_note_policy",
                            "progression_seed"])],
       [("RHYTHM", 4, [STEP("rhythm_preset"), "rhythm_trigger"]),
        ("RANDOM PATTERN", 4, ["random_pattern_length", "random_pattern_chords", "random_pattern_chord_seed"])])
p.page("MODIFY",
       [("MODIFIERS", 8, ["modifier_loop_length", "modifier_trigger", "drop_amount", "drop_seed",
                          "velocity_random_amount", "velocity_seed", "gate_random_amount", "gate_seed"])],
       [("RANDOM OCTAVE", 3, ["random_octave_amount", "random_octave_range", "random_octave_seed"]),
        ("RANDOM NOTE", 2, ["random_note_amount", "random_note_seed"]), ("@logo", 3)])
p.write(defines={"MIDIFX_INIT": '"sync=clock"'})
