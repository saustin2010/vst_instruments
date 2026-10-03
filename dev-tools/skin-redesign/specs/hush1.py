"""Hush One (SH-101): graphite panel, SH-101 blue accents, faders for the source mixer and envelopes. 11 built-in
presets. Its engine reports option values as TEXT, matched case-insensitively, so labels may change case only."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("hush1", "Hush One", ["MONOPHONIC", "SYNTHESIZER"])
for k, v in p.byk.items():
    if v.get("options"):
        p.set(k, options=[str(o).upper() for o in v["options"]])
p.names({"volume": "VOLUME", "bend_range": "BEND RANGE", "saw": "SAW", "pulse": "PULSE", "sub": "SUB OSC",
         "noise": "NOISE", "sub_mode": "SUB MODE", "white_noise": "WHITE NOISE", "pulse_width": "PULSE WIDTH",
         "pwm_mode": "PWM SOURCE", "pwm_depth": "PWM LFO", "pwm_env_depth": "PWM ENV", "cutoff": "CUTOFF",
         "resonance": "RESONANCE", "env_amt": "ENV AMOUNT", "key_follow": "KEY TRACK", "filter_velocity_sens": "VEL FILTER",
         "attack": "AMP ATTACK", "decay": "AMP DECAY", "sustain": "AMP SUSTAIN", "release": "AMP RELEASE",
         "velocity_sens": "VEL AMP", "f_attack": "FLT ATTACK", "f_decay": "FLT DECAY", "f_sustain": "FLT SUSTAIN",
         "f_release": "FLT RELEASE", "lfo_rate": "LFO RATE", "lfo_waveform": "LFO WAVE", "lfo_trigger": "LFO RETRIG",
         "lfo_sync": "LFO SYNC", "lfo_invert": "LFO INVERT", "lfo_pitch_snap": "PITCH SNAP", "lfo_pitch": "LFO PITCH",
         "lfo_filter": "LFO FILTER", "lfo_pwm": "LFO PWM", "glide": "GLIDE", "portamento_mode": "PORTA MODE",
         "portamento_linear": "PORTA CURVE", "retrigger": "RETRIGGER", "hold": "HOLD", "transpose": "TRANSPOSE",
         "octave_transpose": "OCTAVE", "fine_tune": "FINE TUNE", "gate_trig_mode": "GATE MODE", "vca_mode": "VCA MODE",
         "adsr_declick": "DECLICK", "priority": "PRIORITY", "velocity_mode": "VEL MODE", "same_note_quirk": "SAME NOTE",
         "filter_env_full_range": "ENV FULL", "filter_env_polarity": "ENV POLARITY",
         "filter_volume_correction": "VOL CORRECT"})
p.set("fine_tune", display="int").set("glide", display="int")
p.preset_browser(count=523)   # 11 built-in + up to 512 TAL-BassLine-101 files in presets/ (2026-10-03)
p.look({"bg": "16181c", "panel": "23262c", "line": "474c55", "ink": "f1f3f6", "ink_dim": "c7ccd4", "ink_faint": "7d848f",
        "accent": "3f7fd6", "accent_hi": "5d9cf2", "knob_face": "eef1f5", "knob_ring": "343841", "knob_dot": "5d9cf2",
        "lcd": "0d0e10", "seg_active": "f1f3f6", "seg_inactive": "30343b", "seg_active_tx": "16181c", "box": "23262c",
        "display_bg": "180606", "display_cell": "280a09", "display_off": "3c100e", "display_ink": "ff4d3d",
        "display_bezel": "08090a"},
       knob="cap",
       led=dict(on="#ff4d3d", off="#3c100e", cap=("#6a707a", "#4b5058", "#33373e"), rim="#8a909a", well="#0b0c0e"),
       css=":root { --seg-size: 14px; }\n.slider-thumb { fill: #eef1f5; stroke: #343841; }\n")
S = lambda k: {"key": k, "kind": "slider"}
p.page("MAIN",
       [("PROGRAM", 4, [{"key": "preset", "get": "preset_name", "caption": "ARROWS OR Q-LINK TO BROWSE"}]),
        ("VCF", 4, [{"key": "cutoff", "big": True}, "resonance", "env_amt", "key_follow"])],
       [("ENV", 4, [S("attack"), S("decay"), S("sustain"), S("release")]),
        ("OUTPUT", 4, ["volume", "vca_mode", "velocity_sens", "octave_transpose"])])
p.page("SOURCE",
       [("SOURCE MIXER", 6, [S("saw"), S("pulse"), S("sub"), S("noise"), "sub_mode", "white_noise"]),
        ("PITCH", 2, ["transpose", "fine_tune"])],
       [("PULSE WIDTH", 4, ["pulse_width", "pwm_mode", "pwm_depth", "pwm_env_depth"]),
        ("FILTER ENV", 4, [S("f_attack"), S("f_decay"), S("f_sustain"), S("f_release")])])
p.page("MODULATOR",
       [("LFO", 8, ["lfo_rate", "lfo_waveform", "lfo_trigger", "lfo_sync", "lfo_invert", "lfo_pitch_snap"])],
       [("LFO AMOUNT", 3, ["lfo_pitch", "lfo_filter", "lfo_pwm"]),
        ("FILTER EXTRAS", 5, ["filter_velocity_sens", "filter_env_polarity", "filter_env_full_range",
                              "filter_volume_correction", "adsr_declick"])])
p.page("PERFORM",
       [("PORTAMENTO", 4, ["glide", "portamento_mode", "portamento_linear", "bend_range"]),
        ("KEYBOARD", 4, ["retrigger", "priority", "hold", "same_note_quirk"])],
       [("TRIGGER", 4, ["gate_trig_mode", "velocity_mode"]), ("@logo", 4)])
p.write(module_dir="/sdcard/vst/hush1")
