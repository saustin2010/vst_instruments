"""A Stitch mockup (steve/stitch_layouts/<port>.md) -> that port's layout.conf, with the mockup's look.
   python3 steve/tools/stitch/convert.py <port> [<port> ...]      then  steve/tools/skin-redesign/build.sh <port> ...
1. extract.py (in the mpc-vst-html-art container) renders each tab with its live parts hidden, records every control
   and renders each knob style as a filmstrip from the mockup's own knob (see extract.py).
2. Here: each control is matched to a parameter (the key the mockup names: data-key / data-param / ids / onclick
   arguments, else its label vs the parameter's name; MAPS overrides), and the layout is written: the background as
   art, knobs with their filmstrips (MPC draws the name and value live under them), our switches, toggles,
   sliders, steppers and pop-ups at the mockup's spots, the mockup's canvases as displays (real waveforms), and the
   mockup's theme colours. The port's previous layout.conf is kept once as layout.grid.conf; images go to
   images/stitch/. A control the mockup has but no parameter matches is reported and skipped."""
import json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEVE = os.path.dirname(os.path.dirname(HERE))
MV = os.path.dirname(STEVE)
# what Docker mounts: the framework checkout, or in a vst_instruments workspace (framework/mpc-vst-plugins inside
# the repo, plugins linked from the repo) the whole repo, so the links resolve in the container too
DOCKER_ROOT = (os.path.dirname(os.path.dirname(MV)) if os.path.basename(os.path.dirname(MV)) == "framework"
               and os.path.exists(os.path.join(os.path.dirname(MV), "setup.sh")) else MV)
SL = os.path.join(STEVE, "stitch_layouts")
Y_OFF = 86

WAVES = lambda port, key, n: ["images/waves/%s_%d.svg" % (key, i) for i in range(n)]

# the "Mono Voice & Helm" design's markup, for the pages it didn't draw (both ports use its style)
MONO_TPL = {
    "frame": '<section class="absolute bg-mpc-panel rounded-lg border border-neutral-800 p-3 flex flex-col" style="left:{x}px; '
             'top:{y}px; width:{w}px; height:{h}px;"><div class="flex items-center justify-between border-b border-mpc-divider '
             'pb-1 mb-2"><span class="text-xs font-bold tracking-widest text-neutral-200">{title}</span></div></section>',
    "knob": '<div class="flex flex-col items-center rotary-group" data-param="{key}"><div class="relative w-16 h-16 rotary-dial">'
            '<svg class="w-full h-full transform -rotate-90" viewBox="0 0 40 40"><circle cx="20" cy="20" fill="#14171d" r="16" '
            'stroke="#252833" stroke-width="3"></circle></svg><div class="absolute inset-1.5 rounded-full bg-gradient-to-b '
            'from-neutral-700 via-neutral-900 to-black flex items-center justify-center border border-neutral-600 shadow-md">'
            '<div class="w-1 h-4 bg-amber-400 rounded-full knob-pointer transform origin-bottom" style="transform: rotate(0deg);">'
            '</div></div></div><span class="text-[9px] font-bold text-neutral-400 mt-1 uppercase text-center tracking-tight">'
            '{label}</span><div class="w-14 bg-black/90 border border-blue-900/60 rounded text-center py-0.5 mt-0.5"><span '
            'class="text-[10px] text-amber-400 font-mono knob-val">0</span></div></div>',
    "enum": '<div class="flex flex-col items-center"><span class="text-[9px] font-bold text-neutral-400 uppercase tracking-wider '
            'mb-1">{label}</span><div class="aug-enum flex flex-col gap-1" data-param="{key}">{opts}</div></div>',
    "opt_enum": '<button class="px-2.5 py-0.5 text-[10px] font-mono rounded bg-neutral-800 text-neutral-300 font-bold">{opt}</button>',
    "popup": '<div class="flex flex-col"><label class="text-[10px] uppercase tracking-wider text-neutral-400 font-semibold mb-1">'
             '{label}</label><div class="aug-popup relative flex items-center justify-between px-3 py-2 bg-neutral-950 border '
             'border-blue-900/60 rounded shadow-inner" style="width:{w}px;" data-param="{key}"><span class="font-bold text-amber-400 '
             'text-sm tracking-wide">&nbsp;</span><div class="w-4 h-4 rounded bg-amber-500/20 text-amber-400 flex items-center '
             'justify-center text-[10px]">&#9660;</div></div></div>',
    "popup_inline": '<div class="aug-popup relative flex items-center justify-between px-3 py-1 bg-neutral-950 border '
                    'border-blue-900/60 rounded shadow-inner" style="width:170px;" data-param="{key}"><span class="font-bold '
                    'text-amber-400 text-sm">&nbsp;</span><div class="w-4 h-4 rounded bg-amber-500/20 text-amber-400 flex '
                    'items-center justify-center text-[10px]">&#9660;</div></div>',
    "stepper": '<div class="flex flex-col" style="width:{w}px;"><span class="text-[9px] font-mono font-bold text-neutral-400 '
               'uppercase tracking-tighter mb-0.5">{label}</span><div class="aug-stepper flex items-center bg-black rounded border '
               'border-neutral-700/80 p-0.5" data-param="{key}"><button class="px-1 text-amber-500 text-xs">&#9664;</button><div '
               'class="flex-1 lcd-screen-pattern py-1 px-1 rounded text-center border border-emerald-950 font-mono text-[9px] '
               'text-mpc-lcdText font-bold" style="height:{h}px;">&nbsp;</div><button class="px-1 text-amber-500 text-xs">&#9654;'
               '</button></div></div>',
    "toggle": '<div class="flex flex-col items-center"><span class="text-[9px] font-bold text-neutral-400 uppercase mb-1">{label}'
              '</span><button class="aug-toggle px-3 py-1.5 text-[10px] font-mono rounded bg-neutral-800 text-neutral-300 font-bold '
              'border border-neutral-700" data-param="{key}">OFF</button></div>',
    "button": '<button class="aug-btn px-3 py-1.5 text-[10px] font-mono rounded bg-amber-500 text-black font-bold" '
              'data-param="{key}">{label}</button>',
}
MONO_SPECS = {
    "sweep": 135, "design_labels": True,
    "knob": {"item": ".rotary-dial", "unit": ".rotary-group", "label": "span", "value": "div:has(> .knob-val)"},
    "enum": {"item": ".aug-enum", "opt": "button", "label": "span"},
    "popup": {"item": "#machine-trigger, .aug-popup", "label": "label"},
    "stepper": {"item": "div:has(> .lfo-step-btn), .aug-stepper"},
    "toggle": {"item": ".aug-toggle", "label": "span"},
    "button": {"item": ".aug-btn", "selfLabel": True},
    "drop": ["#mpc-screen > header", "#mpc-screen > footer"], "autodrop": False,   # (its panels say "Q-LINK ROW 1")
    "tpl": MONO_TPL,
}
MONO_COMMON = [{"sel": ".knob-arc", "all": True, "remove": True},   # value arcs drawn at one value; the pointer turns
               {"sel": "#machine-dropdown", "remove": True}]

# per mockup: selectors for its controls (each mockup was generated with its own class names), the knob sweep, which
# canvas becomes which display, and label -> parameter fixes where the mockup's key or label doesn't name ours
MAPS = {
    "braids": {
        "sweep": 135,
        "knob": {"item": ".knob-disc", "unit": ".knob-widget", "label": ".knob-label", "value": ".knob-val-box"},
        "slider": {"item": ".slider-track-wrap", "unit": ".slider-unit", "label": ".slider-label", "value": ".slider-value-readout"},
        "stepper": {"item": ".stepper-container", "key": "preset"},
        "popup": {"item": ".popup-trigger-btn", "unit": ".algorithm-popup-selector", "label": ".knob-label", "key": "engine"},
        "canvas": {"#braidsWaveCanvas": {"picture": "engine", "fill": ".algorithm-visual-preview"},
                   "#ampCurveCanvas": {"env": ["attack", "decay", "sustain", "release"], "name": "amp"},
                   "#fltCurveCanvas": {"env": ["f_attack", "f_decay", "f_sustain", "f_release"], "name": "flt"}},
        "hide": [".braids-char-matrix", "#ampCurveDuration", "#fltCurvePeak"],
        "get": {"preset": "preset_name"},
        "map": {"amp_a": "attack", "amp_d": "decay", "amp_s": "sustain", "amp_r": "release",
                "flt_a": "f_attack", "flt_d": "f_decay", "flt_s": "f_sustain", "flt_r": "f_release"},
        # design QA 2026-10-03: a Q-Link column per panel (OSCILLATOR | FILTER + MOD | OUTPUT | PROGRAM); envelope
        # curves on the left, sliders on the right (the owner's notes)
        "qlinks": {0: ["engine", "timbre", "color", "-", "cutoff", "resonance", "filt_env", "fm",
                       "octave_transpose", "volume", "-", "-", "preset", "-", "-", "-"]},
        "augment": [{"sel": "#pageEnvelopes .envelope-bank-layout", "all": True,
                     "attr": {"style": "flex-direction: row-reverse"}}],
    },
    "moog": {
        "sweep": 135,
        "knob": {"item": ".knob-housing", "unit": ".knob-control", "label": ".knob-label", "value": ".knob-val"},
        "enum": {"item": ".enum-box", "opt": ".enum-opt", "unit": ".enum-control", "label": ".enum-label"},
        "stepper": {"item": ".patch-stepper", "key": "preset"},
        "canvas": {"#scopeCanvasMain": {"picture": "osc1_wave", "fill": ".moog-scope-box"},
                   "#ampEnvSvg": {"env": ["attack", "decay", "sustain", "release"], "name": "amp"},
                   "#fltEnvSvg": {"env": ["f_attack", "f_decay", "f_sustain", "f_release"], "name": "flt"}},
        "get": {"preset": "preset_name"},
        "map": {},
    },
    "noisemaker": {
        # ⚠ a rerun no longer reproduces this screen (it renders 25 px lower): layout.conf has hand edits since
        # 2026-10-03 (BANK under PATCH, a shorter scope; see its header). Re-add those after any rerun.
        "sweep": 140,
        "knob": {"item": ".knob-body", "unit": ".knob-unit", "label": ".knob-label", "value": ".knob-val"},
        "slider": {"item": ".slider-track", "unit": ".slider-unit", "label": ".knob-label", "value": ".knob-val"},
        "toggle": {"item": ".toggle-switch", "unit": ".toggle-unit", "label": ".knob-label"},
        "enum": {"item": ".seg-control", "opt": ".seg-btn", "label": ".knob-label"},
        "stepper": {"item": ".dotmatrix-lcd", "key": "preset"},
        "popup": {"item": "select.popup-select", "label": ".knob-label"},
        "autodrop": False, "drop": [".mpc-hardware-bar", ".page-tabs", ".qlink-bar", ".qlinks-bar", ".qlink-footer"],
        "canvas": {"#main-scope-canvas": {"picture": "osc1_wave", "fill": ".scope-container"},
                   "#amp-adsr-canvas": {"env": ["aenv_a", "aenv_d", "aenv_s", "aenv_r"], "name": "amp"},
                   "#filter-adsr-canvas": {"env": ["fenv_a", "fenv_d", "fenv_s", "fenv_r"], "name": "flt"}},
        "get": {"preset": "preset_name"},
        "map": {"KEY TRIG": ["lfo1_keytrig", "lfo2_keytrig"], "SYNC": "delay_sync", "2X L": "delay_fac_l",
                "2X R": "delay_fac_r", "DRAW DEST": "env_dest"},
    },
    "aphex": {
        "sweep": 135,
        # MAIN's 2x2 knob block sits 99 px apart: 6 px more each way so the name/value boxes don't overlap
        "nudge": {"octave": [0, -6], "portamento": [0, -6], "master_tune": [0, 6], "drive": [0, 6]},
        "drop": ["[data-purpose=hardware-header]", "[data-purpose=qlink-encoder-strip]"],
        "knob": {"item": ".knob-cap", "unit": ".knob-container", "label": "span", "value": ".readout-box"},
        "enum": {"item": "div:has(> .seg-btn)", "opt": ".seg-btn", "label": "span"},
        "popup": [{"item": "#preset-select", "unit": ".mt-2", "label": "label", "key": "preset"},
                  {"item": "select:not(#preset-select)", "unit": "div:has(> span)", "label": "span"}],
        "toggle": {"item": "button[onclick^='toggleActive']", "selfLabel": True},
        "hide": [".patch-socket", "div:has(> span > strong)", "button[onclick*='lear']"],   # decorative jack lights, cord count
        "button": {"item": "button[onclick^='randomizePatch'], button[onclick^='mutatePatch'], button[onclick^='resetDefaults'], "
                           "button[onclick^='triggerRandomMod'], #main-gate-btn", "selfLabel": True},
        "canvas": {"#env-canvas": {"env": ["e2_atk", "e2_dcy", "e2_sus", "e2_rel"], "name": "eg2", "fill": "div.flex-1"}},
        "map": {"MASTER VOL": "volume", "DETUNE": "v2_detune", "VCO1 SCALE (FOOTAGE)": "v1_pitch",
                "VCO1 WAVEFORM": "v1_wave", "VCO2 SCALE (FOOTAGE)": "v2_pitch", "VCO2 WAVEFORM": "v2_wave",
                "VCO1 LVL": "mix_v1", "VCO2 LVL": "mix_v2", "SUB LVL": "mix_sub", "NOISE LVL": "mix_noise",
                "ESP IN LVL": "mix_esp", "HP>LP FBK": "mix_fb", "FILTER ROUTING MODE": "filter_mode",
                "CIRCUIT REVISION": "filter_rev", "PITCH TRACK MODE": "esp_pitch_mode", "GATE OUT POLARITY": "esp_gate_pol",
                "TOTAL EXT MOD (T.EXT)": "pb_total", "VCO FREQ EXT": "pb_freq", "HPF CUTOFF IN": "pb_hpf_cv",
                "LPF CUTOFF IN": "pb_lpf_cv", "INITIAL GAIN (VCA)": "pb_vca_in", "EXT SIGNAL IN": "pb_ext_sig",
                "RANDOM": "rnd_patch", "MUTATE": "mutate", "RESET INITIAL PATCH": "reset_patch",
                "GATE TRIGGER (C2)": "trigger", "VCO2 SYNC": "v2_sync", "VCO2 FM": "v2_xmod",
                "MS-10 SINGLE OSC MODE": "ms10_mode", "GENERATE PARAM RANDOM": "rnd_mod", "GATE": "trigger",
                "RESET": "reset_patch"},
        # design QA 2026-10-03 (the owner's notes): one Q-Link column per panel or row ("-" = an empty slot), patch
        # actions in their own box, VCO selectors above their knobs, the envelope curve on the left
        "qlinks": {
            0: ["lpf_cut", "lpf_reso", "hpf_cut", "hpf_reso", "mg_freq", "mg_depth", "volume", "-",
                "octave", "portamento", "master_tune", "drive", "preset", "-", "-", "-"],
            1: ["v1_pitch", "v1_wave", "-", "-", "v1_pw", "v1_drift", "vco_mg", "-",
                "v2_pitch", "v2_wave", "v2_sync", "v2_xmod", "v2_fine", "v2_detune", "vco_eg", "v2_drift"],
            2: ["mix_v1", "mix_v2", "mix_sub", "mix_noise", "noise_color", "mix_esp", "mix_fb", "-",
                "hpf_mg", "hpf_eg", "lpf_mg", "lpf_eg", "filter_mode", "filter_rev", "-", "-"],
            3: ["e1_delay", "e1_atk", "e1_rel", "-", "e2_atk", "e2_dcy", "e2_sus", "e2_rel",
                "e2_hold", "-", "-", "-", "mg_shape", "mg_pw", "-", "-"],
        },
        "augment": [
            # MAIN: RANDOM / MUTATE / GATE / RESET together in a PATCH ACTIONS box (the VOICE panel's trigger pad goes)
            {"sel": "#view-main div.flex.flex-col:has(> #main-gate-btn)", "remove": True},
            {"sel": "#view-main div.flex.flex-col.gap-2:has(> div.grid > button[onclick^='randomizePatch'])", "each": [
                '<div class="border border-[#2a2a2a] bg-[#0a0a0a] p-2 rounded"><div class="text-[10px] text-[#7c7c74] '
                'uppercase font-mono mb-2">PATCH ACTIONS</div><div class="grid grid-cols-2 gap-2">'
                '<button class="h-9 bg-[#1e1e1e] border border-[#3c3c3c] text-xs font-bold text-[#cfcfc8] tracking-wider uppercase rounded" onclick="randomizePatch()">RANDOM</button>'
                '<button class="h-9 bg-[#1e1e1e] border border-[#3c3c3c] text-xs font-bold text-[#cfcfc8] tracking-wider uppercase rounded" onclick="mutatePatch()">MUTATE</button>'
                '<button class="h-9 bg-[#1e1e1e] border border-[#3c3c3c] text-xs font-bold text-[#cfcfc8] tracking-wider uppercase rounded" id="main-gate-btn">GATE</button>'
                '<button class="h-9 bg-[#1e1e1e] border border-[#3c3c3c] text-xs font-bold text-[#cfcfc8] tracking-wider uppercase rounded" onclick="resetDefaults()">RESET</button>'
                '</div></div>']},
            # VCO: selectors on top, knobs at the bottom; VCO2's SYNC / FM between (the made-up status box goes)
            {"sel": "#view-vco > div > div:nth-child(1) > div:nth-child(2)", "remove": True},
            {"sel": "#view-vco > div > div:nth-child(1) > div:nth-child(1)", "attr": {"style": "display: contents"}},
            {"sel": "#view-vco div.grid:has(> [data-knob='vco1Pw'])", "attr": {"style": "margin-top: auto"}},
            {"sel": "#view-vco > div > div:nth-child(2) > div:nth-child(1)", "attr": {"style": "display: contents"}},
            {"sel": "#view-vco div.grid:has(> [data-knob='vco2Pitch'])", "attr": {"style": "order: 2; margin-top: auto"}},
            {"sel": "#view-vco div.grid:has(> button[onclick^='toggleActive'])", "attr": {"style": "order: 1"}},
            # MIXER: room between the rows, so each row's Q-Link outline stays clear of the next
            {"sel": "#view-mixer div.grid.grid-cols-3:has(> [data-knob='espLevel'])", "attr": {"style": "margin-top: 40px"}},
            {"sel": "#view-mixer div.grid.grid-cols-2.gap-3", "attr": {"style": "margin-top: 40px"}},
            # ENVELOPES: the curve on the left, the knobs on the right
            {"sel": "#view-envelopes div.col-span-4.panel-frame", "attr": {"style": "order: -1"}},
            # MODERN: a heading over a switch with no parameter, and a doubled word
            {"sel": "#view-modern div.grid.grid-cols-3 > div:nth-child(2)", "each": [""]},
            {"sel": "#view-modern", "retext": [["EMULATION EMULATION MODEL", "EMULATION MODEL"]]},
        ],
    },
    "303": {
        "augment": [{"sel": "body", "retext": [["ROLAND", "OPEN303"], ["TB-303", "ACID"]]}],   # no maker badge in the art
        "sweep": 135,
        "knob": {"item": ".knob-wrapper", "unit": ".control-unit", "label": ".control-label", "value": ".control-value"},
        "enum": {"item": ".enum-v-selector", "opt": ".enum-opt", "unit": ".control-unit", "label": ".control-label"},
        "toggle": {"item": ".hw-toggle-btn", "unit": ".control-unit", "label": ".control-label"},
        "canvas": {"#acidScopeCanvas": {"picture": "waveform"}},
        "map": {"WAVEFORM": "waveform", "DRIVE MODEL": "drive_model", "selectWave": "waveform",
                "selectDriveModel": "drive_model", "DEVILFISH": "devil_mod_switch"},
        # one Q-Link column per panel (2026-10-03, design QA): VCO | VCF | ACCENT + OUT | DRIVE ("-" = an empty slot)
        "qlinks": {0: ["waveform", "tuning", "-", "-", "cutoff", "resonance", "env_mod", "decay", "accent", "volume", "-", "-",
                       "drive_model", "drive", "drive_mix", "tanh_shaper_drive"]},
    },
    "hera": {
        # both pages drawn; HPF / VCF KYBD / VCF BEND / VCA LEVEL / LFO TRIG added in its markup, the cutoff-envelope
        # graphic follows the ADSR faders
        "sweep": 135, "design_labels": True, "rot": ".knob-cap", "center": False,
        "tabs": "[id^=nav-btn-]",
        "knob": {"item": "[id^=knob-]", "body": ".knob-face", "unit": "div.flex-col:has(> [id^=knob-])", "label": "span",
                 "value": ".recessed-well:has(> span[id^=val-])"},
        "slider": {"item": ".fader-container", "unit": "div.flex-col:has(> .fader-container)", "label": "span"},
        "toggle": {"item": "#btn-chorus-1, #btn-chorus-2", "selfLabel": True},
        "enum": {"item": "div.flex-col:has(> #btn-range-4), div.flex-col:has(> #btn-pwm-man), div.flex-col:has(> #btn-vca-env), .aug-enum",
                 "opt": "button", "label": "span"},
        "stepper": {"item": ".recessed-well:has(#main-patch-title)", "key": "preset"},
        "get": {"preset": "preset_name"},
        "names": {"lfo_rate": "LFO RATE", "lfo_delay": "LFO DELAY", "vcf_lfo": "VCF LFO", "vcf_env": "VCF ENV"},
        "hide": ["div:has(> #chorus-mode-label)"],
        "canvas": {"#vcfCurveSvg": {"env": ["attack", "decay", "sustain", "release"], "name": "env"}},
        "tpl": {
            "knob": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-on-surface-variant mb-1">'
                    '{label}</span><div class="relative w-14 h-14" data-param="{key}" id="knob-aug-{key}"><div class="w-14 h-14 '
                    'rounded-full knob-face border border-outline flex items-center justify-center"><div class="w-11 h-11 '
                    'rounded-full knob-cap relative flex items-center justify-center"><div class="knob-pointer absolute top-1 w-1 '
                    'h-3 rounded-full bg-juno-accent-hi shadow-[0_0_4px_#ff6b3d]"></div></div></div></div><div class="mt-2 '
                    'recessed-well px-2 py-0.5 rounded border border-outline-variant font-label-sm text-label-sm text-primary '
                    'text-center w-14"><span id="val-aug-{key}">0</span></div></div>',
            "enum": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-on-surface-variant mb-2">'
                    '{label}</span><div class="aug-enum flex flex-col gap-1.5" data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="tactile-btn px-3 py-1 rounded bg-surface-container text-[10px] font-label-sm border '
                        'border-outline-variant">{opt}</button>',
        },
        "augment": [
            {"sel": "body > main", "attr": {"style": "top: 55px;"}},   # fixed under the (dropped) header: centre it
            {"sel": "div.flex-col:has(> #btn-range-4)", "attr": {"data-param": "pitch_range"}},
            {"sel": "div.flex-col:has(> #btn-pwm-man)", "attr": {"data-param": "pwm_mod"}},
            {"sel": "div.flex-col:has(> #btn-vca-env)", "attr": {"data-param": "vca_type"}},
            {"sel": "div.grid:has(> button[onclick^=stepPreset])", "remove": True},   # the stepper has its own arrows
            {"sel": "div.flex-col:has(> #btn-hpf-step)", "where": "replace", "html": "@knob(hpf)"},
            {"sel": "div.recessed-well:has(> div > #lfo-blinker)", "where": "replace", "html": "@enum(lfo_trigger)"},
            {"sel": "div.flex-col:has(> div > #btn-env-pos)", "where": "replace", "html": "@knob(vcf_key)@knob(vcf_bend)"},
            {"sel": "div.recessed-well:has(> div.led-glow-red)", "where": "replace", "html": "@knob(vca_depth)"},
        ],
        "map": {"octave": "octave_transpose", "vol": "volume", "cutoff": "vcf_cutoff", "res": "vcf_resonance",
                "env-depth": "vcf_env", "vcf-lfo": "vcf_lfo", "dco-lfo": "pitch_mod", "pwm-depth": "pwm_depth",
                "lfo-rate": "lfo_rate", "lfo-delay": "lfo_delay", "pulse": "pulse_level", "saw": "saw_level",
                "sub": "sub_level", "noise": "noise_level", "chorus-1": "chorus_i", "chorus-2": "chorus_ii"},
    },
    "libpo32": {
        # all three pages drawn; EDIT's knobs/switches with no parameter go (NOISE ENV takes CUTOFF FREQ's place)
        "sweep": 135, "design_labels": True, "tabs": "[onclick^=switchTab]", "drop": ["header", "footer"],
        "knob": {"item": ".knob-pot", "unit": "div.flex-col:has(> .knob-pot)", "label": "span, div[id^=val-]"},
        "slider": {"item": ".fader-track", "unit": "div:has(> .fader-track)", "label": "span"},
        "enum": {"item": "#view-edit div.grid:has(> button), .aug-enum", "opt": "button", "label": "span"},
        "popup": {"item": "div:has(> .edit-voice-btn)", "label": "span"},
        "button": {"item": "button[onclick^=randomizeAllKit]", "selfLabel": True},
        "stepper": {"item": ".po-lcd", "key": "kit"},
        "get": {"kit": "kit_name"},
        "tpl": {
            "enum": '<div class="flex flex-col gap-2 items-center mt-8"><span class="text-label-sm text-[#c3e2d4]">{label}</span>'
                    '<div class="aug-enum grid grid-cols-1 gap-1" data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="py-1 px-3 text-[10px] font-bold rounded bg-[#071f19] text-[#c3e2d4] border '
                        'border-[#1c4a3d]">{opt}</button>',
        },
        "augment": [
            {"sel": "button[onclick^=triggerDrumHit]", "all": True, "remove": True},   # pad / HIT / audition buttons
            {"sel": "button[onclick^=randomizeAllKit] .material-symbols-outlined", "remove": True},
            {"sel": "button[onclick^=randomizeAllKit]", "attr": {"data-param": "randomize"}},
            {"sel": "div:has(> .edit-voice-btn)", "attr": {"data-param": "inst"}},
            {"sel": "#view-edit div.grid:has(> button)", "nth": ["inst_wave", "inst_mod_mode", "inst_noise_filt", "x_sat"]},
            {"sel": "#view-edit span:has(+ div.grid[data-param=x_sat])", "remove": True},
            {"sel": "#view-edit div.grid[data-param=x_sat]", "remove": True},
            {"sel": "#view-edit .knob-pot", "nth": ["inst_freq", "inst_dcy", "inst_mod_amt", "x_drop", "inst_noise", "x_cutoff",
                                                  "inst_dist", "inst_level"]},
            {"sel": "#view-edit div.flex-col:has(> .knob-pot[data-param=x_drop])", "remove": True},
            {"sel": "#view-edit div.flex-col:has(> .knob-pot[data-param=x_cutoff])", "where": "replace", "html": "@enum(inst_noise_env)"},
            {"sel": "#view-tune .knob-pot", "nth": ["v%02d_%s" % (v, k) for v in range(1, 9) for k in ("freq", "dcy")]},
        ],
        "map": {"master-lvl": "level", "decay-scale": "decay", **{"fader-%d" % v: "v%02d_lvl" % v for v in range(1, 9)}},
        "names": {"inst_level": "PAD LEVEL", "inst_dist": "DISTORTION"},
    },
    "midiplayer": {
        # three parameters; the design's transpose/velocity sliders, speed and transport buttons have none: removed
        "sweep": 135, "tabs": "#no-tabs", "page": "PLAYER",
        "stepper": {"item": "div.recessed-display:has(> button[onclick='prevFile()'])", "key": "file_index"},
        "get": {"file_index": "file_name_display"},
        "popup": {"item": "#track-select", "label": "span"},
        "toggle": {"item": "#loop-btn", "selfLabel": True},
        "augment": [
            {"sel": "div.grid.grid-cols-4:has(input#transpose-slider)", "remove": True},
            {"sel": "div.flex-col:has(> div > button.speed-btn)", "remove": True},
            {"sel": "#loop-btn", "attr": {"data-param": "loop"}},
            {"sel": "#track-select", "attr": {"data-param": "track"}},
        ],
    },
    "marbles": {
        # one drawn page; its frozen MIDI OUT note readouts become the routing controls; SETUP drawn in its style
        "sweep": 135, "design_labels": True, "tabs": "#tab-marbles", "drop": ["header", "footer"], "center": False,
        "qlinks_skip": ["t1_out", "t2_out", "t3_out"],   # 17 on the page: the routing switches stay touch-only
        "knob": {"item": ".knob-container", "unit": "div:has(> .knob-container)", "label": "[id^=readout-], .text-mpc-ink-dim"},
        "enum": [{"item": "div:has(> .deja-t-btn), .aug-enum", "opt": "button", "label": ".text-mpc-ink-dim"},
                 {"item": "div:has(> .deja-x-btn)", "opt": "button", "dir": "v"}],   # in a header: stacked, it fits
        "popup": {"item": "[id^=popup-], .aug-popup", "label": ".text-mpc-ink-dim"},
        "toggle": {"item": ".aug-toggle", "label": ".text-mpc-ink-dim"},
        "tpl": {
            "frame": '<section class="module-card rounded p-2.5 flex flex-col absolute border-t-2 border-t-mpc-accent" style="left:{x}px; '
                     'top:{y}px; width:{w}px; height:{h}px;"><div class="flex items-center justify-between pb-1.5 border-b '
                     'border-mpc-line/60"><span class="text-xs font-bold text-mpc-ink font-label-lg tracking-wider">{title}</span>'
                     '</div></section>',
            "knob": '<div class="flex flex-col items-center gap-1"><div class="knob-container w-14 h-14" data-param="{key}"><div '
                    'class="knob-body w-full h-full"><div class="knob-indicator" style="transform: rotate(0deg);"></div></div></div>'
                    '<div class="text-label-sm font-label-sm text-mpc-ink-dim">{label}</div></div>',
            "enum": '<div class="flex flex-col items-center gap-1"><div class="text-label-sm font-label-sm text-mpc-ink-dim mb-1">'
                    '{label}</div><div class="aug-enum flex flex-col bg-surface-container-lowest border border-mpc-line rounded p-0.5 '
                    'gap-0.5" data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="px-3 py-0.5 text-label-sm font-label-sm text-mpc-ink-dim rounded">{opt}</button>',
            "toggle": '<div class="flex flex-col items-center gap-1"><div class="text-label-sm font-label-sm text-mpc-ink-dim">{label}'
                      '</div><button class="aug-toggle tactile-btn px-3 py-1.5 rounded border border-mpc-line text-label-sm '
                      'font-label-sm text-mpc-ink" data-param="{key}">OFF</button></div>',
            "popup": '<div class="flex flex-col gap-1"><div class="text-label-sm font-label-sm text-mpc-ink-dim">{label}</div><div '
                     'class="aug-popup flex items-center gap-1 bg-surface-container-lowest border border-mpc-line px-2 py-1 rounded" '
                     'style="width:{w}px;" data-param="{key}"><span class="text-label-sm font-label-sm text-mpc-accent-hi '
                     'font-bold">&nbsp;</span></div></div>',
            "row": '<div class="flex items-center justify-around w-full flex-1">{items}</div>',
        },
        "augment": [
            {"sel": "main > div.grid", "attr": {"style": "padding-top: 10px; padding-bottom: 10px; height: 628px; "
                                                         "grid-template-rows: minmax(0,1fr) minmax(0,1fr);"}},   # header/footer dropped
            {"sel": "main canvas", "all": True, "attr": {"style": "height: 100px; max-height: 100px; flex: none;"}},
            {"sel": "div:has(> div > #knob-jitter), div:has(> div > #knob-x-bias)", "all": True,
             "attr": {"class": "flex flex-row gap-16 items-center"}},   # two small knobs side by side: room for MPC's text
            {"sel": "div:has(> .deja-t-btn)", "attr": {"data-param": "t_deja_vu"}},
            {"sel": "div:has(> .deja-x-btn)", "attr": {"data-param": "x_deja_vu"}},
            {"sel": "div.grid.grid-cols-3:has(> .recessed-display)", "where": "replace",
             "html": "@row(channels, t1_out, t2_out, t3_out)"},
        ],
        "map": {"t-model": "t_model", "clock-div": "clock_div"},
        "grid": {"tabs": ["SETUP"], "tpl": None, "skip": ["channels", "t1_out", "t2_out", "t3_out"]},
        "tab_order": ["MARBLES", "SETUP"],
    },
    "hush1": dict(HUSH := {
        # pages 1-2 from the design (+ the SOURCE controls it left out); its pages 3-4 were mostly frozen readouts, so
        # MODULATOR and PERFORM are drawn in its style from the port's plan
        "sweep": 135, "design_labels": True, "rot": "div.rounded-full:has(> .knob-indicator)",
        "tabs": "#tab-btn-main, #tab-btn-source",
        "knob": {"item": "div.relative:has(> svg + div > .knob-indicator)", "unit": "div.flex-col:has(> div.relative > svg)",
                 "label": "span"},
        "slider": {"item": ".fader-slot", "unit": "div.flex-col:has(> .fader-slot)", "label": "span"},
        "enum": {"item": "div:has(> #btn-vca-env), div.grid:has(> #sub-1oct), .aug-enum", "opt": "button", "label": "span"},
        "toggle": {"item": ".aug-toggle", "label": "span"},
        "popup": {"item": ".aug-popup", "label": "span"},
        "stepper": {"item": "div:has(> div > button[onclick='prevPreset()'])", "key": "preset"},
        "get": {"preset": "preset_name"},
        "tpl": {
            "frame": '<section class="milled-panel rounded absolute flex flex-col p-space-md" style="left:{x}px; top:{y}px; '
                     'width:{w}px; height:{h}px;"><div class="flex justify-between items-center px-1 border-b '
                     'border-surface-variant pb-1"><span class="font-label-md text-label-md text-primary tracking-wider">{title}'
                     '</span></div></section>',
            "knob": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-on-surface-variant mb-1">'
                    '{label}</span><div class="relative w-[64px] h-[64px] flex items-center justify-center" data-param="{key}">'
                    '<svg class="w-full h-full transform -rotate-90"><circle cx="32" cy="32" fill="none" r="27" stroke="#292a2d" '
                    'stroke-width="5"></circle></svg><div class="absolute w-[48px] h-[48px] rounded-full bg-surface-container-high '
                    'border-2 border-outline flex items-center justify-center shadow-lg"><div class="w-1 h-5 bg-primary '
                    'rounded-full absolute top-1 knob-indicator"></div></div></div><span class="text-label-sm font-label-sm '
                    'text-primary mt-1 font-mono">0</span></div>',
            "enum": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-on-surface-variant mb-1">'
                    '{label}</span><div class="aug-enum flex flex-col gap-1" data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="py-1 px-3 font-label-sm text-label-sm rounded border border-outline-variant '
                        'text-on-surface-variant">{opt}</button>',
            "toggle": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-on-surface-variant '
                      'mb-1">{label}</span><button class="aug-toggle bg-surface-container px-3 py-2 border border-outline rounded '
                      'text-label-sm font-label-sm text-primary" data-param="{key}">OFF</button></div>',
            "popup": '<div class="flex flex-col"><span class="text-label-sm font-label-sm text-on-surface-variant mb-1">{label}</span>'
                     '<div class="aug-popup bg-surface-container px-3 py-2 border border-outline rounded text-label-sm '
                     'font-label-sm text-primary" style="width:{w}px;" data-param="{key}">&nbsp;</div></div>',
            "row": '<div class="flex items-center justify-around w-full">{items}</div>',
        },
        "augment": [
            {"sel": "svg circle[id^=arc-]", "all": True, "remove": True},   # value arcs drawn at one value
            {"sel": ".knob-indicator", "all": True, "attr": {"style": ""}},   # turned by the knob body instead
            {"sel": "div:has(> #btn-vca-env)", "attr": {"data-param": "vca_mode"}},
            {"sel": "div.grid:has(> #sub-1oct)", "attr": {"data-param": "sub_mode"}},
            {"sel": "div.flex-col:has(> div > #oct-16)", "where": "replace", "html": "@knob(octave_transpose)"},
            {"sel": "#view-source div.pt-4", "where": "replace",
             "html": '<div class="pt-4 border-t border-surface-variant">@row(pulse_width, pwm_mode, pwm_depth, pwm_env_depth)</div>'},
            {"sel": "#view-source section:first-child > div.justify-around", "attr": {"style": "height: 240px;"}},
            {"sel": "#view-source section:first-child", "html":
             '<div class="border-t border-surface-variant pt-2">@row(transpose, fine_tune, white_noise, f_attack, f_decay, '
             'f_sustain, f_release)</div>'},
        ],
        "map": {"cutoff-pointer": "cutoff", "reso-pointer": "resonance", "env-amt-pointer": "env_amt",
                "key-track-pointer": "key_follow", "velo-pointer": "velocity_sens", "vol-pointer": "volume"},
        "names": {"f_attack": "FLT ATTACK", "f_decay": "FLT DECAY", "f_sustain": "FLT SUSTAIN", "f_release": "FLT RELEASE"},
        "tab_order": ["MAIN", "SOURCE", "MODULATOR", "PERFORM"],
    }, grid={"tabs": ["MODULATOR", "PERFORM"], "tpl": None}),
    "mazelite": {
        # one page holds both of its tabs; the RESET choices are pop-ups (bar lengths), in place of a made-up route row
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "MAZE",
        "drop": ["#mpc-enclosure > header", "#mpc-enclosure > footer"],
        "knob": {"item": ".knob-dial", "unit": "div.flex-col:has(> .knob-dial)", "label": "span"},
        "popup": {"item": "select, .aug-popup", "label": "span, label"},
        "button": {"item": "button[onclick^=triggerBitFlip], button[onclick^=triggerAdvance]", "selfLabel": True},
        "qlinks_skip": ["s1_bit_flip", "s1_advance", "s2_bit_flip", "s2_advance"],   # triggers: touch them
        "labels": {"s1_bit_flip": "S1 FLIP", "s1_advance": "S1 STEP", "s2_bit_flip": "S2 FLIP", "s2_advance": "S2 STEP"},
        "tpl": {"popup": '<div class="flex flex-col"><span class="text-[10px] font-label-sm text-on-surface-variant mb-1">{label}</span>'
                     '<div class="aug-popup bg-surface-container px-2 py-1 rounded border border-outline-variant text-label-sm '
                     'font-mono text-primary" style="width:130px;" data-param="{key}">&nbsp;</div></div>', "select": '<div class="flex flex-col"><span class="text-[10px] font-label-sm text-on-surface-variant mb-1">{label}</span>'
                     '<div class="aug-popup bg-surface-container px-2 py-1 rounded border border-outline-variant text-label-sm '
                     'font-mono text-primary" style="width:130px;" data-param="{key}">&nbsp;</div></div>',
                "row": '<div class="mt-2 flex items-center justify-around">{items}</div>'},
        "augment": [
            {"sel": "#s1-bits, #s2-bits", "all": True, "attr": {"style": "grid-template-columns: repeat(16, minmax(0, 1fr));"}},
            {"sel": "div:has(> button[onclick='triggerResetBoth()'])", "where": "replace", "html": "@popup(g_reset)"},
            {"sel": "section div.mt-2.bg-surface-container-low", "where": "replace", "html": "@row(s1_reset, s2_reset)"},
        ],
        "map": {"param-scale": "scale", "param-rate": "note_rate", "param-len": "note_length", "s1-corrupt": "s1_corrupt",
                "s1-range": "s1_cv_range", "s1-len": "s1_length", "s1-trig": "trig_mix", "s2-corrupt": "s2_corrupt",
                "s2-range": "s2_cv_range", "s2-len": "s2_length", "s2-trig": "trig_mix_b",
                "S1 ADVANCE >": "s1_advance", "S2 ADVANCE >": "s2_advance"},
    },
    "monksynth": {
        # SINGER from the design (Stitch never drew CHOIR: asked 2026-10-02, the edit failed); CHOIR drawn in its style
        "sweep": 135, "design_labels": True, "tabs": "#tab-singer",
        "knob": {"item": ".knob-outer", "unit": ".knob-container", "label": "span", "value": ".display-well"},
        "enum": {"item": ".aug-enum", "opt": "button", "label": "span"},
        "stepper": {"item": "div.display-well:has(> #preset-prev)", "key": "preset"},
        "get": {"preset": "preset_name"},
        "hide": ["#f1-val", "#f2-val", "#f3-val"],
        "canvas": {"#adsr-canvas": {"env": ["attack", "decay", "sustain", "release"], "name": "amp"}},
        "tpl": {
            "frame": '<div class="module-card rounded p-2.5 flex flex-col absolute" style="left:{x}px; top:{y}px; width:{w}px; '
                     'height:{h}px;"><div class="flex items-center justify-between border-b border-[#4a3352]/60 pb-1 px-1"><span '
                     'class="text-body-md font-headline-sm font-bold text-[#f8eefa]">{title}</span></div></div>',
            "knob": '<div class="knob-container" data-param="{key}"><span class="text-label-sm font-label-sm font-bold '
                    'text-[#dcc8e0] mb-1">{label}</span><div class="knob-outer"><div class="knob-tick-track"></div><div '
                    'class="knob-indicator-dot" style="transform: rotate(0deg) translateY(0px);"></div></div><div '
                    'class="display-well rounded px-2 py-0.5 mt-1.5 text-center w-14"><span class="font-readout-numeric '
                    'text-[11px] font-bold text-[#f5c451] knob-num">0</span></div></div>',
            "enum": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm font-bold text-[#8c7392] mb-1">'
                    '{label}</span><div class="aug-enum flex flex-col gap-1" data-param="{key}">{opts}</div></div>',
            "art": '<div class="module-card rounded absolute flex flex-col items-center justify-center" style="left:{x}px; '
                   'top:{y}px; width:{w}px; height:{h}px;"><span class="font-headline-sm text-[64px] tracking-[0.3em] font-bold '
                   'text-[#f5c451]">MONKSYNTH</span><span class="text-label-sm font-label-sm text-[#8c7392] tracking-widest mt-2">'
                   'FORMANT SINGING VOICE // CHOIR</span></div>',
            "opt_enum": '<button class="vowel-btn tactile-btn px-4 py-1 rounded text-label-md font-label-md font-bold '
                        'text-[#8c7392]">{opt}</button>',
        },
        "augment": [
            {"sel": "div:has(> #phoneme-buttons)", "remove": True},   # vowel shortcuts: no parameter
            {"sel": "div:has(> div > span.w-2.h-2.rounded-full)", "remove": True},   # frozen status row
        ],
        "grid": {"tabs": ["CHOIR"], "tpl": None},
        "tab_order": ["SINGER", "CHOIR"],
    },
    "mrdrums": {
        # KIT from the design; its pad buttons (MPC has real pads) become the EDIT PAD stepper, its PUNCH/CRISP/CLIP and
        # ANALOG SAT (no parameters) go, RAND LOOP takes their plate; PAD drawn in its style
        "sweep": 135, "design_labels": True, "tabs": "#tab-kit", "twice": ["ui_current_pad"], "drop": ["header", "footer"],
        "knob": {"item": ".moog-knob-housing", "unit": "div.flex-col:has(> .moog-knob-housing)", "label": "span",
                 "value": "[id^=val-]"},
        "enum": {"item": ".aug-enum", "opt": "button", "label": "span"},
        "stepper": [{"item": "div:has(> button[onclick='changeKit(-1)'])", "key": "kit"}, {"item": ".aug-stepper"}],
        "get": {"kit": "kit_name"},
        "toggle": {"item": "#toggle-autoselect, .aug-toggle", "label": "span"},
        "tpl": {
            "frame": '<section class="mpc-panel-bevel rounded p-3 flex flex-col absolute" style="left:{x}px; top:{y}px; width:{w}px; '
                     'height:{h}px;"><div class="flex items-center justify-between border-b border-outline-variant/40 pb-1"><span '
                     'class="font-headline-sm text-headline-sm text-on-surface">{title}</span></div></section>',
            "knob": '<div class="flex flex-col items-center gap-1.5"><span class="font-label-sm text-label-sm text-outline">{label}'
                    '</span><div class="moog-knob-housing" data-param="{key}"><div class="moog-indicator" style="transform: '
                    'rotate(0deg);"></div></div><div class="px-2 py-0.5 bg-surface-container-lowest border border-outline-variant '
                    'rounded font-readout-numeric text-label-md text-secondary" id="val-aug-{key}">0</div></div>',
            "enum": '<div class="flex flex-col items-center gap-1.5"><span class="font-label-sm text-label-sm text-outline">{label}'
                    '</span><div class="aug-enum flex flex-col gap-1" data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="px-2 py-0.5 bg-surface-container-lowest border border-outline-variant rounded '
                        'font-readout-numeric text-label-md text-secondary">{opt}</button>',
            "stepper": '<div class="aug-stepper mpc-well rounded flex items-center justify-center" style="width:{w}px; height:60px;" '
                       'data-param="{key}"><span class="font-label-lg text-label-lg text-secondary">{label}</span></div>',
            "toggle": '<div class="flex flex-col items-center gap-1.5"><span class="font-label-sm text-label-sm text-outline">{label}'
                      '</span><button class="aug-toggle px-3 py-1 rounded bg-secondary text-on-secondary" data-param="{key}">ON'
                      '</button></div>',
        },
        "augment": [
            {"sel": "body", "retext": [["AKAI PROFESSIONAL MPC", "SCHWUNG MODULE"]]},   # no maker badges / wrong routing text in the art (2026-10-03)
            {"sel": "div.flex-col:has(> #knob-vel)", "where": "replace", "html": "@enum(g_vel_curve)"},
            {"sel": "div:has(> button[onclick^=toggleWarmth])", "remove": True},
            {"sel": "#page-kit div:has(> div > div > span.w-2.h-2.rounded-full)", "remove": True},   # limiter / warmth text
            {"sel": "div.mpc-well:has(> #active-pad-display)", "remove": True},
            {"sel": "div.grid.grid-cols-4:has(> .mpc-pad-btn)", "where": "replace", "html":
             '<div class="aug-stepper mpc-well rounded h-24 flex items-center justify-center" data-param="ui_current_pad">'
             '<span class="font-label-lg text-label-lg text-secondary">EDIT PAD</span></div>'},
            {"sel": "div.justify-around:has(#knob-drive)", "where": "replace", "html":
             '<div class="flex items-center justify-around bg-surface-container-high border border-outline-variant rounded p-2 '
             'h-24">@knob(g_rand_loop_steps)</div>'},
        ],
        "map": {"vol": "g_master_vol", "poly": "g_polyphony", "jitter": "g_humanize_ms", "autoselect": "ui_auto_select_pad"},
        "grid": {"tabs": ["PAD"], "tpl": None, "skip": ["g_rand_loop_steps"]},
        "tab_order": ["KIT", "PAD"],
    },
    "mrhyde": {
        # MAIN from the design (every MAIN control); its other 6 pages drawn in its style; the scope shows each MODEL
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "MAIN", "rot": ".knob-core",
        "drop": ["body > header", "body > footer"],
        "knob": {"item": ".knob-bevel", "unit": ".knob-control", "label": "span", "value": ".knob-val"},
        "popup": {"item": "#model-select, .aug-popup", "label": "span"},
        "enum": {"item": "div:has(> .filter-mode-btn), .aug-enum", "opt": "button", "label": "span"},
        "toggle": {"item": ".aug-toggle", "label": "span"},
        "canvas": {"#osc-scope": {"picture": "model", "fill": "div:has(> #osc-scope)"}},
        "tpl": {
            "frame": '<section class="panel-bezel rounded-md p-3 absolute flex flex-col" style="left:{x}px; top:{y}px; width:{w}px; '
                     'height:{h}px;"><div class="flex items-center gap-2 border-b border-[#2b2838] pb-1"><span class="w-1 h-3.5 '
                     'bg-[#93ee5a] rounded-sm"></span><span class="font-headline-sm text-headline-sm text-[#f2eff9] font-bold '
                     'uppercase">{title}</span></div></section>',
            "knob": '<div class="flex flex-col items-center justify-center knob-control" data-id="{key}"><span class="font-label-sm '
                    'text-label-sm text-[#f2eff9] mb-1">{label}</span><div class="relative w-[54px] h-[54px] rounded-full knob-bevel '
                    'flex items-center justify-center"><svg class="absolute inset-0 w-full h-full -rotate-90 pointer-events-none" '
                    'viewBox="0 0 54 54"><circle cx="27" cy="27" fill="none" r="23" stroke="#25232d" stroke-width="3"></circle>'
                    '</svg><div class="w-[42px] h-[42px] rounded-full knob-core flex items-center justify-center relative '
                    'shadow-inner"><div class="knob-indicator w-[3px] h-[10px] bg-[#93ee5a] rounded absolute -top-0.5 '
                    'shadow-[0_0_6px_#93ee5a]"></div><div class="w-1.5 h-1.5 rounded-full bg-[#3a3350]"></div></div></div>'
                    '<div class="lcd-display knob-val">0</div></div>',
            "enum": '<div class="flex flex-col items-center"><span class="font-label-sm text-label-sm text-[#d4c4b0] mb-1">{label}'
                    '</span><div class="aug-enum flex flex-col gap-1" data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="py-1 px-3 font-label-md text-label-md font-bold rounded bg-[#1e1a2b] text-[#d4c4b0]">{opt}'
                        '</button>',
            "popup": '<div class="flex flex-col"><span class="font-label-sm text-label-sm text-[#d4c4b0] mb-1">{label}</span><div '
                     'class="aug-popup lcd-display px-2 py-1 rounded" style="width:{w}px;" data-param="{key}">&nbsp;</div></div>',
            "toggle": '<div class="flex flex-col items-center"><span class="font-label-sm text-label-sm text-[#d4c4b0] mb-1">{label}'
                      '</span><button class="aug-toggle py-1 px-3 font-label-md rounded bg-[#1e1a2b] text-[#93ee5a]" '
                      'data-param="{key}">OFF</button></div>',
        },
        "augment": [
            {"sel": ".knob-arc", "all": True, "remove": True},
            {"sel": ".knob-indicator", "all": True, "attr": {"style": ""}},   # the core turns instead
            {"sel": "div:has(> .filter-mode-btn)", "attr": {"data-param": "filter_mode"}},
            {"sel": "#model-select", "attr": {"data-param": "model"}},
        ],
        "grid": {"tabs": ["LFO ENV", "CYC RAND", "ASSIGN", "PITCH HARM", "TIMB CUT", "VOICE"], "tpl": None},
        "tab_order": ["MAIN", "LFO ENV", "CYC RAND", "ASSIGN", "PITCH HARM", "TIMB CUT", "VOICE"],
    },
    "nusaw": {
        # MAIN from the design; MORE drawn in its style
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "MAIN", "rot": ".aug-needle",
        "drop": ["header", "footer"], "center": False,
        "knob": {"item": ".knob-bezel", "unit": "[id$=-wrap]", "label": "span"},
        "slider": {"item": ".slider-groove", "unit": "[id^=fader-]", "label": "span"},
        "stepper": {"item": ".lcd-display:has(#patch-name)", "key": "preset"},
        "get": {"preset": "preset_name"},
        "tpl": {
            "frame": '<section class="absolute rounded border border-[#2f2a52] bg-[#110f1c] p-3 flex flex-col" style="left:{x}px; '
                     'top:{y}px; width:{w}px; height:{h}px;"><div class="flex items-center gap-2 border-b border-[#2f2a52] pb-1">'
                     '<span class="w-1 h-3.5 bg-[#4fe6ff]"></span><span class="text-label-md font-label-md text-[#f1efff] '
                     'tracking-wider uppercase">{title}</span></div></section>',
            "knob": '<div class="flex flex-col items-center justify-center" id="aug-{key}-wrap" data-param="{key}"><span class="text-label-sm '
                    'font-label-sm text-on-surface-variant uppercase tracking-wider mb-1">{label}</span><div class="w-16 h-16 '
                    'rounded-full knob-bezel relative flex items-center justify-center border border-[#3b3564]"><svg class="w-16 '
                    'h-16 absolute -rotate-90 pointer-events-none"><circle cx="32" cy="32" fill="none" r="26" stroke="#171526" '
                    'stroke-width="4"></circle></svg><div class="aug-needle w-1 h-6 bg-[#4fe6ff] rounded-full absolute '
                    'origin-bottom shadow-[0_0_8px_#4fe6ff]" style="bottom: 50%;"></div><div class="w-6 h-6 rounded-full '
                    'bg-[#110f1c] border border-[#4fe6ff]/40 flex items-center justify-center z-10 shadow"><span class="w-1.5 h-1.5 '
                    'rounded-full bg-[#4fe6ff]"></span></div></div></div>',
        },
        "augment": [
            {"sel": "circle[id^=arc-]", "all": True, "remove": True},
            {"sel": "div.flex.items-center.gap-2:has(> button + span)", "remove": True},   # 24dB LP / ANALOG SAT: no parameters
            {"sel": "main", "attr": {"style": "height: 604px; margin-top: 12px; margin-bottom: 12px;"}},
        ],
        "map": {"saws": "saw_count", "sub": "sub_level", "reso": "resonance", "fenv": "f_amount", "groove-attack": "attack",
                "groove-decay": "decay", "groove-sustain": "sustain", "groove-release": "release"},
        "grid": {"tabs": ["MORE"], "tpl": None},
        "tab_order": ["MAIN", "MORE"],
    },
    "obxd": {
        # MAIN from the design (its 7 tab buttons all show it); OSCILLATORS / FILTER / ENVELOPES / MODULATION in its style
        "nudge": {"bend_range": [-10, 0], "bend_osc2": [10, 0]},   # two 120 px toggle boxes 102 px apart
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "MAIN", "drop": ["header", "footer"],
        "knob": {"item": ".moog-knob", "unit": "div.flex-col:has(> div > .moog-knob)", "label": "span", "value": ".recessed-well"},
        "toggle": {"item": "#toggle-unison, #toggle-asplayed, button.rocker-switch, .aug-toggle", "label": "span"},
        "enum": {"item": "div:has(> .legato-btn), .aug-enum", "opt": "button", "label": "span"},
        "stepper": [{"item": "div:has(> div.recessed-well #preset-title)", "key": "preset"}, {"item": ".aug-stepper"}],
        "get": {"preset": "preset_name", "bank_index": "bank_name"},
        # BANK (2026-10-03) went last so MAIN's Q-Link columns stayed where they were
        "qlinks": {0: ["preset", "volume", "tune", "voice_count", "unison_det", "unison", "as_played", "legato", "portamento",
                       "bend_range", "bend_osc2", "bank_index"]},
        "tpl": {
            "frame": '<section class="absolute bg-ob-panel border border-ob-line rounded p-2.5 flex flex-col shadow-lg" style="left:{x}px; '
                     'top:{y}px; width:{w}px; height:{h}px;"><div class="flex items-center justify-between border-b border-[#2d2f34] '
                     'pb-1 px-1"><div class="flex items-center space-x-2"><span class="h-1.5 w-6 bg-ob-accent inline-block"></span>'
                     '<span class="font-headline-sm text-headline-sm text-ob-ink font-bold tracking-wider">{title}</span></div></div>'
                     '</section>',
            "knob": '<div class="flex flex-col items-center"><span class="font-label-sm text-label-sm text-ob-ink-dim tracking-wider '
                    'uppercase mb-1">{label}</span><div class="relative w-14 h-14 rounded-full bg-ob-knob-ring p-1 flex items-center '
                    'justify-center shadow-inner"><div class="knob moog-knob w-12 h-12 rounded-full relative" data-param="{key}">'
                    '<div class="knob-pointer absolute top-1 left-1/2 -translate-x-1/2 w-1 h-3.5 bg-ob-accent-hi rounded-full '
                    'pointer-events-none"></div><div class="absolute inset-2.5 rounded-full bg-gradient-to-tr from-[#9c9a92] '
                    'to-[#f5f3ec] border border-[#a19f96] pointer-events-none"></div></div></div><div class="mt-2 px-2 py-0.5 '
                    'bg-ob-display-bg border border-ob-line rounded recessed-well"><span class="font-readout-numeric text-[12px] '
                    'text-primary font-bold">0</span></div></div>',
            "toggle": '<div class="flex flex-col items-center"><span class="font-label-sm text-label-sm text-ob-ink-dim tracking-wider '
                      'uppercase mb-2">{label}</span><button class="aug-toggle tactile-btn py-2 px-3 bg-[#2d110f] border '
                      'border-[#ff5646] rounded" data-param="{key}"><span class="w-2 h-2 rounded-full bg-[#ff5646] inline-block">'
                      '</span></button></div>',
            "enum": '<div class="flex flex-col items-center"><span class="font-label-sm text-label-sm text-ob-ink-dim uppercase mb-1">'
                    '{label}</span><div class="aug-enum flex flex-col gap-1" data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="tactile-btn px-3 py-1 rounded border border-ob-line text-label-sm">{opt}</button>',
        },
        "augment": [
            {"sel": "body", "retext": [["OBERHEIM OB-Xd", "OB-Xd"]]},   # no maker badges / wrong routing text in the art (2026-10-03)
            {"sel": ".knob-pointer", "all": True, "attr": {"class": "knob-pointer absolute top-1 left-1/2 -translate-x-1/2 w-1 h-3.5 "
                                                                       "bg-ob-accent-hi rounded-full pointer-events-none"}},
            {"sel": "div:has(> .legato-btn)", "attr": {"data-param": "legato"}},
            {"sel": "div:has(> #toggle-unison)", "attr": {"class": "flex flex-col items-center gap-12"}},   # room for MPC's names
            {"sel": "div:has(> div > button.rocker-switch)", "attr": {"class": "flex items-center gap-14"}},
            # a BANK row (the .fxb banks in /sdcard/vst/obxd/presets) between the patch display and a shorter scope
            {"sel": "div:has(> #scopeCanvas)", "where": "beforebegin", "html":
             '<div class="flex items-center space-x-2 px-1"><span class="font-label-sm text-label-sm text-ob-ink-dim '
             'tracking-widest w-10">BANK</span><div class="aug-stepper flex-1 h-10 bg-ob-display-bg border border-[#48110b] '
             'rounded recessed-well crt-grid" data-param="bank_index"></div></div>'},
            {"sel": "div:has(> #scopeCanvas)", "attr": {"style": "height:80px"}},
        ],
        "map": {"master_vol": "volume", "master_tune": "tune", "voice_spread": "unison_det", "glide_rate": "portamento",
                "asplayed": "as_played"},
        "grid": {"tabs": ["OSCILLATORS", "FILTER", "ENVELOPES", "MODULATION"], "tpl": None},
        "tab_order": ["MAIN", "OSCILLATORS", "FILTER", "ENVELOPES", "MODULATION"],
    },
    "pixelwalkers": {
        # one page, every control drawn
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "MAIN", "drop": ["header", "footer"],
        "knob": {"item": ".knob-shadow", "unit": ".knob-container", "label": "span:not(.knob-readout)", "value": ".knob-readout"},
        "toggle": {"item": "#toggle-birth-note", "label": "span"},
        "button": {"item": "#btn-randomize, #btn-killall", "selfLabel": True},
        "augment": [{"sel": ".knob-arc", "all": True, "remove": True},
                    {"sel": "div:has(> #btn-randomize)", "attr": {"class": "flex flex-col items-center gap-3 py-1"}}],
        "nudge": {"birth_note": [12, 0]},   # its touch box reached past the left edge
        "map": {"birthLevel": "birth_level", "hitLevel": "hit_level", "hitDecay": "hit_decay", "birth-note": "birth_note",
                "killall": "kill_all"},
    },
    "plaits": {
        # PLAITS from the design (MODEL: a pop-up on its display, the prev/next buttons go); PLAY drawn in its style
        "sweep": 135, "design_labels": True, "tabs": "#tabPlaits", "rot": "div[class*=inset-2]",
        "drop": ["header", "footer"],
        "knob": {"item": ".rotary-knob", "unit": "div.flex-col:has(> .rotary-knob)", "label": "span"},
        "popup": {"item": ".dot-matrix:has(#modelName)", "key": "engine"},
        "stepper": {"item": ".aug-stepper"},
        "toggle": {"item": ".aug-toggle", "label": "span"},
        "tpl": {
            "frame": '<section class="hardware-panel rounded absolute p-3 flex flex-col" style="left:{x}px; top:{y}px; width:{w}px; '
                     'height:{h}px;"><div class="flex items-center justify-between border-b border-[#2a322d] pb-1"><span '
                     'class="text-xs font-mono font-bold tracking-wider text-[#8ae673]">&#9632; {title}</span></div></section>',
            "knob": '<div class="flex flex-col items-center"><div class="relative w-20 h-20 rotary-knob" data-key="{key}"><svg '
                    'class="w-20 h-20 -rotate-90"><circle cx="40" cy="40" r="33" stroke-width="5" fill="none" class="track"/>'
                    '</svg><div class="absolute inset-2.5 rounded-full bg-[#eef2ef] border-2 border-[#29302b] shadow-inner flex '
                    'items-center justify-center"><div class="w-1.5 h-1.5 rounded-full bg-[#8ae673] absolute top-2"></div></div>'
                    '</div><span class="text-xs font-mono font-bold mt-2 text-[#f1f5f2]">{label}</span></div>',
            "stepper": '<div class="aug-stepper dot-matrix p-2.5 rounded" style="width:{w}px; height:70px;" data-param="{key}"><div '
                       'class="text-[10px] text-[#77847b] uppercase">{label}</div></div>',
            "toggle": '<div class="flex flex-col items-center"><span class="text-xs font-mono font-bold text-[#f1f5f2] mb-1">{label}'
                      '</span><button class="aug-toggle px-3 py-2 text-xs font-mono font-bold rounded bg-[#1f2622] border '
                      'border-[#3a423d] text-[#8ae673]" data-param="{key}">OFF</button></div>',
        },
        "augment": [
            {"sel": "circle.val", "all": True, "remove": True},   # value arcs drawn at one value
            {"sel": ".val-text", "all": True, "remove": True},    # a value written on the cap would turn with it
            {"sel": "div.grid:has(> #btnPrevModel)", "remove": True},
        ],
        "grid": {"tabs": ["PLAY"], "tpl": None},
        "tab_order": ["PLAITS", "PLAY"],
    },
    "rampage": {
        # RAMPAGE from the design; MIDI drawn in its style
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "RAMPAGE", "rot": ".knob-bevel",
        "drop": ["header", "footer"],
        "knob": {"item": ".knob-knurl", "unit": "div.flex-col:has(> div > .knob-knurl), div.flex-col:has(> .knob-knurl)",
                 "label": "span", "value": "div.font-readout-numeric"},
        "enum": {"item": "div:has(> span + button + button + button), .aug-enum", "opt": "button", "label": "span"},
        "toggle": {"item": "button[data-param^=cycle_], div[data-param=audio], .aug-toggle", "label": "span"},
        "button": {"item": "button[data-param^=trig_]", "label": "span"},
        "tpl": {
            "frame": '<div class="absolute bg-[#c9cbcd] text-[#141416] rounded-[3px] border border-[#8e9297] p-2 flex flex-col '
                     'shadow-lg" style="left:{x}px; top:{y}px; width:{w}px; height:{h}px;"><div class="border-b border-[#8e9297] '
                     'pb-1 font-label-md text-label-md font-extrabold">{title}</div></div>',
            "knob": '<div class="flex flex-col items-center justify-center"><span class="font-label-sm text-label-sm font-extrabold '
                    'text-[#141416] mb-1">{label}</span><div class="w-[64px] h-[64px] rounded-full knob-knurl p-[3px] shadow-md flex '
                    'items-center justify-center" data-param="{key}"><div class="w-[58px] h-[58px] rounded-full knob-bevel relative '
                    'flex items-center justify-center"><div class="knob-detent"></div><div class="w-5 h-5 rounded-full bg-[#3a3c40] '
                    'border border-[#222] flex items-center justify-center"><div class="w-2 h-2 rounded-full bg-[#111]"></div></div>'
                    '</div></div></div>',
            "enum": '<div class="flex flex-col items-center"><span class="font-label-sm text-label-sm font-bold mb-1 text-[#333538]">'
                    '{label}</span><div class="aug-enum flex flex-col gap-1 bg-[#b5b8bb] p-0.5 rounded border border-[#8e9297]" '
                    'data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="px-2 py-0.5 text-[9px] font-label-sm font-bold rounded-[1px] text-[#444]">{opt}</button>',
            "toggle": '<div class="flex flex-col items-center"><span class="font-label-sm text-label-sm font-bold mb-2 text-[#141416]">'
                      '{label}</span><button class="aug-toggle w-[60px] h-[48px] tactile-btn rounded-[3px] border border-[#484b50]" '
                      'data-param="{key}"></button></div>',
        },
        "augment": [
            {"sel": ".knob-bevel", "all": True, "unclass": "^-?rotate-|^transform$"},
            {"sel": "div:has(> span + button + button + button)", "nth": ["range_a", "range_b"]},
            {"sel": "button.tactile-btn", "nth": ["cycle_a", "trig_a", "cycle_b", "trig_b"]},
            {"sel": "div[class*='w-[44px]']", "attr": {"data-param": "audio"}},
        ],
        "map": {"RISE": ["rise_a", "rise_b"], "FALL": ["fall_a", "fall_b"], "SHAPE": ["shape_a", "shape_b"]},
        "grid": {"tabs": ["MIDI"], "tpl": None},
        "tab_order": ["RAMPAGE", "MIDI"],
    },
    "rings": {
        # one page, every control drawn
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "RINGS", "drop": ["header", "footer"],
        "knob": {"item": ".knob-radial", "unit": "div.flex-col:has(> div > .knob-radial)", "label": "span"},
        "popup": {"item": "div.w-full.h-11, div[class*='w-[134px]']", "label": "label, span.uppercase"},
        "enum": {"item": "div.grid.grid-cols-3:has(> button)", "opt": "button", "label": "span"},
        # (no MODEL picture: 3 of the 7 models record silence in the probe, so the pictures would mislead)
        "augment": [
            {"sel": "body", "retext": [["AKAI PRO ENGINE", "OPEN SOURCE DSP"]]},   # no maker badges / wrong routing text in the art (2026-10-03)
            {"sel": "svg.-rotate-90 circle:nth-child(2)", "all": True, "remove": True},   # value arcs at one value
            {"sel": "div.grid.grid-cols-2:has(> div > span + span.rounded-full)", "remove": True},   # fake model quick-picks
            {"sel": "div.w-full.h-11", "attr": {"data-param": "model"}},
            {"sel": "div[class*='w-[134px]']", "attr": {"data-param": "synth_fx"}},
            {"sel": "div.grid.grid-cols-3:has(> button)", "attr": {"data-param": "polyphony"}},
        ],
    },
    "ringsfx": {
        # one page, every control drawn
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "RINGS FX", "drop": ["header", "footer"],
        "knob": {"item": ".knob-shadow", "unit": "div.flex-col:has(> div > .knob-shadow)", "label": "span",
                 "value": "div.text-center"},   # its name + readout box under the knob: MPC draws those
        "popup": {"item": "div[data-param=model]"},
        "enum": {"item": "div[data-param=polyphony]", "opt": "button", "label": "span"},
        "augment": [
            {"sel": "svg.-rotate-90 circle:nth-child(2)", "all": True, "remove": True},   # value arcs at one value
            {"sel": ".knob-shadow span", "all": True, "remove": True},   # captions on the caps would turn with them
            {"sel": "div.cursor-pointer:has(> div > span.block)", "attr": {"data-param": "model"}},
            {"sel": "div.flex-col:has(> button > span + span.rounded-full)", "attr": {"data-param": "polyphony"}},
        ],
        "map": {"Q03: STRUCT": "structure", "Q04: BRIGHT": "brightness", "Q05: DAMP": "damping", "Q06: POS": "position",
                "Q07: NOTE": "note", "Q08: FINE": "fine", "Q09: INPUT": "input_gain", "Q10: MIX": "mix", "Q11: WIDTH": "width",
                "Q12: VOL": "volume"},
        "design_labels": False,   # its first labels are Q-Link hints
    },
    "superarp": {
        # MAIN from the design; PATTERN and MODIFY drawn in its style
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "MAIN", "drop": ["header", "footer"],
        "knob": {"item": ".dial-knob", "unit": "div.flex-col:has(> div > .dial-knob)", "label": "span"},
        "enum": {"item": "div[data-param=sync], div[data-param=rate], .aug-enum", "opt": "button", "label": "span"},
        "toggle": {"item": "button[data-param=triplet], button[data-param=latch], .aug-toggle", "label": "span"},
        "popup": {"item": "div[data-param=octave_range], .aug-popup", "label": "span"},
        "stepper": {"item": ".aug-stepper"},
        "tpl": {
            "frame": '<section class="absolute bg-[#18121e] border border-[#3f3150] rounded p-3 flex flex-col chassis-screws '
                     'shadow-[0_2px_4px_rgba(0,0,0,0.6)]" style="left:{x}px; top:{y}px; width:{w}px; height:{h}px;"><div class="flex '
                     'items-center justify-between border-b border-[#3f3150] pb-1.5"><div class="flex items-center space-x-2"><div '
                     'class="w-1.5 h-3 bg-[#a764e8] rounded-sm"></div><span class="text-label-md font-label-md text-[#f6effc] '
                     'font-bold tracking-wider">{title}</span></div></div></section>',
            "knob": '<div class="flex flex-col items-center justify-center"><span class="text-label-sm font-label-sm text-[#d5c7e3] '
                    'mb-1">{label}</span><div class="relative w-[76px] h-[76px] flex items-center justify-center"><svg '
                    'class="absolute inset-0 w-full h-full -rotate-90"><circle cx="38" cy="38" fill="none" r="33" stroke="#251a30" '
                    'stroke-width="4"></circle></svg><div class="dial-knob w-14 h-14 rounded-full flex items-center justify-center '
                    'relative border border-[#3f3150]" data-param="{key}"><div class="w-1 h-4 bg-[#c993ff] rounded-full absolute '
                    '-top-0.5 shadow-[0_0_6px_#c993ff]"></div></div></div></div>',
            "enum": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-[#d5c7e3] mb-1.5">{label}'
                    '</span><div class="aug-enum bg-[#0d0716] border border-[#3f3150] rounded p-1 flex flex-col space-y-1" '
                    'data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="w-full py-1 px-2 text-label-sm font-label-sm rounded text-[#d5c7e3]">{opt}</button>',
            "toggle": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-[#d5c7e3] mb-1.5">{label}'
                      '</span><button class="aug-toggle w-16 h-[50px] rounded bg-[#1e1329] border border-[#3f3150]" '
                      'data-param="{key}"></button></div>',
            "popup": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-[#d5c7e3] mb-1.5">{label}'
                     '</span><div class="aug-popup rounded bg-[#0d0716] border border-[#3f3150]" style="width:{w}px; height:48px;" '
                     'data-param="{key}"></div></div>',
            "stepper": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-[#d5c7e3] mb-1.5">{label}'
                       '</span><div class="aug-stepper rounded bg-[#0d0716] border border-[#3f3150]" style="width:{w}px; height:56px;" '
                       'data-param="{key}"></div></div>',
        },
        "augment": [
            {"sel": "svg.-rotate-90 circle:nth-child(2)", "all": True, "remove": True},
            {"sel": "div.w-full.flex.flex-col.space-y-1:has(> button)", "nth": ["sync", "rate"]},
            {"sel": "button[class*='h-[50px]']", "nth": ["triplet", "latch"]},
            {"sel": "div[class*='w-[134px]']", "attr": {"data-param": "octave_range"}},
            {"sel": "section.chassis-screws", "all": True, "attr": {"style": "overflow: hidden;"}},   # its event stream spills
        ],
        "map": {"TEMPO (BPM)": "bpm"},
        "grid": {"tabs": ["PATTERN", "MODIFY"], "tpl": None},
        "tab_order": ["MAIN", "PATTERN", "MODIFY"],
    },
    "tablor": {
        # MAIN from the design; FILTER / SHAPE / ENVELOPES / MOD ENVS / VOICE drawn in its style
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "MAIN", "drop": ["header", "footer"],
        "knob": {"item": ".knob-cap", "unit": "div.flex-col:has(> div > .knob-cap)", "label": "span",
                 "value": ":scope > div:last-child"},
        "stepper": {"item": "div[class*='h-[42px]'], .aug-stepper"},
        "get": {"wt1_select": "wt1_name", "wt2_select": "wt2_name"},
        "enum": {"item": ".aug-enum", "opt": "button", "label": "span"},
        "popup": {"item": ".aug-popup", "label": "span"},
        "toggle": {"item": ".aug-toggle", "label": "span"},
        "tpl": {
            "frame": '<section class="module-card absolute rounded p-2 flex flex-col" style="left:{x}px; top:{y}px; width:{w}px; '
                     'height:{h}px;"><div class="flex items-center justify-between border-b border-[#1b433f] pb-1 px-3"><div '
                     'class="flex items-center space-x-2"><div class="w-2.5 h-2.5 bg-[#5ff0cf] rounded-sm '
                     'shadow-[0_0_8px_#5ff0cf]"></div><h2 class="text-label-lg font-label-lg tracking-wider text-[#eafffb] '
                     'font-bold">{title}</h2></div></div></section>',
            "knob": '<div class="flex flex-col items-center justify-center"><span class="text-label-sm font-label-sm text-[#bfe9e2] '
                    'tracking-wider mb-1">{label}</span><div class="relative w-[70px] h-[70px] flex items-center justify-center">'
                    '<svg class="absolute inset-0 w-full h-full -rotate-90 pointer-events-none" viewbox="0 0 100 100"><circle '
                    'cx="50" cy="50" fill="none" r="42" stroke="#08221f" stroke-width="7"></circle></svg><div class="knob-cap '
                    'w-[52px] h-[52px] rounded-full border border-[#21504b] flex items-center justify-center relative" '
                    'data-param="{key}"><div class="w-1.5 h-4 bg-[#5ff0cf] absolute top-1 rounded-sm shadow-[0_0_6px_#5ff0cf]">'
                    '</div><div class="w-3 h-3 rounded-full bg-[#05100f] border border-[#21504b]"></div></div></div></div>',
            "enum": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-[#bfe9e2] mb-1">{label}'
                    '</span><div class="aug-enum flex flex-col gap-1" data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="px-3 py-1 bg-[#09221f] text-[#5ff0cf] border border-[#21504b] text-label-sm rounded">{opt}'
                        '</button>',
            "popup": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-[#bfe9e2] mb-1">{label}'
                     '</span><div class="aug-popup recessed-display rounded" style="width:{w}px; height:44px;" data-param="{key}">'
                     '</div></div>',
            "toggle": '<div class="flex flex-col items-center"><span class="text-label-sm font-label-sm text-[#bfe9e2] mb-1">{label}'
                      '</span><button class="aug-toggle h-8 px-4 bg-[#09221f] border border-[#21504b] rounded" data-param="{key}">'
                      '</button></div>',
            "stepper": '<div class="aug-stepper recessed-display rounded" style="width:{w}px; height:56px;" data-param="{key}"></div>',
        },
        "augment": [
            {"sel": "svg.-rotate-90 circle:nth-child(2)", "all": True, "remove": True},
            {"sel": "div[class*='h-[42px]']", "nth": ["wt1_select", "wt2_select"]},
        ],
        "grid": {"tabs": ["FILTER", "SHAPE", "ENVELOPES", "MOD ENVS", "VOICE"], "tpl": None},
        "tab_order": ["MAIN", "FILTER", "SHAPE", "ENVELOPES", "MOD ENVS", "VOICE"],
    },
    "verglas": {
        # VERGLAS from the design (all 14 controls); TONE drawn in its style
        "sweep": 135, "design_labels": True, "tabs": "#tab-verglas", "drop": ["header", "footer"],
        "knob": {"item": ".knob-body", "unit": ".knob-container", "label": "div.uppercase", "value": "div:has(> .knob-val)"},
        "enum": {"item": "div[data-param=mode], div[data-param=quality]", "opt": "button", "label": "span"},
        "toggle": {"item": "#freeze-toggle-btn, .aug-toggle", "label": "span"},
        "tpl": {
            "frame": '<div class="milled-panel rounded flex flex-col p-2.5 absolute" style="left:{x}px; top:{y}px; width:{w}px; '
                     'height:{h}px;"><div class="flex items-center justify-between border-b border-outline-variant/60 pb-1 px-1">'
                     '<div class="flex items-center gap-2"><span class="w-2 h-2 bg-primary rounded-sm shadow-[0_0_5px_#6fb6ff]">'
                     '</span><h2 class="text-headline-sm font-headline-sm text-on-surface tracking-wider">{title}</h2></div></div>'
                     '</div>',
            "knob": '<div class="knob-container" data-knob="{key}"><div class="text-label-md font-label-md text-on-surface-variant '
                    'uppercase tracking-wider mb-1">{label}</div><div class="w-[52px] h-[52px] knob-body"><svg '
                    'class="knob-dial-ring" viewBox="0 0 62 62"><circle cx="31" cy="31" fill="none" r="27" stroke="#1f2a36" '
                    'stroke-dasharray="130 180" stroke-linecap="round" stroke-width="2.5" transform="rotate(135 31 31)"></circle>'
                    '</svg><div class="knob-cap-groove"></div><div class="knob-pointer" style="transform: rotate(0deg);"></div></div>'
                    '<div class="mt-1.5 px-2 py-0.5 rounded bg-surface-container-lowest border border-outline-variant text-label-sm '
                    'font-label-sm text-primary font-bold"><span class="knob-val">0</span></div></div>',
            "toggle": '<div class="flex flex-col items-center"><span class="text-label-md font-label-md text-on-surface-variant mb-1">'
                      '{label}</span><button class="aug-toggle tactile-btn py-2.5 px-6 rounded border border-outline-variant" '
                      'data-param="{key}"></button></div>',
        },
        "augment": [
            {"sel": ".knob-arc-active", "all": True, "remove": True},
            {"sel": "div:has(> .mode-btn)", "attr": {"data-param": "mode"}},
            {"sel": "div:has(> .quality-btn)", "attr": {"data-param": "quality"}},
            {"sel": "#freeze-toggle-btn", "attr": {"data-param": "freeze"}},
            {"sel": "div:has(> #particle-cloud-canvas)", "attr": {"style": "height: 100px; flex: none;"}},   # knobs up: room for values
        ],
        "grid": {"tabs": ["TONE"], "tpl": None},
        "tab_order": ["VERGLAS", "TONE"],
    },
    "warps": {
        # one page, every control drawn; the caps turn (its needles pivot off-centre)
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "WARPS", "rot": ".knob-shadow > div",
        "drop": ["header", "footer"],
        "knob": {"item": ".knob-shadow", "unit": "div.flex-col:has(> div > .knob-shadow), div.flex-col:has(> .knob-shadow)",
                 "label": "span", "value": "div:has(> span[id^=txt-]), div.px-2"},
        "enum": {"item": "div:has(> #btn-mode-meta), #carrier-selector, #output-selector", "opt": "button", "label": "span"},
        "augment": [
            {"sel": "[id^=needle-]", "all": True, "attr": {"style": ""}},
            {"sel": "circle[stroke-dashoffset]", "all": True, "remove": True},   # value arcs at one value
            {"sel": ".knob-shadow span", "all": True, "remove": True},
            {"sel": "div:has(> #btn-mode-meta)", "attr": {"data-param": "mode"}},
            {"sel": "#carrier-selector", "attr": {"data-param": "carrier"}},
            {"sel": "#output-selector", "attr": {"data-param": "output"}},
        ],
        "option_labels": {"mode": ["META MOD", "FREQ"]},
        "nowrap": ["carrier"], "nudge": {"carrier": [-40, 0], "output": [-50, 0]},
        "map": {"needle-lvl2": "level_2"},
        "names": {"level_2": "MOD LEVEL"},   # (its first caption is a "SIG" light)
    },
    "wurl": {
        # one page, every control drawn (its other tab buttons lead nowhere)
        "sweep": 135, "design_labels": True, "tabs": "#no-tabs", "page": "WURL", "drop": ["header", "footer"],
        "knob": {"item": ".knob-assembly", "body": ".knob-body", "unit": "div.flex-col:has(> .knob-assembly)", "label": "span",
                 "value": "[id^=val-]"},
        "popup": {"item": "#presetSelectorTrigger", "key": "preset"},
    },
    "monovoice": dict(MONO_SPECS, **{
        # the design's MACHINE side (machine + LFO destinations); its Helm knobs go; 7 pages drawn in its style
        "page": "MACHINE",
        "augment": MONO_COMMON + [
            {"sel": "[data-purpose=rotary-knob-cluster-osc]", "remove": True},
            {"sel": "[data-purpose=sub-noise-lfo-matrix] .col-span-6:has(.rotary-group)", "remove": True},
            {"sel": "div:has(> span + div > .wave-btn)", "remove": True},
            {"sel": "div.mt-3.flex-1:has(> div > p)", "remove": True},   # a card about one machine
            {"sel": "[data-purpose=sub-noise-lfo-matrix] .col-span-6", "attr": {"class": "col-span-12 flex flex-col justify-center h-full"}},
            {"sel": "[data-purpose=sub-noise-lfo-matrix] > div:first-child > span", "text": "LFO DESTINATIONS"},
            {"sel": "[data-purpose=osc-mix-section] > div:first-child > span", "text": "MONO VOICE // DIGITAL MACHINES"},
            {"sel": "[data-purpose=sub-noise-lfo-matrix] span.text-amber-500", "text": "MONOMACHINE LFO 1-3"},
            {"sel": "[data-purpose=lfo-dest-selectors]", "attr": {"class": "grid grid-cols-3 gap-6 mt-2"}},
            {"sel": "[data-purpose=lfo-dest-selectors] .lcd-screen-pattern", "all": True, "attr": {"style": "height: 40px;"}},
            {"sel": "[data-purpose=sub-noise-lfo-matrix] span.text-neutral-500:not(:only-child)", "remove": True},
        ],
        "map": {"machine-trigger": "machine", "lfo1": "lfo1_1", "lfo2": "lfo2_1", "lfo3": "lfo3_1"},
        "grid": {"tabs": ["SYNTH", "AMP", "FILTER", "EFFECT", "LFO 1", "LFO 2", "LFO 3"], "tpl": MONO_TPL, "keep": ".crt-scanlines"},
        "tab_order": ["MACHINE", "SYNTH", "AMP", "FILTER", "EFFECT", "LFO 1", "LFO 2", "LFO 3"],
    }),
    "helm": dict(MONO_SPECS, **{
        # the design's OSC MIX / SUB side is Helm's; its Mono Voice parts go; 11 pages drawn in its style
        "page": "OSC MIX",
        "augment": MONO_COMMON + [
            {"sel": "[data-purpose=machine-engine-module]", "remove": True},
            {"sel": "[data-purpose=lfo-dest-selectors]", "remove": True},
            {"sel": "div:has(> .wave-btn)", "where": "replace", "html": "@popup_inline(sub_waveform)"},
            {"sel": "span:has(+ .amber-glow-text)", "text": "Hel"}, {"sel": ".amber-glow-text", "text": "m"},
            {"sel": "div:has(> div > .amber-glow-text) > div:last-child", "text": "POLYPHONIC SYNTHESIZER"},
            {"sel": "[data-purpose=sub-noise-lfo-matrix] > div:first-child > span", "text": "SUB OSC / NOISE"},
            {"sel": "[data-purpose=sub-noise-lfo-matrix] span.text-amber-500", "text": "HELM SUB OSCILLATOR"},
            {"sel": "[data-purpose=sub-noise-lfo-matrix] span.text-neutral-500:not(:only-child)", "remove": True},
        ],
        "map": {"crossMod": "cross_modulation", "fbkAmt": "osc_feedback_amount", "fbkTran": "osc_feedback_transpose",
                "fbkTune": "osc_feedback_tune", "oscMix": "osc_mix", "noiseVol": "noise_volume", "subOct": "sub_octave",
                "subShuf": "sub_shuffle", "subVol": "sub_volume"},
        "grid": {"tabs": ["MAIN", "OSC", "FILTER", "MOD ENV", "MONO LFO", "STEP SEQ", "STEPS", "DISTORTION", "REVERB",
                          "PLAYING", "MOD AMOUNTS"], "tpl": MONO_TPL, "keep": ".crt-scanlines"},
        "tab_order": ["MAIN", "OSC", "OSC MIX", "FILTER", "MOD ENV", "MONO LFO", "STEP SEQ", "STEPS", "DISTORTION", "REVERB",
                      "PLAYING", "MOD AMOUNTS"],
    }),
    "hank": {
        # one page; the scope shows the FM RATIO's waveform from the engine, the ADS display follows its knobs
        "sweep": 135, "design_labels": True, "page": "HANK",
        "knob": {"item": ".knob-container", "body": ".knob-knurling", "unit": "div:has(> .knob-container)", "label": "span",
                 "value": ".val-display"},
        "stepper": {"item": ".recessed-well:has(> #patch-prev)", "key": "preset"},
        "get": {"preset": "preset_name"},
        "popup": {"item": "#ratio-group", "key": "ratio"},
        "hide": ["div:has(> span > #scope-ratio-tag)", ".recessed-well:has(> #envCanvas) > div"],
        "canvas": {"#scopeCanvas": {"picture": "ratio", "fill": ".recessed-well:has(> #scopeCanvas)"},
                   "#envCanvas": {"env": ["attack", "decay", "sustain"], "name": "mod", "fill": ".recessed-well:has(> #envCanvas)"}},
        "drop": ["header", "footer"],
    },
    "groovebank": {
        # one page; the MIDI monitor / chord readouts stay as the design's decoration, a dead button goes
        "sweep": 135, "design_labels": True,
        "knob": {"item": ".collet-knob-casing", "unit": "div[onmousedown^=startKnobDrag]", "label": "span",
                 "value": "span.font-readout-numeric"},
        "toggle": {"item": "div:has(> #latch-indicator)", "label": "span"},
        "stepper": {"item": "div:has(> button[onclick^=changePattern])", "key": "pattern"},
        "get": {"pattern": "pattern_label"},
        "drop": ["header", "footer"], "page": "MAIN",
        "hide": ["div:has(> #groove-feel-tag)"],
        "augment": [{"sel": "div:has(> div.flex-col > span + span.font-readout-numeric) > button", "remove": True}],   # BURST TEST
    },
    "grids": {
        # drum-note steppers and the channel readout become the design's knobs (a stepper needs prev/next parameters)
        "sweep": 135, "design_labels": True, "tabs": "[id^=tab-btn]",
        "knob": {"item": ".knob-ring", "body": ".knob-body", "unit": ".knob-container:has(> span), div.flex-col:has(> span + div .knob-container)",
                 "label": "span", "value": ".recessed-display"},
        "enum": {"item": "div:has(> #mode-drums), div:has(> #sw-off), #page-notes div.grid:has(> button)", "opt": "button",
                 "label": "span"},
        "slider": {"item": "input[type=range]", "label": "span.font-bold", "value": "span.font-readout-numeric"},
        "hide": ["#topoCanvas + div", ".w-16.recessed-display > span:last-child"],   # X/Y and note numbers frozen at one value
        "tpl": {
            "knob": '<div class="flex flex-col items-center knob-container" data-param="{key}"><span class="text-label-sm '
                    'font-label-sm text-on-surface-variant mb-1 font-bold">{label}</span><div class="knob-ring w-[50px] h-[50px] '
                    'p-[2px] flex items-center justify-center"><div class="knob-body w-[42px] h-[42px] pointer-events-none">'
                    '<div class="knob-dot" style="transform: rotate(0deg); transform-origin: 2.5px 16.5px;"></div></div></div>'
                    '<div class="recessed-display px-2 py-0.5 mt-2 rounded border border-outline-variant w-14 text-center">'
                    '<span class="text-label-sm font-readout-numeric text-primary">0</span></div></div>',
        },
        "augment": [
            {"sel": "body", "retext": [["Notes transmit triggers to external Akai MPC Drum Programs or Synthesizers.", "Notes go out of the Grids MIDI port: set a drum track's MIDI input to it."], ["Routed automatically to internal MPC pads and rear MIDI Out 1.", "Its own MIDI port (Preferences > MIDI: Track on), clocked by MPC."]]},   # no maker badges / wrong routing text in the art (2026-10-03)
            {"sel": "div:has(> #mode-drums)", "attr": {"data-param": "mode"}},
            {"sel": "div:has(> #sw-off)", "attr": {"data-param": "swing"}},
            {"sel": "#page-notes div.grid:has(> button)", "attr": {"data-param": "resolution", "class": "grid grid-cols-2 gap-2 mt-1"}},
            {"sel": "div:has(> div > #sw-off) + .recessed-display", "remove": True},   # PPQN readout
            {"sel": "div:has(> button[onclick^=changeNote])", "each": ["@knob(bd_note)", "@knob(sd_note)", "@knob(hh_note)"]},
            {"sel": "div.recessed-display:has(button[onclick^=changeNote]), div.recessed-display:has(> div > .knob-container)",
             "all": True, "attr": {"style": "min-height: 112px; justify-content: space-evenly;"}},
            {"sel": "#page-notes div.flex:has(> span.font-readout-numeric.text-label-lg)", "where": "replace", "html": "@knob(channel)"},
            {"sel": "#page-notes div.recessed-display.p-3:has(> span.font-readout-numeric)", "remove": True},   # RANDOM HUMANIZE
            # velocity sliders -> the design's knobs: a long horizontal slider's touch box is as tall as it is wide, so
            # the two (70 px apart) overlapped entirely
            {"sel": "div.space-y-6:has(> div > input[type=range])", "where": "replace",
             "html": '<div class="flex justify-around items-center my-auto">@knob(accent_vel)@knob(normal_vel)</div>'},
            {"sel": "#page-notes div.recessed-display:has(> div > span.block):has(> div.rounded-full)", "remove": True},   # MIDI clock
        ],
        "map": {"mapX": "map_x", "mapY": "map_y", "bdFill": "bd_fill", "sdFill": "sd_fill", "hhFill": "hh_fill",
                "bdLen": "len_bd", "sdLen": "len_sd", "hhLen": "len_hh", "ACCENT VELOCITY": "accent_vel",
                "NORMAL VELOCITY": "normal_vel"},
    },
    "fizzik": {
        # every control is drawn; the resonator scope shows MODEL A's waveform from the engine
        "sweep": 135, "design_labels": True,
        "knob": {"item": ".knob-wrap", "body": ".knob-body", "unit": ".control-item", "label": ".control-label",
                 "value": ".val-badge"},
        "enum": {"item": ".enum-group", "opt": ".enum-opt", "unit": ".control-item", "label": ".control-label"},
        "popup": {"item": ".popup-box", "unit": ".control-item", "label": ".control-label"},
        "button": {"item": ".hw-button", "selfLabel": True},
        "hide": [".scope-legend"],
        "canvas": {"#canvasMain": {"picture": "a_model", "fill": ".resonator-scope"}},
        "augment": [{"sel": "button[onclick='randomizeAll()']", "attr": {"style": "margin-right: 44px;"}}],   # MPC's buttons are wider
        "map": {"presetPopup": "preset", "SHAPE": ["lfo1_shape", "lfo2_shape"], "TARGET": ["lfo1_target", "lfo2_target"],
                "RANDOM ALL": "rnd_patch", "RND EXCITER": "rnd_exc", "RND RESON": "rnd_reson"},
    },
    "eucalypso": {
        # LANE 2-4 weren't drawn: copies of LANE 1 with the keys and titles swapped
        "sweep": 135, "design_labels": True,
        "knob": {"item": ".knob-wrap", "body": ".knob-body", "unit": ".control-item", "label": ".control-label",
                 "value": ".val-badge"},
        "enum": {"item": ".enum-group", "opt": ".enum-opt", "unit": ".control-item", "label": ".control-label"},
        "popup": {"item": ".popup-box", "unit": ".control-item", "label": ".control-label"},
        "toggle": {"item": ".hw-toggle", "selfLabel": True},
        "augment": [
            {"sel": "#tab1 .knob-wrap", "nth": ["lane1_" + k for k in ['steps', 'pulses', 'rotation', 'drop', 'drop_seed', 'velocity', 'gate', 'note', 'n_rnd', 'n_seed', 'octave', 'oct_rnd', 'oct_seed']]},
            {"sel": "#tab1 .hw-toggle", "attr": {"data-param": "lane1_enabled"}},
            {"sel": "#tab1 .popup-box", "attr": {"data-param": "lane1_oct_rng"}},
            {"sel": "#tab1", "clone": [{"replace": [["lane1_", "lane%d_" % n], ['id="tab1"', 'id="tab%d"' % n],
                                                    ["LANE 1 RHYTHM (KICK / LOW BASS)", "LANE %d RHYTHM" % n],
                                                    ["LANE 1", "LANE %d" % n], ["L1 ON", "L%d ON" % n]]}
                                       for n in (4, 3, 2)]},
        ],
        # design labels on the lane pages would give four parameters one name: keep "L1 STEPS" etc. there
        "names": {},
    },
    "elements": {
        # same design family as Denis; the "real-time spectrum" box shows the resonator MODEL's waveform from the engine
        "sweep": 135, "design_labels": True,
        "knob": {"item": ".knob-body", "unit": ".knob-control", "label": ".knob-label", "value": ".knob-val"},
        "enum": {"item": ".enum-v", "opt": ".enum-opt", "label": ".enum-label"},
        "toggle": {"item": "#legatoBtn", "label": ".knob-label"},
        "canvas": {"#modalCanvas": {"picture": "model", "fill": ".vis-container"}},
        "augment": [
            {"sel": "#pagePlay .frame:has(#modalCanvas) .frame-title", "text": "RESONATOR MODEL // WAVEFORM"},
            {"sel": ".tab-page", "all": True, "attr": {"style": "position: relative; top: 30px;"}},
        ],
        "map": {"legatoBtn": "legato"},
        # design QA 2026-10-03: bank 1 = BOW | BLOW | STRIKE | RESONATOR knobs, bank 2 = CONTOUR | MODEL | SPACE (six panels
        # of 1 to 5 controls don't fit one bank of four columns); PLAY: PITCH | PERFORMANCE | MASTER
        "qlinks": {
            0: ["bow", "bow_timbre", "-", "-", "blow", "flow", "blow_timbre", "-", "strike", "mallet", "strike_timbre", "-",
                "geometry", "brightness", "damping", "position", "contour", "-", "-", "-", "model", "-", "-", "-",
                "space", "-", "-", "-"],
            1: ["octave", "fine", "bend_range", "-", "legato", "velocity", "signature", "-", "volume", "-", "-", "-"],
        },
    },
    "denis": {
        # 4 pages; the 2x8 matrix cells (no control in the design) become small knobs in its own knob style
        "sweep": 135, "design_labels": True,
        "knob": {"item": ".knob-body", "unit": ".knob-control", "label": ".knob-label", "value": ".knob-val"},
        "enum": {"item": ".enum-v", "opt": ".enum-opt", "label": ".enum-label"},
        "popup": {"item": ".preset-display", "unit": ".preset-stepper", "label": "span", "key": "preset"},
        "button": {"item": ".hw-btn[onclick], .aug-btn", "selfLabel": True},
        "toggle": {"item": ".legato-btn", "label": ".knob-label"},
        "canvas": {"#denisScope": {"env": ["attack", "decay", "sustain", "release"], "name": "amp", "fill": ".scope-box"}},
        "tpl": {
            "knob_s": '<div class="knob-control" data-key="{key}"><div class="knob-body" style="width:56px;height:56px">'
                      '<div class="knob-pointer" style="height:16px;top:5px"></div></div><div class="knob-label">{label}</div>'
                      '<div class="knob-val">0</div></div>',
        },
        "augment": [
            {"sel": "#pageDenis button[onclick='randomizeMod()']", "remove": True},   # RANDOM MOD is on page 2
            {"sel": "#pageDenis button.hw-btn:not([onclick])", "attr": {"class": "hw-btn legato-btn", "data-param": "legato"}},
            {"sel": "#pageEnvMod button[onclick='randomizeMod()']", "where": "afterend",
             "html": '<button class="hw-btn aug-btn" style="width: 160px;" data-param="matrix_reset">RESET MATRIX</button>'},
            {"sel": "#pageEnvMod .frame:has(#denisScope) .frame-title", "text": "SLOPE GENERATOR // ENVELOPE SHAPE"},
            {"sel": "#pageEnvLfo .matrix-cell, #pageShNoise .matrix-cell", "all": True,
             "attr": {"class": "matrix-cell", "style": "height: 150px;"}},
            {"sel": "#pageEnvLfo .matrix-cell", "each": ["@knob_s(mat_%d_%d)" % (r, c) for r in (0, 1) for c in range(8)]},
            {"sel": "#pageShNoise .matrix-cell", "each": ["@knob_s(mat_%d_%d)" % (r, c) for r in (2, 3) for c in range(8)]},
            {"sel": ".tab-page", "all": True, "attr": {"style": "position: relative; top: 30px;"}},   # 548 px of frames, centred
        ],
        "map": {"RANDOM ALL": "rnd_patch", "RANDOM SOUND": "rnd_denis", "RANDOM MOD": "rnd_mod", "RESET MATRIX": "matrix_reset"},
        # design QA 2026-10-03: a Q-Link column per panel ("-" = an empty slot); preset, RANDOM buttons and the filter
        # type are touch only (no slot left in their groups); the matrix pages were already a half row per column
        "qlinks": {
            0: ["osc1_freq", "osc1_timbre", "-", "-", "osc2_pitch", "osc2_harmonics", "osc_mix", "-",
                "fold_depth", "fold_type", "filter_cutoff", "filter_q", "vel_to_filter", "portamento", "legato", "-"],
            1: ["attack", "decay", "sustain", "release", "noise_mix", "noise_type", "-", "-",
                "lfo_rate", "sh_rate", "mod_depth_env", "mod_depth_noise"],
        },
    },
    "chordism": {
        # 10 pages; ~55 of the 135 controls weren't in the design: added per page in its own markup (augment)
        "sweep": 140, "center": False, "rot": ".knob-body", "design_labels": True,
        "knob": {"item": ".knob-container", "label": "span.mt-1", "value": ".knob-val"},
        "enum": {"item": "div:has(> button.tune-btn), div:has(> button.filter-mode-btn), div:has(> button.slope-btn), "
                         "div:has(> button.vca-btn), .aug-enum", "opt": "button", "label": "label"},
        "popup": {"item": "select", "label": "label"},
        "toggle": {"item": "#arpToggleBtn, .aug-toggle", "label": "span"},
        "hide": ["#tab-page-0 .scope-grid > div"],   # chord notes written for one chord
        "canvas": {"#chordCanvas": {"picture": "chord_type", "fill": ".scope-grid"}},
        "tpl": {
            "knob": '<div class="flex flex-col items-center"><div class="knob-container w-14 h-14 flex items-center '
                    'justify-center" data-param="{key}"><div class="knob-body w-12 h-12 rounded-full relative">'
                    '<div class="knob-pointer"></div></div></div><span class="text-[10px] font-mono text-mpc-inkDim '
                    'mt-1">{label}</span><span class="text-[10px] font-mono text-mpc-accentHi knob-val">0</span></div>',
            "enum": '<div class="flex flex-col space-y-1"><label class="text-[10px] font-mono text-mpc-inkDim uppercase '
                    'text-center">{label}</label><div class="aug-enum flex flex-col gap-1 bg-mpc-lcd p-1 rounded border '
                    'border-mpc-line" data-param="{key}">{opts}</div></div>',
            "opt_enum": '<button class="py-1 px-2 text-[10px] font-mono font-bold rounded text-mpc-inkFaint">{opt}</button>',
            "select": '<div class="flex flex-col space-y-1"><label class="text-[10px] font-mono text-mpc-inkDim uppercase">'
                      '{label}</label><select data-param="{key}" class="w-36 bg-[#120817] border border-mpc-line rounded '
                      'text-xs font-mono text-mpc-accentHi p-2">{opts}</select></div>',
            "opt_select": '<option>{opt}</option>',
            "toggle": '<div class="flex flex-col items-center space-y-2"><span class="text-[11px] font-mono '
                      'text-mpc-inkDim uppercase">{label}</span><button data-param="{key}" class="aug-toggle w-24 py-2 '
                      'font-mono font-bold text-xs rounded bg-mpc-panel border border-mpc-line text-mpc-inkFaint">OFF'
                      '</button></div>',
            "row": '<div class="grid grid-cols-{n} gap-3 py-2 items-center justify-items-center">{items}</div>',
            "box": '<div class="bg-[#130919] p-3 rounded border border-mpc-line flex flex-col">'
                   '<span class="text-xs font-mono font-bold text-mpc-accentHi border-b border-mpc-line pb-1">{title}</span>',
        },
        "names": {"lm_lfo_rate": "LVL LFO RATE", "lm_lfo_depth": "LVL LFO DPTH", "lm_lfo_shape": "LVL LFO WAVE",
                  "lm_lfo_mode": "LVL LFO MODE", "pm_lfo_rate": "PAN LFO RATE", "pm_lfo_depth": "PAN LFO DPTH",
                  "pm_lfo_shape": "PAN LFO WAVE", "pm_lfo_mode": "PAN LFO MODE", "lfo_shape": "SHP LFO WAVE",
                  "lfo_rate": "SHP LFO RATE", "lfo_depth": "SHP LFO DPTH", "filter_lfo_rate": "FLT LFO RATE",
                  "filter_lfo_depth": "FLT LFO DPTH", "filter_lfo_spread": "FLT LFO SPRD", "lfo_phase_1": "LFO PHASE 1",
                  "lfo_phase_2": "LFO PHASE 2", "lfo_phase_3": "LFO PHASE 3", "lfo_phase_4": "LFO PHASE 4",
                  "vib_osc_enable": "VIB OSCS", "sweep_osc_enable": "SWEEP OSCS", "fenv_hard_reset": "FENV RESET",
                  "vca_hard_reset": "VCA RESET", "delay_tone_hi": "DLY TONE HI", "delay_tone_lo": "DLY TONE LO",
                  "delay_mod_rate": "DLY MOD RATE", "delay_mod_depth": "DLY MOD DPTH", "reverb_lowcut": "REV LOW CUT",
                  "reverb_mod_rate": "REV MOD RATE", "reverb_mod_depth": "REV MOD DPTH", "reverb_decay": "REV DECAY",
                  "reverb_damp": "REV DAMPING", "ctrl_to_cutoff": "CTRL>CUTOFF", "ctrl_to_morph": "CTRL>MORPH",
                  "ctrl_to_vib": "CTRL>VIBRATO", "ctrl_to_shape": "CTRL>SHAPE", "ctrl_to_fm": "CTRL>FM",
                  "arp_variation_interval": "ARP VAR INT", "fm_amount_1": "FM AMT 1", "fm_amount_2": "FM AMT 2",
                  "fm_amount_3": "FM AMT 3", "fm_amount_4": "FM AMT 4", "pan_morph_index": "PAN MORPH",
                  "pan_morph_intensity": "PAN MORPH IN"},
        "labels": {"lm_lfo_rate": "RATE", "lm_lfo_depth": "DEPTH", "lm_lfo_shape": "WAVE", "lm_lfo_mode": "MODE",
                   "pm_lfo_rate": "RATE", "pm_lfo_depth": "DEPTH", "pm_lfo_shape": "WAVE", "pm_lfo_mode": "MODE",
                   "lfo_shape": "WAVE", "lfo_rate": "RATE", "lfo_depth": "DEPTH",
                   "vib_osc_enable": "VIB OSCS", "sweep_osc_enable": "SWEEP OSCS", "fenv_hard_reset": "HARD RESET",
                   "vca_hard_reset": "VCA RESET", "quality_position": "LOFI POS", "delay_tone_hi": "TONE HI",
                   "delay_tone_lo": "TONE LO", "delay_mod_rate": "MOD RATE", "reverb_lowcut": "LOW CUT",
                   "reverb_mod_rate": "MOD RATE", "reverb_mod_depth": "MOD DEPTH", "arp_variation_interval": "VAR INTERVAL",
                   "arp_clock_division": "CLOCK DIV", "arp_direction": "DIRECTION", "ctrl_to_cutoff": "TO CUTOFF",
                   "ctrl_to_morph": "TO MORPH", "ctrl_to_vib": "TO VIBRATO", "ctrl_to_shape": "TO SHAPE",
                   "ctrl_to_fm": "TO FM", "ctrl_source": "SOURCE", "ctrl_cc": "CC", "filter_lfo_shape": "FLT LFO WAVE",
                   "filter_lfo_mode": "FLT LFO MODE", "fenv_mode": "ENV MODE", "pan_morph_index": "PAN MORPH",
                   "pan_morph_intensity": "PAN INT"},
        "augment": [
            # the design's own switches and pop-ups, tagged with their parameters
            {"sel": "#tab-page-0 div:has(> button.tune-btn)", "attr": {"data-param": "tuning_mode"}},
            {"sel": "#tab-page-0 div:has(> button.filter-mode-btn)", "attr": {"data-param": "filter_mode"}},
            {"sel": "#tab-page-0 div:has(> button.slope-btn)", "attr": {"data-param": "filter_slope"}},
            {"sel": "#tab-page-0 div:has(> button.vca-btn)", "attr": {"data-param": "vca_mode"}},
            {"sel": "#tab-page-1 select", "nth": ["wave_1", "wave_2", "wave_3", "wave_4"]},
            {"sel": "#tab-page-8 select", "nth": ["chord_pc_%d" % i for i in range(12)]},
            # OSCILLATORS (design QA 2026-10-03): each voice's SHAPE and LFO PHASE under its WAVE / MIX box, so a Q-Link
            # column is one voice; the global SHAPE and LFO MODE move to the SHAPE page
            {"sel": "#tab-page-1 .grid-cols-7", "each": ["".join("@knob(shape_%d)@knob(lfo_phase_%d)" % (v, v)
                                                                 for v in range(1, 5))]},
            {"sel": "#tab-page-1 .grid-cols-7", "attr": {"class": "grid grid-cols-8 gap-3 items-center py-2"}},
            # SHAPE: pan morph under the phases, per-voice FM amounts + position under the FM row
            {"sel": "#tab-page-2 > div:nth-child(1) > .bg-mpc-lcd", "where": "replace",
             "html": "@row(pan_morph_index, pan_morph_intensity)"},
            {"sel": "#tab-page-2 > div:nth-child(2) > .h-10", "where": "replace",
             "html": "@row(fm_amount_1, fm_amount_2, fm_amount_3, fm_amount_4, fm_position)"},
            # FILTER ENV
            {"sel": "#tab-page-3 > div:nth-child(1) > .bg-mpc-lcd", "where": "replace",
             "html": "@row(fenv_mode, fenv_hard_reset, quality_position)"},
            {"sel": "#tab-page-3 > div:nth-child(2) > .bg-mpc-lcd", "where": "replace",
             "html": "@row(filter_lfo_shape, filter_lfo_mode)<box>SHAPE LFO@row(lfo_shape, lfo_rate, lfo_depth)</div>"},
            # VIBRATO: VIB STRAY is a switch; the oscillator masks join the row; level/pan morph LFOs below
            {"sel": "#tab-page-4 div:has(> .knob-container[data-param=stray])", "where": "replace", "html": "@enum(vib_stray)"},
            {"sel": "#tab-page-4 .grid-cols-6", "html": "@knob(vib_osc_enable)@knob(sweep_osc_enable)"},
            {"sel": "#tab-page-4 .grid-cols-6", "attr": {"class": "grid grid-cols-8 gap-4 py-4 items-center"}},
            {"sel": "#tab-page-4 > .h-12", "where": "replace",
             "html": '<div class="grid grid-cols-2 gap-4"><box>LEVEL MORPH LFO@row(lm_lfo_rate, lm_lfo_depth, lm_lfo_shape, '
                     'lm_lfo_mode)</div><box>PAN MORPH LFO@row(pm_lfo_rate, pm_lfo_depth, pm_lfo_shape, pm_lfo_mode)</div></div>'},
            # TREMOLO
            {"sel": "#tab-page-5 > div:nth-child(1) > .bg-mpc-lcd", "where": "replace",
             "html": "@row(amp_lfo_shape, glide_legato, vca_hard_reset, vca_drone)"},
            # DELAY / REVERB: the made-up status bars make room
            {"sel": "#tab-page-6 > .h-10", "where": "replace",
             "html": "@row(delay_mode, delay_tone_hi, delay_tone_lo, delay_mod_rate)"},
            {"sel": "#tab-page-7 > .h-10", "where": "replace", "html": "@row(reverb_lowcut, reverb_mod_rate, reverb_mod_depth)"},
            # CHORD MAP: intervals and the CTRL source routing
            {"sel": "#tab-page-8 > div.p-2", "where": "replace",
             "html": '<div class="grid grid-cols-10 gap-3"><div class="col-span-3"><box>INTERVALS@row(interval_1, interval_2, '
                     'interval_3)</div></div><div class="col-span-7"><box>CONTROL@row(ctrl_source, ctrl_cc, ctrl_to_cutoff, '
                     'ctrl_to_morph, ctrl_to_vib, ctrl_to_shape, ctrl_to_fm)</div></div></div>'},
            # ARPEGGIATOR
            {"sel": "#tab-page-9 > .h-12", "where": "replace",
             "html": "@row(arp_hold, arp_direction, arp_variation_interval, arp_clock_sync, arp_clock_division)"},
            # SHAPE (design QA): its first panel holds the global SHAPE and LFO MODE with the pan morph pair
            {"sel": "#tab-page-2 > div:nth-child(1) > span", "text": "SHAPE & PAN"},
            {"sel": "#tab-page-2 > div:nth-child(1) > div.flex.justify-around", "each": ["@knob(shape)@enum(shape_lfo_mode)"]},
            # CHORD MAP (design QA): room between the two rows of note pop-ups, so their Q-Link outlines stay apart
            {"sel": "#tab-page-8 div.grid.grid-cols-6", "attr": {"style": "row-gap: 30px"}},
            # FILTER ENV (design QA): the envelope panel's rows packed at the top instead of spread apart
            {"sel": "#tab-page-3 > div:nth-child(1)", "attr": {"style": "justify-content: flex-start; gap: 28px"}},
        ],
        "map": {"cutoff": "filter_cutoff", "resonance": "filter_resonance", "fm_mod": "fm_modulator", "fm_amt": "fm_amount",
                "morph_idx": "morph_index", "morph_int": "morph_intensity", "lfo_ph_1": "lfo_phase_1",
                "lfo_ph_2": "lfo_phase_2", "lfo_ph_3": "lfo_phase_3", "lfo_ph_4": "lfo_phase_4",
                "env_a": "filter_env_attack", "env_d": "filter_env_decay", "env_amt": "filter_env_depth",
                "flt_lfo_rate": "filter_lfo_rate", "flt_lfo_depth": "filter_lfo_depth", "flt_lfo_sprd": "filter_lfo_spread",
                "sweep_amt": "sweep_amount", "trem_rate": "amp_lfo_rate", "trem_depth": "amp_lfo_depth",
                "glide": "glide_rate", "dly_mix": "delay_mix", "dly_time": "delay_time", "dly_fbk": "delay_feedback",
                "dly_tone": "delay_tone", "dly_mod": "delay_mod_depth", "rev_mix": "reverb_mix",
                "rev_decay": "reverb_decay", "rev_damp": "reverb_damp", "rev_shimmer": "reverb_shimmer",
                "rev_size": "reverb_size", "arp_steps": "arp_euclid_steps", "arp_beats": "arp_euclid_beats",
                "arp_var": "arp_variations", "arpToggleBtn": "arp_enabled"},
        # design QA 2026-10-03: a Q-Link column per panel or row ("-" = an empty slot); MAIN and VIBRATO were fine
        "qlinks": {
            1: [k % v for v in range(1, 5) for k in ("wave_%d", "mix_%d", "shape_%d", "lfo_phase_%d")],
            2: ["shape", "shape_lfo_mode", "pan_morph_index", "pan_morph_intensity", "fm_modulator", "fm_amount",
                "morph_index", "morph_intensity", "fm_amount_1", "fm_amount_2", "fm_amount_3", "fm_amount_4",
                "fm_position", "-", "-", "-"],
            3: ["filter_env_attack", "filter_env_decay", "filter_env_depth", "drive", "filter_lfo_rate", "filter_lfo_depth",
                "filter_lfo_spread", "filter_lfo_shape", "lfo_shape", "lfo_rate", "lfo_depth", "-",
                "fenv_mode", "fenv_hard_reset", "quality_position", "-"],
            5: ["amp_lfo_rate", "amp_lfo_depth", "glide_rate", "-", "amp_lfo_shape", "glide_legato", "vca_hard_reset",
                "vca_drone", "grind", "bit_shift", "decimator", "-"],
            6: ["delay_mix", "delay_time", "delay_feedback", "delay_tone", "delay_mode", "delay_tone_hi", "delay_tone_lo", "-",
                "delay_mod_depth", "delay_mod_rate", "-", "-"],
            7: ["reverb_mix", "reverb_decay", "reverb_damp", "reverb_shimmer", "reverb_size", "-", "-", "-",
                "reverb_lowcut", "reverb_mod_rate", "reverb_mod_depth", "-"],
            8: ["chord_pc_0", "chord_pc_1", "chord_pc_2", "chord_pc_3", "chord_pc_4", "chord_pc_5", "-", "-",
                "chord_pc_6", "chord_pc_7", "chord_pc_8", "chord_pc_9", "chord_pc_10", "chord_pc_11", "-", "-",
                "interval_1", "interval_2", "interval_3", "-", "ctrl_source", "ctrl_cc", "ctrl_to_cutoff", "ctrl_to_morph",
                "ctrl_to_vib", "ctrl_to_shape", "ctrl_to_fm", "-"],
            9: ["arp_euclid_steps", "arp_euclid_beats", "arp_tempo", "arp_variations", "arp_hold", "arp_direction",
                "arp_variation_interval", "-", "arp_clock_sync", "arp_clock_division", "-", "-", "arp_enabled", "-", "-", "-"],
        },
    },
}


for _m in MAPS.values():   # a "grid" without its own templates uses the design's (tpl)
    if _m.get("grid") and not _m["grid"].get("tpl"):
        _m["grid"]["tpl"] = _m["tpl"]


def norm(s):
    return re.sub(r"[^a-z0-9]+", "", str(s).lower())


def theme_of(html):
    return {m.group(1).replace("-", "_"): m.group(2).lstrip("#")
            for m in re.finditer(r"--theme-([a-z0-9-]+):\s*(#[0-9a-fA-F]{6})", html)}


def match(c, params, mp):
    byk = {p["key"]: p for p in params}
    nk = {norm(k): k for k in byk}
    nn = {}
    for p in params:
        nn.setdefault(norm(p.get("name", "")), p["key"])
    for cand in [c.get("label", "")] + c.get("keys", []):
        if cand in mp:
            v = mp[cand]
            if isinstance(v, list):   # the same label more than once: in page order
                return v.pop(0) if v else None
            return v
    for cand in c.get("keys", []):
        if cand in byk:
            return cand
        if norm(cand) in nk:
            return nk[norm(cand)]
    lab = norm(c.get("label", ""))
    if lab and lab in nn:
        return nn[lab]
    if lab and lab in nk:
        return nk[lab]
    return None


def q(s):
    return '"%s"' % str(s).replace('"', "'")


def ctl_kind(p):
    """How an added control shows a parameter: OFF/ON -> toggle, up to 4 options -> switch, more -> pop-up, else knob."""
    o = p.get("options")
    if not o:
        return "knob"
    if [str(x).upper() for x in o] == ["OFF", "ON"]:
        return "toggle"
    return "enum" if len(o) <= 4 else "select"


def expand(html, cfg, byk):
    """A design's missing controls, written in its own markup: @<kind>(key) for any template kind in cfg["tpl"] (knob,
       enum, select, toggle, or the design's own extras), @ctl(key) (picks one, see ctl_kind), @row(key, key, ...) (a grid
       row of @ctl) and <box>TITLE (a sub-panel), from the per-design templates cfg["tpl"]."""
    tpl, labels = cfg.get("tpl", {}), cfg.get("labels", {})   # no "tpl": markup with no @macros

    def one(kind, key):
        if key not in byk:
            sys.exit("augment: %r is not a parameter" % key)
        cfg.setdefault("_added", set()).add(key)
        p = byk[key]
        kind = ctl_kind(p) if kind == "ctl" else kind
        opts = "".join(tpl["opt_" + kind].replace("{opt}", str(o)) for o in p.get("options") or []) if "opt_" + kind in tpl else ""
        return tpl[kind].replace("{key}", key).replace("{label}", labels.get(key, p.get("name", key))).replace("{opts}", opts)

    html = re.sub(r"<box>([^@<]+)", lambda m: tpl["box"].replace("{title}", m.group(1).strip()), html)
    html = re.sub(r"@row\(([\w, ]+)\)", lambda m: tpl["row"].replace("{n}", str(len(m.group(1).split(",")))).replace(
        "{items}", "".join(one("ctl", k.strip()) for k in m.group(1).split(","))), html)
    return re.sub(r"@(\w+)\((\w+)\)", lambda m: one(m.group(1), m.group(2)) if m.group(1) in tpl or m.group(1) == "ctl"
                  else m.group(0), html)


GRID_KINDS = ("frame", "art", "knob", "enum_v", "enum_h", "popup", "toggle", "slider_v", "slider_h", "stepper", "button")


def grid_tabs(D):
    """The port's own page plan (layout.grid.conf, the generated layout a design replaced; else layout.conf): per tab,
       its frames and controls, as {name, items: [{kind, x, y, w, h, title | cx, cy, key, ...}]}."""
    f = next(os.path.join(D, n) for n in ("layout.grid.conf", "layout.conf") if os.path.exists(os.path.join(D, n)))
    tabs = []
    for line in open(f).read().splitlines():
        m = re.match(r"\[tab (.+)\]$", line.strip())
        if m:
            tabs.append({"name": m.group(1).strip(), "items": []})
        elif tabs and line.split(" ", 1)[0] in GRID_KINDS:
            a = {k: v.strip('"') for k, v in re.findall(r'(\w+)=("[^"]*"|\S+)', line)}
            a["kind"] = line.split(" ", 1)[0]
            tabs[-1]["items"].append(a)
    return tabs


def fit_boxes(lines, popts=None):
    """bw= on knobs and vertical sliders whose 130 px touch box (it carries the name and value) would overlap a
       neighbour's: MPC gives a touch in the overlap to one of them. Knob/slider neighbours split the gap; a switch,
       pop-up, toggle, stepper or button keeps its size. Lines of one page, in layout.conf form."""
    sys.path.insert(0, os.path.join(MV, "tools"))
    import shadow_skin
    ws = []
    for i, l in enumerate(lines):
        kind = l.split(" ", 1)[0]
        if kind not in ("knob", "slider_v", "slider_h", "toggle", "button", "enum_h", "enum_v", "popup", "stepper"):
            continue
        w = shadow_skin.parse_widget(l)
        if kind == "knob":
            s_ = 2 * w["r"] + 10
            box = (w["cx"] - 65, w["cy"] - s_ // 2, 130, s_ // 2 + w["r"] + 2 + 22 + 32)
        elif kind in ("slider_v", "slider_h"):
            sq = max(w["w"], w["h"])
            box = (w["cx"] - max(130, sq) // 2, w["cy"] - sq // 2, max(130, sq), (sq - w["h"]) // 2 + w["h"] + 2 + 22 + 32)
        elif kind == "toggle":
            box = (w["cx"] - 60, w["cy"] - 18, 120, 58)
        elif kind == "button":
            box = shadow_skin.button_rect(w)
        elif kind in ("enum_h", "enum_v"):
            if "options" not in w:   # the parameter's own options (the layout leaves them to the build)
                w["options"] = (popts or {}).get(w.get("key")) or ["X"] * 2
            rs = shadow_skin.seg_rects(w)
            x0, y0 = min(r[0] for r in rs), min(r[1] for r in rs)
            box = (x0, y0, max(r[0] + r[2] for r in rs) - x0, max(r[1] + r[3] for r in rs) - y0)
        else:
            box = (w["cx"] - w["w"] // 2, w["cy"] - w["h"] // 2, w["w"], w["h"])
        ws.append((i, kind, w, box))
    for i, kind, w, (x, y, bw, bh) in ws:
        if kind not in ("knob", "slider_v"):
            continue
        half = full = bw / 2   # a knob's box is 130 wide; a vertical slider's as wide as it is long
        for j, k2, w2, (x2, y2, bw2, bh2) in ws:
            if j == i or min(y + bh, y2 + bh2) - max(y, y2) <= 4:   # not side by side
                continue
            d = abs(w2.get("cx", x2 + bw2 // 2) - w["cx"])
            if k2 in ("knob", "slider_v", "slider_h"):
                half = min(half, d / 2)
            else:   # its box keeps its size: up to its near edge
                half = min(half, (x2 - w["cx"]) if x2 > w["cx"] else (w["cx"] - (x2 + bw2)))
        floor = 2 * w["r"] + 10 if kind == "knob" else w["w"] + 20
        if half < full:
            lines[i] += " bw=%d" % max(floor, int(2 * half) - 6)
    return lines


def fill(t, **kw):
    for k, v in kw.items():
        t = t.replace("{%s}" % k, str(v))
    return t


def grid_page(tab, cfg, byk):
    """A page the design didn't draw, drawn in its style: the plan's frames and controls at their places, each in the
       design's own markup (cfg["grid"]["tpl"]: frame, knob, enum (+ opt_enum), popup, stepper, toggle, button; a kind
       without a template becomes a knob). extract.py shows it in place of the design's pages and reads it like them."""
    tpl, labels = cfg["grid"]["tpl"], cfg.get("labels", {})
    kinds = {"enum_v": "enum", "enum_h": "enum", "slider_v": "knob", "slider_h": "knob"}
    out = []
    for it in tab["items"]:
        if it["kind"] in ("frame", "art"):   # art (a logo, a caption): the design's "art" plate if it has one, else nothing
            if it["kind"] in tpl:
                out.append(fill(tpl[it["kind"]], x=it["x"], y=int(it["y"]) - Y_OFF, w=it["w"], h=it["h"],
                                title=it.get("title", "")))
            continue
        p = byk.get(it.get("key"))
        if not p or p["key"] in cfg["grid"].get("skip", ()):   # skip: placed on a drawn page instead
            continue
        kind = kinds.get(it["kind"], it["kind"])
        kind = kind if kind in tpl else "knob"
        if it.get("get"):
            cfg.setdefault("get", {})[p["key"]] = it["get"]
        opts = p.get("options") or []
        html = fill(tpl[kind], key=p["key"], label=labels.get(p["key"], p.get("name", p["key"])), w=it.get("w", 150),
                    h=it.get("h", 48), first=opts[0] if opts else "",
                    opts="".join(fill(tpl.get("opt_" + kind, ""), opt=o) for o in opts))
        out.append('<div class="stitch-gc" style="position:absolute; left:%spx; top:%dpx; transform:translate(-50%%,-50%%);">%s</div>'
                   % (it["cx"], int(it["cy"]) - Y_OFF, html))
    return ('<div class="stitch-grid-page" style="position:absolute; left:0; top:0; width:1280px; height:628px; z-index:5;">%s'
            '</div>' % "".join(out))


def convert(port):
    cfg = dict(MAPS[port])
    D = os.path.join(STEVE, "schwung-ports", port)
    out = os.path.join(SL, "build", port)
    html = open(os.path.join(SL, port + ".md")).read()
    cfg.update(file=os.path.join(SL, port + ".md"), tabs=cfg.get("tabs", ".tab-btn"), canvas=cfg.get("canvas", {}))
    byk0 = {p["key"]: p for p in json.load(open(os.path.join(D, "params.json")))["params"]}
    cfg["augment"] = [dict(op, **({"html": expand(op["html"], cfg, byk0)} if "html" in op else {}),
                           **({"each": [expand(h, cfg, byk0) for h in op["each"]]} if "each" in op else {}))
                      for op in cfg.get("augment", [])]
    added = cfg.pop("_added", set())
    if cfg.get("grid"):   # pages the design didn't draw
        cfg["grid_pages"] = [{"name": t["name"], "html": grid_page(t, cfg, byk0)} for t in grid_tabs(D)
                             if t["name"] in cfg["grid"]["tabs"]]
        added |= {it["key"] for t in grid_tabs(D) if t["name"] in cfg["grid"]["tabs"] for it in t["items"] if it.get("key")}
        cfg["grid_keep"] = cfg["grid"].get("keep", "")
    os.makedirs(out, exist_ok=True)
    cj = os.path.join(out, "config.json")
    json.dump(cfg, open(cj, "w"), indent=1)
    r = subprocess.run(["docker", "run", "--rm", "-v", "%s:%s" % (DOCKER_ROOT, DOCKER_ROOT), "-w", MV, "mpc-vst-html-art", "python3",
                        os.path.join(HERE, "extract.py"), cj, out], capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode:
        sys.exit(r.stderr[-3000:])
    ctl = json.load(open(os.path.join(out, "controls.json")))
    if cfg.get("tab_order"):   # drawn and generated pages in the plan's order
        clean = lambda n: re.sub(r"^(?:TAB\s*)?\d+\s*[:.)\]]\s*", "", n.strip(" []()")).strip(" []()").upper()
        order = [clean(n) for n in cfg["tab_order"]]
        ctl["tabs"].sort(key=lambda t: order.index(clean(t["name"])) if clean(t["name"]) in order else len(order))
    params = json.load(open(os.path.join(D, "params.json")))["params"]
    pre = os.path.join(D, "params.pre-stitch.json")
    if os.path.exists(pre):   # renames start from the port's own names on every run
        orig = {p["key"]: p.get("name") for p in json.load(open(pre))["params"]}
        for p in params:
            p["name"] = orig.get(p["key"], p.get("name"))
    byk = {p["key"]: p for p in params}
    popts = {k: p["options"] for k, p in byk.items() if p.get("options")}
    mp = json.loads(json.dumps(cfg.get("map", {})))   # a copy: list entries are used up in page order
    # MPC writes a knob's, slider's or toggle's name from the PARAMETER's name (shadow_skin _name_label), not from the
    # layout, so a better name is a rename in params.json: cfg "names", or with "design_labels" the design's own label
    # (12 characters at most, and only while every name stays unique). The layout gets the final name.
    want = dict(cfg.get("names", {}))

    def lab(k, p, c=None):
        if cfg.get("design_labels") and c and c.get("label") and k not in added and k not in want:
            n = re.sub(r"\s+", " ", re.sub(r"\[[^\]]*\]", "", c["label"])).strip().rstrip(":").strip().upper()   # no "[Q1]" hints
            if 3 <= len(n) <= 12:   # a bare "A" or "S" says too little out of context
                want[k] = n
        return "\x00%s\x00" % k

    theme_now = theme_of(html) or dict(re.findall(r"^theme_(\w+)=(\w+)", open(os.path.join(D, "layout.conf")).read(), re.M))
    envs = {}   # canvas selector -> (column width) for envelope displays, drawn in the container
    for tab in ctl["tabs"]:
        for c in tab["canvases"]:
            sp = cfg["canvas"].get(c["sel"], {})
            if sp.get("env") and c["sel"] not in envs:
                x, y, w, h = c["rect"]
                es = {"out": out, "name": sp["name"], "w": round(w), "h": round(h), "frames": 32, "bands": 11,
                      "stages": len(sp["env"]),
                      "color": "#" + theme_now.get("accent_hi", theme_now.get("accent", "ffb85c"))}
                ej = os.path.join(out, "env_%s.json" % sp["name"])
                json.dump(es, open(ej, "w"))
                r = subprocess.run(["docker", "run", "--rm", "-v", "%s:%s" % (DOCKER_ROOT, DOCKER_ROOT), "-w", MV, "mpc-vst-html-art",
                                    "python3", os.path.join(HERE, "envelope.py"), ej], capture_output=True, text=True)
                if r.returncode:
                    sys.exit(r.stderr[-2000:])
                envs[c["sel"]] = int(r.stdout.split()[-1])
    img = os.path.join(D, "images", "stitch")
    os.makedirs(img, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".png"):
            shutil.copy(os.path.join(out, f), os.path.join(img, f))

    old = open(os.path.join(D, "layout.conf")).read()
    if not os.path.exists(os.path.join(D, "layout.grid.conf")):
        shutil.copy(os.path.join(D, "layout.conf"), os.path.join(D, "layout.grid.conf"))
    src = open(os.path.join(D, "layout.grid.conf")).read()
    keep = [l for l in src.splitlines() if re.match(r"(art_css|toggle_img|toggle_img_on)=", l)]
    theme = theme_of(html) or dict(re.findall(r"^theme_(\w+)=(\w+)", src, re.M))
    L = ["# %s for MPC, from the Stitch mockup steve/stitch_layouts/%s.md (steve/tools/stitch/convert.py, 2026-10-02)."
         % (port, port),
         "# Background = the mockup with its live parts hidden; knobs use filmstrips rendered from its own knobs.",
         "# The previous (grid) layout is layout.grid.conf."]
    L += ["theme_%s=%s" % kv for kv in sorted(theme.items())] + keep
    placed, missing = set(), []
    for ti, tab in enumerate(ctl["tabs"]):
        name = re.sub(r"^(?:TAB\s*)?\d+\s*[:.)\]]\s*", "", tab["name"].strip(" []()")).strip(" []()").upper() or "PAGE %d" % (ti + 1)
        L += ["", "[tab %s]" % name, "art file=images/stitch/%s x=0 y=%d w=1280 h=628" % (tab["bg"], Y_OFF)]
        tab_start = len(L)
        ql = []

        def put(kind, c, line):
            k = match(c, params, mp)
            if not k or k not in byk:
                missing.append("%s %s %r %s" % (name, kind, c.get("label"), c.get("keys", [])[:4]))
                return
            if k in placed and k not in cfg.get("twice", ()):   # "twice": a selector both pages need (an edit-pad stepper)
                missing.append("%s %s %r -> %s already placed" % (name, kind, c.get("label"), k))
                return
            placed.add(k)
            ln = line(k, byk[k])
            if k in cfg.get("nudge", {}):   # "nudge": {key: [dx, dy]}, where ours is bigger than the design's control
                dx, dy = cfg["nudge"][k]
                ln = re.sub(r"\b(cx|x)=(-?\d+)", lambda m: "%s=%d" % (m.group(1), int(m.group(2)) + dx), ln, 1)
                ln = re.sub(r"\b(cy|y)=(-?\d+)", lambda m: "%s=%d" % (m.group(1), int(m.group(2)) + dy), ln, 1)
            L.append(ln)
            ql.append((c.get("dom", 0), k))

        # names and values are MPC's live labels, 130 px wide: where the mockup packs controls closer, smaller text
        pts = [(c["cx"], c["cy"]) for c in tab["knobs"]] + [(c["rect"][0] + c["rect"][2] / 2, c["rect"][1] + c["rect"][3] / 2)
                                                           for c in tab["sliders"]]

        def sizes(cx, cy, name):
            d = min([abs(cx - x) for x, y in pts if abs(cy - y) < 60 and abs(cx - x) > 1] or [999])
            ns = max(11, min(17, int(0.92 * d / (0.70 * max(4, len(name))))))
            vs = 22 if d >= 120 else max(14, int(d / 6))
            return "" if (ns, vs) == (17, 22) else " ns=%d vs=%d" % (ns, vs)

        for c in tab["knobs"]:
            s = ctl["strips"][c["strip"]]["size"]
            put("knob", c, lambda k, p, c=c, s=s: 'knob cx=%d cy=%d r=%d label=%s key=%s strip=images/stitch/%s frames=%d%s'
                % (round(c["cx"]), round(c["cy"]) + Y_OFF, (s - 10) // 2, q(lab(k, p, c)), k, c["strip"],
                   ctl["strips"][c["strip"]]["frames"], sizes(c["cx"], c["cy"], lab(k, p, c))))
        for c in tab["enums"]:
            x, y, w, h = c["rect"]
            def line(k, p, c=c, x=x, y=y, w=w, h=h):
                n = len(p.get("options") or [])
                if n != len(c["options"]):
                    print("  %s: %s has %d options, the mockup %d (using ours)" % (port, k, n, len(c["options"])))
                # the option labels shown: cfg "option_labels", else the design's own when they abbreviate ours in order
                # ("EXT" for EXTERNAL), else ours
                shown = cfg.get("option_labels", {}).get(k)
                d = [" ".join(str(o).split()).upper() for o in c["options"]]
                if not shown and len(d) == n and all(norm(a) and (norm(b).startswith(norm(a)) or norm(a) == norm(b))
                                                     for a, b in zip(d, p.get("options") or [])):
                    shown = d
                ow, oh = c["opt"]
                ow = max(ow, 10 * max(len(str(o)) for o in (shown or p.get("options") or ["X"])) + 14)   # room for the labels
                extra = ' options="%s"' % ",".join(shown) if shown and shown != [str(o) for o in p.get("options") or []] else ""
                kind = "enum_v" if c["dir"] == "v" else "enum_h"
                # ours can be taller than the design's (22 px options at least): keep its top where the design's is,
                # so it doesn't cover the caption above
                rows = " rows=2" if (kind == "enum_h" and n > 2 and n * max(40, ow) > w + 40 and
                                     k not in cfg.get("nowrap", ())) else ""   # too wide: two rows
                tall = n * (max(22, oh) + 2) if kind == "enum_v" else (2 * (max(22, oh) + 2) if rows else h)
                return '%s cx=%d cy=%d label="" key=%s sw=%d sh=%d%s%s' % (kind, round(x + w / 2), round(y + max(h, tall) / 2) + Y_OFF,
                                                                         k, max(40, ow), max(22, oh), rows, extra)
            put("enum", c, line)
        for c in tab["toggles"]:
            x, y, w, h = c["rect"]
            put("toggle", c, lambda k, p, x=x, y=y, w=w, h=h, c=c: 'toggle cx=%d cy=%d label=%s key=%s'
                % (round(x + w / 2), round(y + h / 2) + Y_OFF, q(lab(k, p, c)), k))
        for c in tab.get("buttons", []):
            x, y, w, h = c["rect"]
            put("button", c, lambda k, p, x=x, y=y, w=w, h=h, c=c: 'button cx=%d cy=%d label=%s key=%s'
                % (round(x + w / 2), round(y + h / 2) + Y_OFF, q(" ".join(re.sub(r"\s*\(.*?\)", "", cfg.get("labels", {}).get(k) or c.get("label") or p.get("name", k)).split()).upper()), k))
        for c in tab["sliders"]:
            x, y, w, h = c["rect"]
            put("slider", c, lambda k, p, x=x, y=y, w=w, h=h, c=c: '%s cx=%d cy=%d w=%d h=%d label=%s key=%s%s'
                % ("slider_v" if h >= w else "slider_h", round(x + w / 2), round(y + h / 2) + Y_OFF, round(w), round(h),
                   q(lab(k, p, c)), k, sizes(x + w / 2, y + h / 2, lab(k, p, c))))
        for c in tab["steppers"]:
            x, y, w, h = c["rect"]
            def sline(k, p, x=x, y=y, w=w, h=h, c=c):
                g = cfg.get("get", {}).get(k)
                return 'stepper cx=%d cy=%d w=%d h=%d label=%s key=%s%s style=dotmatrix' % (   # arrows are h wide: keep h small
                    round(x + w / 2), round(y + h / 2) + Y_OFF, round(w), round(min(h, 80, w / 5)), '""', k,
                    " get=%s" % g if g else "")
            put("stepper", c, sline)
        for c in tab["popups"]:
            x, y, w, h = c["rect"]
            put("popup", c, lambda k, p, x=x, y=y, w=w, h=h: 'popup cx=%d cy=%d w=%d h=%d label="" key=%s'
                % (round(x + w / 2), round(y + h / 2) + Y_OFF, round(max(w, 110)), round(max(h, 40)), k))   # room for the value
        for c in tab["canvases"]:
            spec = cfg["canvas"].get(c["sel"], {})
            x, y, w, h = c["rect"]
            if spec.get("env"):   # A | D | S | R columns; D, S and R per sustain band (see envelope.py)
                from envelope_layout import lines_for
                cw = envs[c["sel"]]
                L += lines_for(spec["name"], [round(x + cw * i + cw / 2) for i in range(len(spec["env"]))], spec["env"],
                               round(y + h / 2) + Y_OFF, cw, round(h))
            if spec.get("picture"):
                k = spec["picture"]
                n = len(byk[k]["options"])
                L.append('picture x=%d y=%d w=%d h=%d key=%s files=%s fit=stretch' % (
                    round(x), round(y) + Y_OFF, round(w), round(h), k, q(",".join(WAVES(port, k, n)))))
        L[tab_start:] = fit_boxes(L[tab_start:], popts)
        # page order: a row of four in a panel becomes one Q-Link column (MPC outlines each column's four)
        ql = cfg.get("qlinks", {}).get(ti) or [k for _, k in sorted(ql) if k not in cfg.get("qlinks_skip", ())]
        for i in range(0, len(ql), 16):
            L.append('qlinks %s = %s' % (q(name + (" %d" % (i // 16 + 1) if len(ql) > 16 else "")), ",".join(ql[i:i + 16])))
    names = {p["key"]: p.get("name", p["key"]) for p in params}
    while True:   # drop renames that would give two parameters the same name
        final = dict(names, **want)
        seen = {}
        for k, n in final.items():
            seen.setdefault(n, []).append(k)
        clash = [k for ks in seen.values() if len(ks) > 1 for k in ks if k in want and k not in cfg.get("names", {})]
        if not clash:
            break
        for k in clash:
            want.pop(k)
    bad = [n for n, ks in seen.items() if len(ks) > 1]
    if bad:
        sys.exit("names: duplicate parameter names %s (fix cfg names)" % bad)
    renamed = {k: n for k, n in final.items() if n != names[k]}
    pj = os.path.join(D, "params.json")
    if any(p.get("name") != final[p["key"]] for p in json.load(open(pj))["params"]):
        if not os.path.exists(os.path.join(D, "params.pre-stitch.json")):
            shutil.copy(pj, os.path.join(D, "params.pre-stitch.json"))
        d = json.load(open(pj))
        for p in d["params"]:
            p["name"] = final.get(p["key"], p["name"])
        open(pj, "w").write(json.dumps(d, indent=1) + "\n")
        print("%s: %d parameters renamed to the design's names (old list: params.pre-stitch.json)" % (port, len(renamed)))
    L = [re.sub(r"\x00(\w+)\x00", lambda m: final[m.group(1)].replace('"', "'"), l) for l in L]
    open(os.path.join(D, "layout.conf"), "w").write("\n".join(L) + "\n")
    unplaced = [p["key"] for p in params if p["key"] not in placed and not p.get("step_of")
                and p.get("type") != "readout" and not p["key"].endswith("__open")]
    print("%s: %d controls placed over %d pages; not in the mockup (still automatable): %s" % (
        port, len(placed), len(ctl["tabs"]), " ".join(unplaced) or "-"))
    for m in missing:
        print("  unmatched:", m)


def fit_only(port):
    """--fit: just (re)apply fit_boxes to a port's existing layout.conf, page by page (no re-extraction)."""
    f = os.path.join(STEVE, "schwung-ports", port, "layout.conf")
    lines = [re.sub(r" bw=\d+", "", l) for l in open(f).read().splitlines()]
    popts = {p["key"]: p["options"] for p in json.load(open(os.path.join(os.path.dirname(f), "params.json")))["params"]
             if p.get("options")}
    starts = [i for i, l in enumerate(lines) if l.startswith("[tab ")] + [len(lines)]
    for a, b in zip(starts, starts[1:]):
        lines[a:b] = fit_boxes(lines[a:b], popts)
    open(f, "w").write("\n".join(lines) + "\n")
    print("%s: %d controls given a narrower touch box" % (port, sum(" bw=" in l for l in lines)))


if __name__ == "__main__":
    if sys.argv[1:2] == ["--fit"]:
        for port in sys.argv[2:]:
            fit_only(port)
    else:
        for port in sys.argv[1:]:
            convert(port)
