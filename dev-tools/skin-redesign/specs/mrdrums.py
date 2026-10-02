"""mrdrums (16-pad sample player, notes 36-51): black with a hot-magenta accent. On the Move a file browser sets each
pad's sample; on the MPC the port adds kit folders (a patch in src/dsp/mrdrums_plugin.cpp, see VENDORED.md):
/sdcard/vst/mrdrums/kits/<kit>/*.wav go to pads 1-16 in name order. Ships a synthesised "01_Starter" kit
(build.sh mrdrums src/kits:kits). The pad_* parameters edit the selected pad ("EDIT PAD", or the last pad played
while AUTO SELECT is on). The engine reports options as TEXT (case-insensitive)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("mrdrums", "Mr Drums", ["16-PAD", "SAMPLE PLAYER"])
# a text path the MPC can't set; a host restoring it as a number would clear the pad's sample: slot kept, key retired
p.rename("pad_sample_path", "unused_sample_path", name="UNUSED A", min=0, max=1, default=0, hidden=True)
for key, name, kw in [("pad_vol", "PAD VOLUME", dict(min=0, max=2, default=1)),
                      ("pad_pan", "PAD PAN", dict(min=-1, max=1, default=0)),
                      ("pad_tune", "PAD TUNE", dict(min=-24, max=24, default=0, display="int")),
                      ("pad_start", "PAD START", dict(min=0, max=1, default=0)),
                      ("pad_attack_ms", "PAD ATTACK", dict(min=0, max=5000, default=0, display="int")),
                      ("pad_decay_ms", "PAD DECAY", dict(min=0, max=5000, default=250, display="int")),
                      ("pad_choke_group", "CHOKE GROUP", dict(min=0, max=16, default=0, display="int")),
                      ("pad_mode", "PAD MODE", dict(options=["GATE", "ONESHOT"], default=1)),
                      ("pad_rand_pan_amt", "RAND PAN", dict(min=0, max=1, default=0)),
                      ("pad_rand_vol_amt", "RAND VOLUME", dict(min=0, max=1, default=0)),
                      ("pad_rand_decay_amt", "RAND DECAY", dict(min=0, max=1, default=0)),
                      ("pad_chance_pct", "CHANCE", dict(min=0, max=100, default=100, display="int"))]:
    p.set(key, name=name, type=None, unit=None, **kw)
p.preset_browser(key="kit", name_key="kit_name", count=64, name="KIT")
p.add("ui_current_pad", "EDIT PAD", min=1, max=16, default=1, display="int", type="stepper")
p.add("ui_current_pad_prev", "PREV PAD", momentary=True, step_of="ui_current_pad", step_delta=-1)
p.add("ui_current_pad_next", "NEXT PAD", momentary=True, step_of="ui_current_pad", step_delta=1)
p.add("ui_auto_select_pad", "AUTO SELECT", options=["OFF", "ON"], default=1)
p.add("g_master_vol", "MASTER VOL", min=0, max=2, default=1)
p.add("g_polyphony", "POLYPHONY", min=1, max=64, default=16, display="int")
p.add("g_vel_curve", "VEL CURVE", options=["LINEAR", "SOFT", "HARD"], default=0)
p.add("g_humanize_ms", "HUMANIZE", min=0, max=50, default=0)
p.add("g_rand_loop_steps", "RAND LOOP", min=1, max=128, default=16, display="int")
p.look({"bg": "0d0a0d", "panel": "191219", "line": "45304a", "ink": "f8f0f6", "ink_dim": "d8c3d3", "ink_faint": "8a6f84",
        "accent": "d23c93", "accent_hi": "ff5cb8", "knob_face": "f6eef4", "knob_ring": "33233a", "knob_dot": "ff5cb8",
        "lcd": "090609", "seg_active": "ff5cb8", "seg_inactive": "271c27", "seg_active_tx": "0d0a0d", "box": "191219",
        "display_bg": "170512", "display_cell": "270a1f", "display_off": "3d1030", "display_ink": "ff5cb8",
        "display_bezel": "060406"},
       knob="moog",
       led=dict(on="#ff5cb8", off="#3d1030", cap=("#5e5260", "#433a45", "#2c262e"), rim="#7e7080", well="#070507"))
PAD = {"key": "ui_current_pad", "full": False, "span": 2, "label": "EDIT PAD", "repeat": True}
p.page("KIT",
       [("KIT", 4, [{"key": "kit", "get": "kit_name", "caption": "KITS: /sdcard/vst/mrdrums/kits  ·  NOTES 36-51"}]),
        ("MASTER", 4, ["g_master_vol", "g_polyphony", "g_vel_curve", "g_humanize_ms"])],
       [("PAD SELECT", 4, [dict(PAD, span=3), "ui_auto_select_pad"]), ("@logo", 4)])
p.page("PAD",
       [("PAD", 2, [dict(PAD, w=270)]),
        ("SOUND", 6, ["pad_vol", "pad_pan", "pad_tune", "pad_start", "pad_mode", "pad_choke_group"])],
       [("ENVELOPE", 2, ["pad_attack_ms", "pad_decay_ms"]),
        ("RANDOM", 6, ["pad_rand_vol_amt", "pad_rand_pan_amt", "pad_rand_decay_amt", "pad_chance_pct",
                       "g_rand_loop_steps"])])
p.write(module_dir="/sdcard/vst/mrdrums")
