"""Noisemaker (TAL-NoiseMaker port): 256 factory presets (compiled in). Graphite with a teal accent and drawn
value-arc knobs. The engine's two Move-UI macros ("wave", "tune2") write through to other parameters (osc waves,
levels, PW, FM; osc 2 pitch), so a host restoring every parameter shifted osc 2's tuning: their VST slots are kept
(saved automation indices stay put) under keys the engine ignores."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from skin_gen import Port

p = Port("noisemaker", "Noisemaker", ["VIRTUAL ANALOG", "SYNTHESIZER"])
for i, k in enumerate(("wave", "tune2")):
    p.rename(k, "unused_macro_%d" % (i + 1), name="UNUSED %s" % "AB"[i], min=0, max=1, default=0, display=None,
             hidden=True)
for k in ("osc_sync", "lfo1_sync", "lfo1_keytrig", "lfo2_sync", "lfo2_keytrig", "chorus1", "chorus2", "delay_sync",
          "delay_fac_l", "delay_fac_r"):
    p.set(k, options=["OFF", "ON"], default=0)
for k in ("lfo1_wave", "lfo2_wave"):
    p.set(k, options=["SIN", "TRI", "SAW", "SQR", "S+H", "RND"], default=0)
for k, v in list(p.byk.items()):
    if v.get("options") and k not in ("filter_type",):
        p.set(k, options=[str(o).upper() for o in v["options"]])
p.names({"fenv_time": "FLT TIME", "aenv_time": "AMP TIME", "volume": "VOLUME", "highpass": "HIGH PASS",
         "osc1_wave": "OSC1 WAVE", "osc2_wave": "OSC2 WAVE", "osc1_vol": "OSC1 LEVEL", "osc2_vol": "OSC2 LEVEL",
         "osc3_vol": "SUB LEVEL", "osc_tune": "MASTER TUNE", "osc1_tune": "OSC1 TUNE", "osc2_tune": "OSC2 TUNE",
         "osc1_fine": "OSC1 FINE", "osc2_fine": "OSC2 FINE", "osc1_pw": "OSC1 PW", "osc1_phase": "OSC1 PHASE",
         "osc2_phase": "OSC2 PHASE", "osc2_fm": "OSC2 FM", "osc_sync": "OSC SYNC", "ringmod": "RING MOD",
         "detune": "DETUNE", "bitcrush": "BITCRUSH", "vintage": "VINTAGE", "filter_type": "FILTER TYPE",
         "cutoff": "CUTOFF", "resonance": "RESONANCE", "keyfollow": "KEY TRACK", "filter_env": "FILTER ENV",
         "filter_drive": "DRIVE", "fenv_a": "FLT ATTACK", "fenv_d": "FLT DECAY", "fenv_s": "FLT SUSTAIN",
         "fenv_r": "FLT RELEASE", "aenv_a": "AMP ATTACK", "aenv_d": "AMP DECAY", "aenv_s": "AMP SUSTAIN",
         "aenv_r": "AMP RELEASE", "lfo1_wave": "LFO1 WAVE", "lfo1_rate": "LFO1 RATE", "lfo1_amount": "LFO1 AMOUNT",
         "lfo1_dest": "LFO1 DEST", "lfo1_sync": "LFO1 SYNC", "lfo1_keytrig": "LFO1 KEYTRIG", "lfo1_phase": "LFO1 PHASE",
         "lfo2_wave": "LFO2 WAVE", "lfo2_rate": "LFO2 RATE", "lfo2_amount": "LFO2 AMOUNT", "lfo2_dest": "LFO2 DEST",
         "lfo2_sync": "LFO2 SYNC", "lfo2_keytrig": "LFO2 KEYTRIG", "lfo2_phase": "LFO2 PHASE", "free_a": "ENV3 ATTACK",
         "free_d": "ENV3 DECAY", "free_amt": "ENV3 AMOUNT", "free_dest": "ENV3 DEST", "vel_vol": "VEL VOLUME",
         "vel_env": "VEL ENV", "vel_cut": "VEL CUTOFF", "pw_cutoff": "WHEEL CUTOFF", "pw_pitch": "BEND RANGE",
         "portamento": "PORTAMENTO", "porta_mode": "PORTA MODE", "voices": "VOICES", "chorus1": "CHORUS I",
         "chorus2": "CHORUS II", "reverb_wet": "REVERB WET", "reverb_decay": "REV DECAY", "reverb_pre": "REV PREDELAY",
         "reverb_hi": "REV HI CUT", "reverb_lo": "REV LO CUT", "delay_wet": "DELAY WET", "delay_time": "DELAY TIME",
         "delay_sync": "DELAY SYNC", "delay_fac_l": "DELAY 2X L", "delay_fac_r": "DELAY 2X R",
         "delay_fb": "DLY FEEDBACK", "delay_hi": "DLY HI CUT", "delay_lo": "DLY LO CUT", "env_amt": "DRAW AMOUNT",
         "env_speed": "DRAW SPEED", "env_dest": "DRAW DEST"})
p.preset_browser(count=256)
p.preset_browser(key="bank_index", name_key="bank_name", count=64, name="BANK")   # preset folders (2026-10-03)
p.look({"bg": "15181c", "panel": "1f2429", "line": "434c56", "ink": "f0f3f5", "ink_dim": "c3cbd2", "ink_faint": "76818c",
        "accent": "26a9b8", "accent_hi": "43d3e3", "knob_face": "e6ebef", "knob_ring": "2e353d", "knob_dot": "43d3e3",
        "lcd": "0b0d0f", "seg_active": "43d3e3", "seg_inactive": "2a3037", "seg_active_tx": "0e1114", "box": "1f2429",
        "display_bg": "04100f", "display_cell": "081c1c", "display_off": "0e2d2e", "display_ink": "4fe3f2",
        "display_bezel": "05070a"},
       knob=None,
       led=dict(on="#43d3e3", off="#0e2d2e", cap=("#5d6670", "#414850", "#2b3036"), rim="#7c8792", well="#07090b"))
S = lambda k: {"key": k, "kind": "slider"}
FT = ["LP24", "LP18", "LP12", "LP6", "HP24", "BP24", "NOTCH", "SV-LP", "SV-HP", "SV-BP", "SV-NOTCH", "SV-PEAK"]
p.page("MAIN",
       [("PROGRAM", 4, [{"key": "preset", "get": "preset_name", "caption": "256 PATCHES  ·  ARROWS OR Q-LINK TO BROWSE"}]),
        ("FILTER", 4, [{"key": "filter_type"}, {"key": "cutoff", "big": True}, "resonance", "filter_env"])],
       [("AMP ENVELOPE", 4, [S("aenv_a"), S("aenv_d"), S("aenv_s"), S("aenv_r")]),
        ("VOICE", 4, ["volume", "voices", "portamento", "porta_mode"])])
p.page("OSC",
       [("OSC 1", 4, ["osc1_wave", "osc1_tune", "osc1_fine", "osc1_pw"]),
        ("OSC 2", 4, ["osc2_wave", "osc2_tune", "osc2_fine", "osc2_fm"])],
       [("MIXER", 3, ["osc1_vol", "osc2_vol", "osc3_vol"]),
        ("OSC MOD", 5, ["osc_sync", "ringmod", "osc_tune", "osc1_phase", "osc2_phase"])])
p.page("FILTER",
       [("FILTER", 4, ["keyfollow", "filter_drive", "highpass", "vel_cut"]),
        ("CHARACTER", 4, ["detune", "vintage", "bitcrush"])],
       [("FILTER ENVELOPE", 4, [S("fenv_a"), S("fenv_d"), S("fenv_s"), S("fenv_r")]),
        ("ENV TIME", 2, ["fenv_time", "aenv_time"]), ("VELOCITY", 2, ["vel_vol", "vel_env"])])
p.page("LFO",
       [("LFO 1", 8, ["lfo1_wave", "lfo1_rate", "lfo1_amount", "lfo1_dest", "lfo1_sync", "lfo1_keytrig", "lfo1_phase"])],
       [("LFO 2", 8, ["lfo2_wave", "lfo2_rate", "lfo2_amount", "lfo2_dest", "lfo2_sync", "lfo2_keytrig", "lfo2_phase"])])
p.page("MOD",
       [("ENV 3", 4, ["free_a", "free_d", "free_amt", "free_dest"]),
        ("ENV DRAW", 4, ["env_amt", "env_speed", "env_dest"])],
       [("WHEEL / BEND", 2, ["pw_cutoff", "pw_pitch"]), ("CHORUS", 2, ["chorus1", "chorus2"]), ("@logo", 4)])
p.page("FX",
       [("REVERB", 8, ["reverb_wet", "reverb_decay", "reverb_pre", "reverb_hi", "reverb_lo"])],
       [("DELAY", 8, ["delay_wet", "delay_time", "delay_sync", "delay_fac_l", "delay_fac_r", "delay_fb", "delay_hi",
                      "delay_lo"])])
p.write(module_dir="/sdcard/vst/noisemaker")
