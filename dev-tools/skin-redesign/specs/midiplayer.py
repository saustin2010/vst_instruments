"""MIDI Player (plays Standard MIDI Files in time with the transport, Schwung MIDI FX): plays out of its own MIDI port
for other tracks. Files live in /sdcard/vst/midiplayer/MIDI (a generated demo ships there); the file arrows step
file_index and show the file's name. Track picks one track of the file (ALL = every track, sent on channel 1)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port
from _seq import info, tidy

p = tidy(Port("midiplayer", "MIDI Player", ["SMF", "FILE PLAYER"]))
p.preset_browser(key="file_index", name_key="file_name_display", count=128, name="FILE")
p.add("track", "TRACK", options=["ALL"] + [str(i) for i in range(1, 17)], values=list(range(-1, 16)), default=0)
p.names({"loop": "LOOP"})
p.look({"bg": "0a0d12", "panel": "121820", "line": "2f3c4d", "ink": "eef4fb", "ink_dim": "c3cfdd", "ink_faint": "6d7e92",
        "accent": "3a9be8", "accent_hi": "66b8ff", "knob_face": "eef4fb", "knob_ring": "212a35", "knob_dot": "66b8ff",
        "lcd": "06080b", "seg_active": "66b8ff", "seg_inactive": "1c2430", "seg_active_tx": "0a0d12", "box": "121820",
        "display_bg": "02101c", "display_cell": "061c2e", "display_off": "0a2a44", "display_ink": "66b8ff",
        "display_bezel": "030508"},
       knob=None,
       led=dict(on="#66b8ff", off="#0a2a44", cap=("#4a5668", "#343e4c", "#222a35"), rim="#6d7e92", well="#040608"))
p.page("MAIN",
       [("FILE", 4, [{"key": "file_index", "get": "file_name_display",
                      "caption": "/SDCARD/VST/MIDIPLAYER/MIDI  ·  PLAYS WITH MPC'S TRANSPORT"}]),
        info("MIDI Player")],
       [("PLAYBACK", 2, ["track", "loop"]), ("@logo", 6)])
p.write(module_dir="/sdcard/vst/midiplayer")
