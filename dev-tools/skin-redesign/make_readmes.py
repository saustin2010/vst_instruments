"""Write README.md for the ports added on 2026-10-01 (10 synths + 6 MIDI sequencers), from each port's spec docstring,
UPSTREAM, LICENSE, vst.json, layout.conf and logs. The eleven older ports keep their hand-edited READMEs.
Rerun after a rebuild:  python3 steve/tools/skin-redesign/make_readmes.py [port ...]"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(os.path.dirname(os.path.dirname(HERE)), "schwung-ports")
SYNTHS = ["nusaw", "aphex", "denis", "wurl", "fizzik", "monksynth", "chordism", "monovoice", "tablor", "helm"]
SEQS = ["eucalypso", "superarp", "groovebank", "pixelwalkers", "mazelite", "midiplayer", "grids", "marbles"]
MUTABLE = ["plaits", "rings", "elements", "grids", "marbles", "verglas", "warps", "ringsfx"]
FX = ["verglas", "warps", "ringsfx"]
CLOCK_ONLY = ["grids", "marbles"]
VCV = ["rampage"]   # sequencers that need no notes, only the transport
DATA = {"tablor": ["src/wavetables:wavetables"], "groovebank": ["src/patterns:patterns"], "midiplayer": ["data/MIDI:MIDI"],
        "helm": ["src/dsp/helm/patches/Factory Presets:helm-data/patches/Factory Presets",
                 "src/data/Move Organ.helm:helm-data/patches/Factory Presets/Keys/Move Organ.helm"]}
LOCAL = {
    "tablor": ["`src/dsp/wt/scanner.h`: the user wavetable folder is `/sdcard/vst/tablor/wavetables` (where the shipped "
               "packs go), and the first-run copy into a user folder is skipped (`-DMPC_PORT`).",
               "`src/dsp/tablor_plugin.cpp`: `wt1_name`/`wt2_name` readouts (the table's file name) for the screen.",
               "`src/dsp/wt/loader.h` + `tb_destroy_instance`: the loader thread is joined before the instance is "
               "deleted. Upstream deletes members the loader may still be publishing into (ASan heap-use-after-free "
               "on a quick insert/remove, found by the offline test 2026-10-01).",
               "Originals of every patched file: `bak-upstream/`."],
    "fizzik": ["`src/dsp/fizzik.c` (`-DMPC_PORT`, 2026-10-02): the waveguide's fractional read wraps a position that "
               "float rounding left at exactly the delay length (one past the line; found with steve/tools/fuzz). "
               "Original: `bak-upstream/`."],
    "monovoice": ["LFO destinations (114 options) are steppers with PREV/NEXT triggers instead of a pop-up."],
    "aphex": ["Screen from the Stitch mockup (steve/tools/stitch/convert.py, 2026-10-02): the mockup as background, knobs from "
              "its artwork, an envelope display following EG2, patchbay pop-ups; the generated layout is layout.grid.conf.",
              "Footage switches report labels like `8'`: the wrapper now matches an option's label before reading a "
              "number (generic wrapper fix, `steve/patches/`)."],
    "helm": ["Adds `src/data/Move Organ.helm` (from upstream's `data/`) to the factory patch folder it ships.",
             "mopo (Helm's DSP library), found by the offline test: `Memory` frees a `new[]` buffer with `delete` "
             "(now `delete[]`), and `TriggerWait` copies an uninitialised flag (now initialised). Its oscillators "
             "wrap their phase by signed overflow, so the build adds `-fwrapv`. Originals: `bak-upstream/`.",
             "Vendored tree trimmed from 139 MB to 57 MB (2026-10-01): removed the unused second JUCE copy, JUCE's "
             "examples/extras/docs, concurrentqueue's benchmarks/tests, the GUI/standalone/builds folders and the "
             "bundled VST3_SDK (Steinberg's SDK is never kept here); the build and test pass without them."],
    "plaits": ["`src/dsp/plaits_plugin.cpp` (`-DMPC_PORT`): an `fm_preset_index` param (the FM patch as a number, "
               "so the screen can step it; the names differ per 6-op bank). Original: `bak-upstream/`.",
               "`src/dsp/plaits/dsp/engine2/six_op_engine.cc` (2026-10-02): the 6-op scratch buffer is 4 blocks; the "
               "Move port's 1 block let every render write 48 floats into the FM patch bank (patches 0-1 played "
               "garbage, out-of-range reads; found with steve/tools/fuzz).",
               "All Mutable ports (2026-10-02): `stmlib::Interpolate` clamps its index (a control at exactly 100% "
               "read one past a table). Originals: `bak-upstream/stmlib-dsp.h`."],
    "rings": ["Not a Move module: `mpc/rings_engine.cc` drives Mutable's `rings::Part` (and its string synth) "
              "directly. MIDI note-on = strum at that note (internal exciter), voices rotate as on the module; pitch "
              "bend = FM. Runs at Rings' own 48 kHz, resampled to 44.1 kHz (`mpc/mi_engine.h`).",
              "`src/rings/dsp/part.*`, `performance_state.h` (`-DMPC_PORT`): velocity scales the internal "
              "exciter's strike/pluck (the module has no velocity). Originals: `bak-upstream/`."],
    "ringsfx": ["Not a Move module: `mpc/ringsfx_engine.cc` drives `rings::Part` with the track's audio as its "
                "exciter and Rings' own onset detector as the strummer, at NOTE (MIDI note-ons too, if MPC sends any "
                "to an effect). Runs at 44.1 kHz with its tuning corrected; decays ~9% long.",
                "Same `src/` (with the velocity patch) as `../rings`."],
    "elements": ["Not a Move module: `mpc/elements_engine.cc` drives Mutable's `elements::Part`: MIDI notes are "
                 "GATE (held while any note is down, last note priority), V/OCT and STRENGTH (velocity); LEGATO "
                 "skips the re-strike. Panel smoothing as the module's. SIGNATURE reseeds the per-unit variations "
                 "the module takes from its serial number. Runs at Elements' own 32 kHz, resampled to 44.1 kHz.",
                 "`src/elements/drivers/debug_pin.h` is vendored only because `resonator.cc` includes it (a no-op "
                 "with `-DTEST`)."],
    "grids": ["Not a Move module: `src/grids_fx.c` is Grids' pattern generator rewritten as a per-instance "
              "Schwung MIDI FX module (the original is a single static AVR class); `src/grids_maps.h` holds its "
              "drum maps and Euclidean table unchanged. GPL-3.0, like Grids' AVR code.",
              "Steps follow the MIDI clock from MPC's transport: RESOLUTION 1/16 reads every other map step (the "
              "module on a 4 PPQN clock), 1/32 all of them. Start lines up with MPC's bar. Accents = ACCENT VEL."],
    "marbles": ["Not a Move module: `src/marbles_fx.cc` runs Mutable's T and X/Y generators (unchanged, at 44.1 kHz) "
                "as a Schwung MIDI FX module. Their external clock is MPC's (RATE BASE pulses from the MIDI clock). "
                "T1/T2/T3 gates play X1/X2/X3 as notes (1 V = 1 octave above BASE NOTE) on channels 1/2/3. "
                "`src/marbles_scales.h` = the module's six preset scales."],
    "verglas": ["`src/clouds_move.cpp`: the port drives Clouds fully wet with `dry_wet = 1.0`, which makes the "
                "crossfade lookup read one past its table; now 0.99999 (ASan, 2026-10-01).",
                "`src/clouds/dsp/grain.h` (Mutable's code): a grain at exactly its peak reads `lut_window[4097]`, one "
                "past the table; the index is clamped. Harmless on the module (reads the next flash word), still "
                "fixed. Originals: `bak-upstream/`.",
                "`mpc/schwung_audio_fx.c` = `steve/tools/audiofx` (Schwung audio FX -> the MPC engine interface)."],
    "rampage": ["Not a Move module: Befaco's Rampage from VCVRack/Befaco. `src/Rampage_dsp.hpp` is its module code without "
                "the panel widget, unchanged (`steve/tools/rack/extract_module.py`), compiled against "
                "`steve/tools/rack/rack_shim.hpp`, a small portable stand-in for the Rack API (Module, ports, float_4).",
                "`mpc/rampage_engine.cc`: MIDI notes trigger (AR) or gate (ASR) each channel, KEY TRACK drives EXP CV; OUT A/B, "
                "MIN and MAX go out as MIDI CC through the plugin's own port (`steve/tools/midiout`), end-of-cycle as notes; "
                "AUDIO plays OUT A/B (DC-blocked). Turning CYCLE on kicks the loop once (Rampage only re-fires at a cycle's end)."],
    "warps": ["Not a Move module: `mpc/warps_engine.cc` drives Mutable's `warps::Modulator` at 44.1 kHz (its "
              "Init takes the rate). Internal carrier (sine/triangle/saw at NOTE) x the track's audio, or EXTERNAL "
              "(left = carrier, right = modulator); MODE 2 = the hidden frequency shifter; MIX blends dry back."],
    "wurl": ["Setting its preset reapplies it, even to the same value: the wrapper skips a set that would not change "
             "what the engine reports (generic wrapper fix), so restoring a project keeps your edits."],
}
SEQ_LOCAL = ["`mpc/` = the MIDI FX adapter (`steve/tools/midifx/schwung_midi_fx.c`), Schwung's host headers and "
             "`wrapper/engine.h`. The adapter opens an ALSA MIDI port named after the plugin, sends the module's "
             "notes there, and turns MPC's transport (tempo, position, play/stop) into the 24-PPQN MIDI clock and "
             "Start/Stop the module expects.",
             "`mpc/host/plugin_api_v1.h`: its `offsetof(reserved) == 120` assert is a 64-bit (Move) layout check, so "
             "it is skipped on the MPC's 32-bit ARM (the module and adapter share the one header)."]


def licence(path):
    if not os.path.exists(path):
        return "?"
    t = open(path).read()
    if "Permission is hereby granted" in t:
        return "MIT"
    if "GNU GENERAL PUBLIC LICENSE" in t:
        return "GPL-3.0" if "Version 3" in t else "GPL"
    return next((l.strip() for l in t.splitlines() if l.strip()), "?")


def write(port):
    d = os.path.join(R, port)
    v = json.load(open(os.path.join(d, "vst.json")))
    spec = os.path.join(HERE, "specs", port + ".py")
    doc = re.search(r'^"""(.*?)"""', open(spec).read(), re.S).group(1) if os.path.exists(spec) else ""
    doc = " ".join(doc.split())
    up = open(os.path.join(d, "UPSTREAM")).read().split("\n")
    lic = licence(os.path.join(d, "LICENSE"))
    md = v.get("defines", {}).get("MODULE_DIR", "").strip('"')
    lay = open(os.path.join(d, "layout.conf")).read()
    tabs = re.split(r"^\[tab (.+)\]$", lay, flags=re.M)[1:]
    pages = []
    for name, body in zip(tabs[0::2], tabs[1::2]):
        titles = re.findall(r'^frame .*title="([^"]*)"', body, re.M)
        q = re.search(r'^qlinks "[^"]*" = (.*)$', body, re.M)
        n = len(q.group(1).split(",")) if q else 0
        pages.append("- **%s** (%d controls): %s" % (name, n, " · ".join(titles)))
    test = open(os.path.join(d, "logs", "test.log")).read() if os.path.exists(os.path.join(d, "logs", "test.log")) else ""
    status = "PASSED" if re.search(r"^PASSED", test, re.M) else "not passed yet (see logs/test.log)"
    skin = "%s - VST - %s" % (v["vendor"], v["name"])
    seq = port in SEQS
    data = DATA.get(port, [])
    L = ["# %s — MPC plugin (`%s`)" % (v["name"], port), "",
         doc, "",
         "Upstream: %s (commit %s, %s). Licence: %s." % (up[0], up[1].split()[1][:7], up[1].split()[2], lic),
         "MPC name: **%s** · file `/sdcard/vst/%s`%s." % (v["name"], v["so"], " · data folder `%s`" % md if md else ""), "",
         "**Status 2026-10-01:** ported, skinned and built; offline test %s (ASan/UBSan, including the restore and "
         "slow Q-Link checks); previews in `preview/`. **Not on the MPC yet.** A first install needs the plugin added to "
         "MPC's plugin list, which restarts MPC (see Install)." % status, ""]
    if port in VCV:
        L += ["## Modulating other tracks", "",
              "1. Put Rampage on a track. Notes played into it fire channel A (TRIGGER: a rise and fall) or hold it "
              "(GATE: rise, hold, fall); CYCLE makes a channel loop (an LFO). It is silent unless AUDIO is on.",
              "2. MPC **Preferences → MIDI**: switch **Track** on for the **Rampage** port (MIDI Out).",
              "3. On the track to modulate: set its **MIDI input** to that port, then MIDI-learn the parameter you want "
              "moved to CC OUT A (20) or CC OUT B (21) on CC CHANNEL. MIN/MAX send too when given a CC (0 = off).",
              "4. EOC NOTES sends a short note at the end of each cycle (e.g. to trigger a drum pad on another track).",
              "", "Not checked on a device yet: MPC's MIDI learn from an incoming CC onto a plugin or program parameter.", ""]
    if seq:
        L += ["## Playing other tracks", "",
              "MPC OS ignores a plugin's VST MIDI output, so this plugin opens its own MIDI port (ALSA client "
              "**%s**, port **MIDI Out**; MPC picks a new port up without a restart):" % v["name"], "",
              ("1. Put %s on a track. It needs no notes and makes no sound itself: it plays when MPC's transport "
               "runs." % v["name"]) if port in CLOCK_ONLY else
              "1. Put %s on a track and play or hold notes into it (pads, keys or a MIDI clip). It makes no sound itself." % v["name"],
              "2. MPC **Preferences → MIDI**: switch **Track** on for the %s port." % v["name"],
              "3. On the track(s) that should play: set **MIDI input** to that port (not *All*, and not on %s's own "
              "track, or it hears itself)." % v["name"],
              "4. Press play: it follows MPC's tempo and transport (MIDI clock from the plugin host).", ""]
    if port in FX:
        L += ["## Using it", "",
              "An **audio effect** (vst.json `\"effect\": true`: two inputs, the effect category, `isInstrument=\"0\"` in "
              "MPC's plugin list). Insert it on a track, program or bus where MPC offers plugin effects. These are the "
              "first effect builds of this wrapper: whether MPC OS lists and runs a third-party effect is **not yet "
              "checked on a device**. The wrapper hands the engine 128-frame blocks, so it adds 2.9 ms of latency.", ""]
    L += ["## Pages"] + pages + [""]
    loc = LOCAL.get(port, []) + (SEQ_LOCAL if seq else [])
    if port in MUTABLE and port not in ("plaits", "grids"):   # plaits says it in LOCAL; grids has no stmlib
        loc = loc + ["`src/stmlib/dsp/dsp.h` (`-DMPC_PORT`, 2026-10-02): `Interpolate` clamps its index; a control at "
                     "exactly 100% read one past a lookup table (found with steve/tools/fuzz). Original: "
                     "`bak-upstream/stmlib-dsp.h`."]
    if os.path.exists(os.path.join(d, "chain_params.engine.json")):
        loc.append("`params.base.json` comes from the module's `chain_params` (what the engine actually takes, saved as "
                   "`chain_params.engine.json`), not its menu tree.")
    if loc:
        L += ["## Local changes"] + ["- " + x for x in loc] + [""]
    L += ["## Files", "| Path | What |", "|---|---|",
          "| `vst.json` | build config (name, sources, `\"art\": \"html\"`, layout%s) |" % (", MODULE_DIR" if md else ""),
          ("| `params.base.json` | the engine's parameter list, from its table (`steve/tools/mi/params_from_engine.py`; "
           "VST index = order) |" if os.path.exists(os.path.join(d, "mpc")) and any(f.endswith("_engine.cc") for f in os.listdir(os.path.join(d, "mpc")))
           else "| `params.base.json` | the module's own parameter list (saved once; VST index = order) |"),
          "| `params.json` | the list the build uses, written by the spec (names, switches, steppers) |",
          "| `layout.conf`, `%s.css`, `images/` | the screen, written by `steve/tools/skin-redesign/specs/%s.py`; "
          "edit layout.conf in Skin Studio from here on |" % (port, port),
          "| `src/` | vendored upstream source (`UPSTREAM`, `LICENSE`) |"]
    if seq:
        L.append("| `mpc/` | the MIDI FX adapter and host headers (see Local changes) |")
    elif os.path.isdir(os.path.join(d, "mpc")):
        L.append("| `mpc/` | the MPC engine (see Local changes), `mi_engine.h` / adapter, `engine.h` |")
    if port == "midiplayer":
        L.append("| `data/MIDI/` | the demo file (`steve/tools/midifx/make_demo_mid.py`, original material) |")
    if os.path.isdir(os.path.join(d, "bak-upstream")):
        L.append("| `bak-upstream/` | upstream originals of the patched files |")
    L += ["| `build/`, `deploy/`, `preview/`, `logs/` | build output, install payload (laid out like the device), "
          "page previews, logs |", ""]
    L += ["## Install on the MPC (first time: restarts MPC, so ask first)", "```",
          "steve/tools/deploy.sh <mpc-ip> %s" % port,
          "steve/tools/register.sh <mpc-ip> %s --yes" % port, "```",
          "`deploy.sh` copies `deploy/vst/%s`%s and `deploy/Synths/%s/` (no restart). `register.sh` stops MPC, backs up "
          "`MPC.settings`, adds the plugin-list entry and starts MPC. Later updates only need `deploy.sh`, then remove "
          "and re-insert the plugin." % (v["so"], " + `deploy/vst/%s/`" % os.path.basename(md) if data else "", skin), "",
          "## Rebuild", "```",
          "python3 steve/tools/skin-redesign/specs/%s.py      # only to regenerate the screen from the spec" % port,
          "steve/tools/skin-redesign/build.sh %s %s" % (port, " ".join('"%s"' % x if " " in x else x for x in data)),
          "```", ""]
    open(os.path.join(d, "README.md"), "w").write("\n".join(L))
    print("wrote", os.path.join(d, "README.md"))


for p in sys.argv[1:] or SYNTHS + SEQS + [m for m in MUTABLE if m not in SEQS] + VCV:
    write(p)
