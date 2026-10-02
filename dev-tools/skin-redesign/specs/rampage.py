"""Rampage (Befaco's dual slope generator, from its VCV Rack source through steve/tools/rack): modulates OTHER tracks
through its own MIDI port (OUT A/B, MIN, MAX as CCs; end-of-cycle as notes), triggered or gated by the notes on its own
track; it can also be heard (AUDIO). Light panel, Befaco red. Page 1 is the module's panel; MIDI holds the routing."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("rampage", "Rampage", ["DUAL SLOPE", "GENERATOR"])
for k in ("trig_a", "trig_b"):
    p.set(k, options=["-", "GO"], momentary=True)
p.names({"range_a": "A RANGE", "shape_a": "A SHAPE", "rise_a": "A RISE", "fall_a": "A FALL", "cycle_a": "A CYCLE",
         "trig_a": "A TRIGGER", "range_b": "B RANGE", "shape_b": "B SHAPE", "rise_b": "B RISE", "fall_b": "B FALL",
         "cycle_b": "B CYCLE", "trig_b": "B TRIGGER", "balance": "BALANCE", "mode_a": "A NOTES", "mode_b": "B NOTES",
         "keytrack_a": "A KEY TRACK", "keytrack_b": "B KEY TRACK", "cc_channel": "CC CHANNEL", "cc_a": "CC OUT A",
         "cc_b": "CC OUT B", "cc_min": "CC MIN", "cc_max": "CC MAX", "eoc_notes": "EOC NOTES", "eoc_note_a": "EOC NOTE A",
         "eoc_note_b": "EOC NOTE B", "audio": "AUDIO", "volume": "VOLUME"})
p.look({"bg": "c9cbcd", "panel": "e4e5e6", "line": "9a9da1", "ink": "151515", "ink_dim": "333538", "ink_faint": "6a6d72",
        "accent": "d0312d", "accent_hi": "e8453f", "knob_face": "f4f4f2", "knob_ring": "8d9095", "knob_dot": "d0312d",
        "lcd": "1b1c1f", "seg_active": "d0312d", "seg_inactive": "d4d6d8", "seg_active_tx": "ffffff", "box": "e4e5e6"},
       knob="moog",
       led=dict(on="#ff4a3d", off="#5a1814", cap=("#f2f2f0", "#d6d7d8", "#b9bbbe"), rim="#8d9095", well="#4a4c50"))
CH = lambda c: [{"key": "range_" + c}, {"key": "rise_" + c, "big": True}, {"key": "fall_" + c, "big": True}, "shape_" + c]
p.page("RAMPAGE",
       [("CHANNEL A", 4, CH("a")), ("CHANNEL B", 4, CH("b"))],
       [("A", 2, ["cycle_a", "trig_a"]), ("LOGIC", 1, ["balance"]), ("B", 2, ["cycle_b", "trig_b"]),
        ("AUDIO", 3, ["audio", "volume"])])
p.page("MIDI",
       [("NOTES IN", 4, ["mode_a", "mode_b", "keytrack_a", "keytrack_b"]),
        ("@info:MIDI OUT|MODULATES OTHER TRACKS|PORT: RAMPAGE MIDI OUT|PREFS > MIDI: TURN ON 'TRACK' FOR IT"
         "|OTHER TRACK: MIDI IN = THAT PORT|THEN MIDI-LEARN ITS CCS (0 = OFF)", 4)],
       [("CC OUT", 5, ["cc_channel", "cc_a", "cc_b", "cc_min", "cc_max"]),
        ("END OF CYCLE", 3, ["eoc_notes", "eoc_note_a", "eoc_note_b"])])
p.write()
