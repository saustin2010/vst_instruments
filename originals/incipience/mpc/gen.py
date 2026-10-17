#!/usr/bin/env python3
"""Incipience: writes params.json (what MPC sees) and src/params_def.h (the engine's table, same order).

The parameters follow the AlphaBase Mk II's own: each instrument's two pages of 16 encoders (A and B), with the
manual's names and its 0-127 ranges, so a value reads the same as on the hardware and a factory kit loads as stored.
Where the Mk II has a range the plugin splits in two (its LFO RATE runs S000-S127 synced, then 000-127 free), there is
a CLOCK switch instead. Parameters are append-only once released: add new ones at the end.

    python3 mpc/gen.py          (from originals/incipience)
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# the instruments, in the AlphaBase's order (its MIDI channels 1-11): key prefix, name prefix, family
INSTR = [("kd", "KICK", "kick"), ("mb", "MBRANE", "mbrane"), ("ch", "CL HAT", "hh"), ("oh", "OP HAT", "hh"),
         ("cp", "CLAP", "mm"), ("rm", "RIM", "mm"), ("cr", "CRASH", "mm"), ("rd", "RIDE", "mm"),
         ("x1", "X1", "x"), ("x2", "X2", "x"), ("fm", "FM", "fm")]

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
NOTES = ["%s%d" % (NOTE_NAMES[n % 12], n // 12 - 2) for n in range(128)]   # MPC's naming: 36 = C1, 60 = C3

LFO_CLOCK = ["FREE", "SYNC"]
ONOFF = ["OFF", "ON"]
# the FM synth's chord when its own note plays it (the AlphaBase stores the chord played into it from a keyboard)
CHORDS = ["ROOT", "OCTAVE", "FIFTH", "OCT+5TH", "MAJOR", "MINOR", "SUS4", "MAJ7", "MIN7", "DOM7", "MIN9", "CLUSTER"]
DELAY_SYNC = ["FREE", "SYNC"]


def build():
    P = []

    def knob(key, name, default, lo=0, hi=127, **kw):
        P.append(dict(key=key, name=name, min=lo, max=hi, default=default, dynamic_display=True, **kw))

    def step(key, name, default, lo, hi, **kw):
        P.append(dict(key=key, name=name, type="stepper", min=lo, max=hi, step=1, default=default,
                      dynamic_display=True, **kw))

    def opts(key, name, options, default=0, **kw):
        P.append(dict(key=key, name=name, options=options, default=options[default], **kw))

    # clamped: the engine holds KIT to the kits it has (built in, plus AlphaBase kit files found)
    P.append(dict(key="kit", name="KIT", type="stepper", min=0, max=511, step=1, default=0, display="int", clamped=True))
    P.append(dict(key="kit_prev", name="PREV KIT", momentary=True, step_of="kit", step_delta=-1))
    P.append(dict(key="kit_next", name="NEXT KIT", momentary=True, step_of="kit", step_delta=1))
    P.append(dict(key="kit_name", name="KIT NAME", min=0, max=0, display="string", type="readout"))

    # ---- KICK (manual 6.1; CCs on channel 1) ----
    k, n = "kd", "KICK"
    knob(k + "_tune", n + " TUNE", 70)          # the attack tune envelope's amount (909-style "tune")
    knob(k + "_pitch", n + " PITCH", 30)        # the base frequency
    knob(k + "_decay", n + " DECAY", 70)
    knob(k + "_harm", n + " HARMONICS", 0)
    knob(k + "_attack", n + " ATTACK", 70)      # how much of PULSE + NOISE
    knob(k + "_pulse", n + " PULSE", 80)
    knob(k + "_noise", n + " NOISE", 30)
    knob(k + "_eq", n + " EQ", 20)
    knob(k + "_ptatk", n + " PITCH ATK", 0)     # the second (software) pitch envelope
    knob(k + "_ptdec", n + " PITCH DEC", 40)
    knob(k + "_compr", n + " COMPRESS", 20)
    step(k + "_metnz", n + " METAL NZ", 1, 0, 127)
    knob(k + "_evamt", n + " PITCH AMT", 0)
    knob(k + "_gate", n + " GATE", 40)
    step(k + "_lfwav", n + " LFO WAVE", 4, 0, 63)
    knob(k + "_lfrat", n + " LFO RATE", 40)
    knob(k + "_lfint", n + " LFO DEPTH", 0)
    knob(k + "_vol", n + " VOLUME", 110)
    knob(k + "_acc", n + " ACCENT", 127)

    # ---- MBRANE (manual 6.2; channel 2) ----
    k, n = "mb", "MBRANE"
    knob(k + "_pitch1", n + " M1 PITCH", 46)
    knob(k + "_damp1", n + " M1 DAMPEN", 58)
    knob(k + "_pitch2", n + " M2 PITCH", 62)
    knob(k + "_damp2", n + " M2 DAMPEN", 50)
    knob(k + "_cupl1", n + " CPL 1>2", 20)
    knob(k + "_cupl2", n + " CPL 2>1", 64)      # 64 = none; below, phase-flipped
    knob(k + "_noise", n + " NOISE", 70)
    knob(k + "_decay", n + " DECAY", 50)
    knob(k + "_p1atk", n + " P1 ATTACK", 0)
    knob(k + "_p1dec", n + " P1 DECAY", 30)
    knob(k + "_p2atk", n + " P2 ATTACK", 0)
    knob(k + "_p2dec", n + " P2 DECAY", 30)
    knob(k + "_nzfil", n + " NOISE FILT", 100)
    step(k + "_mtnz1", n + " METAL NZ 1", 1, 0, 127)
    step(k + "_nzshp", n + " NOISE SHAPE", 6, 0, 12)
    step(k + "_scene", n + " SCENE", 0, 0, 9)
    knob(k + "_e1amt", n + " P1 AMOUNT", 0)
    step(k + "_mtnz2", n + " METAL NZ 2", 0, 0, 127)
    knob(k + "_e2amt", n + " P2 AMOUNT", 0)
    knob(k + "_gate", n + " GATE", 30)
    step(k + "_l1wav", n + " LFO1 WAVE", 4, 0, 63)
    knob(k + "_l1rat", n + " LFO1 RATE", 40)
    knob(k + "_l1int", n + " LFO1 DEPTH", 0)
    knob(k + "_vol", n + " VOLUME", 110)
    step(k + "_l2wav", n + " LFO2 WAVE", 4, 0, 63)
    knob(k + "_l2rat", n + " LFO2 RATE", 40)
    knob(k + "_l2int", n + " LFO2 DEPTH", 0)
    knob(k + "_acc", n + " ACCENT", 127)

    # ---- the six hybrid sample channels (manual 7.1-7.2; channels 3-8). One block each, the same order; the two hats
    # have one more control (HP CUT) at the end of theirs ----
    smp_defaults = {
        "ch": dict(vcdec=44, fidec=36, cutoff=110, res=40, rout=1, mtnz=37, hpcut=70),
        "oh": dict(vcdec=70, fidec=60, cutoff=118, res=30, rout=1, mtnz=37, hpcut=64),
        "cp": dict(vcdec=46, fidec=40, cutoff=80, res=30, rout=3, mtnz=1, evamt=30),
        "rm": dict(vcdec=22, fidec=20, cutoff=96, res=100, rout=3, mtnz=0, evamt=10),
        "cr": dict(vcdec=96, fidec=90, cutoff=100, res=20, rout=2, mtnz=52, evamt=20, vol=96),
        "rd": dict(vcdec=88, fidec=80, cutoff=104, res=50, rout=3, mtnz=91, evamt=10, vol=92),
    }
    for k, n, fam in INSTR:
        if fam not in ("hh", "mm"):
            continue
        d = smp_defaults[k]
        knob(k + "_pitch", n + " PITCH", 55)
        knob(k + "_start", n + " START", 0)
        knob(k + "_stop", n + " STOP", 127)
        step(k + "_sample", n + " SAMPLE", 0, 0, 255)
        knob(k + "_cents", n + " CENTS", 0)
        knob(k + "_loop", n + " LOOP", 0)
        opts(k + "_loopon", n + " LOOP ON", ONOFF, 0)
        opts(k + "_rev", n + " REVERSE", ONOFF, 0)
        knob(k + "_vcatt", n + " VCA ATTACK", 0)
        knob(k + "_vcdec", n + " VCA DECAY", d["vcdec"])
        knob(k + "_vcsus", n + " VCA SUSTAIN", 0)
        knob(k + "_vcrel", n + " VCA RELEASE", d["vcdec"])   # = decay: a short pad tap doesn't cut it
        knob(k + "_fiatt", n + " FLT ATTACK", 0)
        knob(k + "_fidec", n + " FLT DECAY", d["fidec"])
        knob(k + "_fisus", n + " FLT SUSTAIN", 0)
        knob(k + "_firel", n + " FLT RELEASE", d["fidec"])
        knob(k + "_cutoff", n + (" LP CUT" if fam == "hh" else " CUTOFF"), d["cutoff"])
        knob(k + "_res", n + " RESONANCE", d["res"])
        knob(k + "_evamt", n + " ENV AMOUNT", d.get("evamt", 0))
        opts(k + "_rout", n + " ROUTING", ["OFF", "HH FILTER"] if fam == "hh" else ["OFF", "LP", "HP", "BP"],
             d["rout"])
        knob(k + "_nzvol", n + " NOISE VOL", 100)
        step(k + "_mtnz", n + " METAL NZ", d["mtnz"], 0, 127)
        step(k + "_lfwav", n + " LFO WAVE", 4, 0, 63)
        knob(k + "_lfrat", n + " LFO RATE", 40)
        opts(k + "_lfclk", n + " LFO CLOCK", LFO_CLOCK, 0)
        knob(k + "_lftun", n + " LFO TUNE", 0)
        knob(k + "_vol", n + " VOLUME", d.get("vol", 110))
        opts(k + "_lfsyn", n + " LFO RETRIG", ONOFF, 1)
        knob(k + "_lfvca", n + " LFO VCA", 0)
        knob(k + "_lfcut", n + " LFO CUTOFF", 0)
        knob(k + "_acc", n + " ACCENT", 127)
        if fam == "hh":
            knob(k + "_hpcut", n + " HP CUT", d["hpcut"])

    # ---- X SAMPLE 1 and 2 (manual 7.3; channels 9-10): the sample engine without the analog part ----
    for k, n, fam in INSTR:
        if fam != "x":
            continue
        knob(k + "_pitch", n + " PITCH", 55)
        knob(k + "_start", n + " START", 0)
        knob(k + "_stop", n + " STOP", 127)
        step(k + "_sample", n + " SAMPLE", 0, 0, 255)
        knob(k + "_cents", n + " CENTS", 0)
        knob(k + "_loop", n + " LOOP", 0)
        opts(k + "_loopon", n + " LOOP ON", ONOFF, 0)
        opts(k + "_rev", n + " REVERSE", ONOFF, 0)
        step(k + "_lfwav", n + " LFO WAVE", 4, 0, 63)
        knob(k + "_lfrat", n + " LFO RATE", 40)
        opts(k + "_lfclk", n + " LFO CLOCK", LFO_CLOCK, 0)
        knob(k + "_lftun", n + " LFO TUNE", 0)
        knob(k + "_vol", n + " VOLUME", 110)
        opts(k + "_lfsyn", n + " LFO RETRIG", ONOFF, 1)
        knob(k + "_lfvca", n + " LFO VCA", 0)
        knob(k + "_acc", n + " ACCENT", 127)

    # ---- FM SYNTH (manual 7.4; channel 11): page A the 4x4 matrix (operator tunings on the diagonal), page B an
    # envelope row per operator ----
    k, n = "fm", "FM"
    fm_def = {(1, 2): 10, (1, 3): 55, (1, 4): 29, (2, 3): 5, (2, 4): 7, (3, 4): 7}
    op_tune = {1: 16, 2: 29, 3: 68, 4: 95}
    for i in range(1, 5):
        for j in range(1, 5):
            if i == j:
                knob("%s_op%d" % (k, i), "%s OP%d TUNE" % (n, i), op_tune[i])
            else:
                knob("%s_fm%d%d" % (k, i, j), "%s %d>%d" % (n, i, j), fm_def.get((i, j), 0))
    op_env = {1: (0, 60, 127, 30), 2: (0, 40, 0, 0), 3: (0, 26, 0, 0), 4: (0, 18, 0, 0)}
    for i in range(1, 5):
        a, r, amt, fm = op_env[i]
        knob("%s_eg%dat" % (k, i), "%s OP%d ATTACK" % (n, i), a)
        knob("%s_eg%drl" % (k, i), "%s OP%d DECAY" % (n, i), r)
        knob("%s_s%damt" % (k, i), "%s OP%d LEVEL" % (n, i), amt)
        knob("%s_s%dfm" % (k, i), "%s OP%d PITCH ENV" % (n, i), fm)
    step(k + "_voices", n + " VOICES", 4, 1, 6)
    knob(k + "_spatial", n + " SPATIAL", 0)
    opts(k + "_chord", n + " CHORD", CHORDS, 0)
    knob(k + "_vol", n + " VOLUME", 100)
    knob(k + "_acc", n + " ACCENT", 127)

    # ---- MIX: pan, reverb send, delay send per instrument (the AlphaBase's pan page and FX send pages) ----
    rev_def = {"mb": 32, "cp": 56, "rm": 32, "cr": 25, "rd": 31}
    for k, n, fam in INSTR:
        knob(k + "_pan", n + " PAN", 64)
        knob(k + "_rsend", n + " REV", rev_def.get(k, 0))
        knob(k + "_dsend", n + " DLY", 0)

    # ---- FX (manual 7.5) ----
    knob("rv_size", "REV ROOM", 77)
    knob("rv_lo", "REV LOW PASS", 100)
    knob("rv_hi", "REV HIGH PASS", 5)
    knob("rv_level", "REV LEVEL", 63)
    opts("rv_on", "REVERB", ONOFF, 1)
    knob("dl_time", "DLY TIME", 64)
    opts("dl_sync", "DLY CLOCK", DELAY_SYNC, 0)
    knob("dl_fb", "DLY FEEDBACK", 60)
    knob("dl_spatial", "DLY SPATIAL", 88)
    knob("dl_level", "DLY LEVEL", 100)
    opts("dl_on", "DELAY", ONOFF, 1)

    # ---- MIDI and the rest: not part of a kit ----
    for i, (k, n, fam) in enumerate(INSTR):
        P.append(dict(key=k + "_note", name=n + " NOTE", options=NOTES, default=NOTES[36 + i], type="stepper"))
        # the stepper's arrows (as Percolator's NOTEs)
        P.append(dict(key=k + "_note_prev", name=n + " NOTE DOWN", momentary=True, step_of=k + "_note", step_delta=-1))
        P.append(dict(key=k + "_note_next", name=n + " NOTE UP", momentary=True, step_of=k + "_note", step_delta=1))
    for k, n, fam in INSTR:
        opts(k + "_mute", n + " MUTE", ONOFF, 0)
    opts("hh_choke", "HH CHOKE", ONOFF, 1)
    knob("volume", "VOLUME", 100)
    # the SAMPLE steppers' arrows: one sample a tap (owner, 2026-10-13: the knob "scrolls really fast")
    for k, n, fam in INSTR:
        if fam in ("hh", "mm", "x"):
            P.append(dict(key=k + "_sample_prev", name=n + " SAMPLE DOWN", momentary=True, step_of=k + "_sample",
                          step_delta=-1))
            P.append(dict(key=k + "_sample_next", name=n + " SAMPLE UP", momentary=True, step_of=k + "_sample",
                          step_delta=1))

    # ---- MIDI, OUTs and modulation (owner, 2026-10-17: "play the voices chromatically like I can with the
    # percolator", "output routing for each of the tracks", "all the controls should be able to be modulated"); none
    # of it is part of a kit ----
    def arrows(key, name):
        P.append(dict(key=key + "_prev", name=name + " DOWN", momentary=True, step_of=key, step_delta=-1))
        P.append(dict(key=key + "_next", name=name + " UP", momentary=True, step_of=key, step_delta=1))

    # each instrument's KEYS CH (every note on that channel plays it pitched, C3 = as tuned; channels 2-12 to start
    # with: MPC hands the plugin's own track every note-on on channel 1) and OUT (MAIN, or OUT n: [FX] Incipience OUT
    # plays it on another track). "send": the labels 1-16 would otherwise be read as option numbers
    for i, (k, n, fam) in enumerate(INSTR):
        opts(k + "_kch", n + " KEYS CH", KCH, i + 3, send=[str(c) for c in range(len(KCH))])
        arrows(k + "_kch", n + " KEYS CH")
        opts(k + "_out", n + " OUT", OUTS, i + 1)   # its own OUT: it stays in the mix until something plays that
    # MIDI MOD: four lanes per instrument (the mod wheel, Gen Purpose 1-3: CC 1, 16-18, as MPC names them in a MIDI
    # track's lane menu); on the instrument's KEYS CH each sets the control picked here, as Percolator's do
    for k, n, fam in INSTR:
        tg = ["OFF"] + [short_name(P, t) for t in targets(P, k)]
        assert len(tg) == len(set(tg)), (k, "two targets with one name")
        for lane, lname, cc in LANES:
            opts(k + "_" + lane, n + " " + lname, tg, 0)
            arrows(k + "_" + lane, n + " " + lname)
    # the mod matrix: four LFOs, then 16 slots, each a SOURCE moving one control (DEST: the instrument, CONTROL: which
    # of its controls) by DEPTH (+-100 % of the control's range)
    for l in range(1, NLFO + 1):
        step("l%d_wave" % l, "LFO%d WAVE" % l, 4, 0, 63)
        knob("l%d_rate" % l, "LFO%d RATE" % l, 40)
        opts("l%d_clock" % l, "LFO%d CLOCK" % l, LFO_CLOCK, 0)
        opts("l%d_trig" % l, "LFO%d RESTART" % l, ["FREE"] + [n for _, n, _ in INSTR], 0)
    nctl = max(len(targets(P, d)) for d in DEST_KEYS)
    for m in range(1, NSLOT + 1):
        opts("m%d_src" % m, "MOD%d SOURCE" % m, SOURCES, 0)
        opts("m%d_dest" % m, "MOD%d DEST" % m, DESTS, 0)
        step("m%d_ctl" % m, "MOD%d CONTROL" % m, 0, 0, nctl - 1)
        arrows("m%d_ctl" % m, "MOD%d CONTROL" % m)
        knob("m%d_depth" % m, "MOD%d DEPTH" % m, 0, -100, 100)
    # RANDOMISE (owner, 2026-10-18: "a randomise button on each voice which sits outside of the mix"): a new sound from
    # the instrument's own controls (RAND below); its VOLUME, ACCENT and MIX stay as they are
    for k, n, fam in INSTR:
        P.append(dict(key=k + "_rand", name=n + " RANDOMISE", momentary=True))
    return P


# What RANDOMISE gives each sound control, by instrument and the key's suffix: (lo, hi), a whole number in that range;
# (lo, hi, p, v): v p % of the time, else lo..hi; "=x": the value x got (a release as long as the decay, so a short pad
# tap doesn't cut it); "smp": a SAMPLE of the instrument's kind (src/incipience.c, rand_sample). Left out: not touched.
_SMP = {"pitch": (35, 80), "start": (0, 30, 60, 0), "stop": (60, 127, 60, 127), "sample": "smp", "cents": (0, 40, 60, 0),
        "loop": (0, 127), "loopon": (0, 1, 85, 0), "rev": (0, 1, 85, 0), "lfwav": (0, 63), "lfrat": (0, 127),
        "lfclk": (0, 1), "lftun": (0, 30, 75, 0), "lfsyn": (0, 1), "lfvca": (0, 60, 75, 0)}
_HYB = dict(_SMP, vcatt=(0, 30, 60, 0), vcsus=(0, 60, 80, 0), vcrel="=vcdec", fiatt=(0, 30, 60, 0), fidec=(10, 100),
            fisus=(0, 60, 80, 0), firel="=fidec", evamt=(0, 90), nzvol=(20, 127), mtnz=(1, 127, 25, 0),
            lfcut=(0, 60, 70, 0))
_VCDEC = {"ch": (20, 60), "oh": (50, 100), "cp": (30, 80), "rm": (15, 60), "cr": (70, 120), "rd": (60, 115)}
_LFO = {"wav": (0, 63), "rat": (0, 127), "int": (0, 40, 75, 0)}
RAND = {
    "kd": {"tune": (20, 110), "pitch": (10, 70), "decay": (30, 110), "harm": (0, 90, 40, 0), "attack": (20, 127),
           "pulse": (20, 127), "noise": (0, 90), "eq": (0, 90), "ptatk": (0, 50, 50, 0), "ptdec": (10, 100),
           "compr": (0, 90), "metnz": (0, 127, 60, 0), "evamt": (0, 70, 50, 0), "gate": (10, 100),
           **{"lf" + s: r for s, r in _LFO.items()}},
    "mb": {"pitch1": (20, 100), "damp1": (20, 110), "pitch2": (20, 110), "damp2": (20, 110), "cupl1": (0, 90),
           "cupl2": (20, 110), "noise": (0, 127), "decay": (20, 100), "p1atk": (0, 40, 50, 0), "p1dec": (10, 90),
           "p2atk": (0, 40, 50, 0), "p2dec": (10, 90), "nzfil": (20, 127), "mtnz1": (0, 127, 50, 0), "nzshp": (0, 12),
           "scene": (0, 9, 40, 0), "e1amt": (0, 80, 40, 0), "mtnz2": (0, 127, 60, 0), "e2amt": (0, 80, 40, 0),
           "gate": (10, 90), **{"l%d%s" % (l, s): r for l in (1, 2) for s, r in _LFO.items()}},
    "x1": _SMP, "x2": _SMP,
    "fm": {**{"op%d" % i: (0, 127) for i in range(1, 5)},
           **{"fm%d%d" % (i, j): (0, 90, 45, 0) if i < j else (0, 70, 70, 0) for i in range(1, 5) for j in range(1, 5) if i != j},
           **{"eg%dat" % i: (0, 40, 60, 0) for i in range(1, 5)}, **{"eg%drl" % i: (10, 100) for i in range(1, 5)},
           "s1amt": (60, 127), **{"s%damt" % i: (0, 127, 60, 0) for i in range(2, 5)},   # OP1 always heard
           **{"s%dfm" % i: (0, 60, 60, 0) for i in range(1, 5)}},
}
for _k in ("ch", "oh"):
    RAND[_k] = dict(_HYB, vcdec=_VCDEC[_k], cutoff=(60, 127), res=(0, 100), rout=(0, 1, 80, 1), hpcut=(30, 100))
for _k in ("cp", "rm", "cr", "rd"):
    RAND[_k] = dict(_HYB, vcdec=_VCDEC[_k], cutoff=(30, 127), res=(0, 110), rout=(0, 3))


NOUT = 11       # OUT 1-11: every instrument can have a track of its own
NLFO = 4
NSLOT = 16
KCH = ["OFF", "ALL"] + [str(c) for c in range(1, 17)]
OUTS = ["MAIN"] + ["OUT %d" % o for o in range(1, NOUT + 1)]
LANES = (("wheel", "MOD WHEEL", 1), ("gp1", "GEN PURP 1", 16), ("gp2", "GEN PURP 2", 17), ("gp3", "GEN PURP 3", 18))
SOURCES = ["OFF", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "RANDOM", "VELOCITY"]
DESTS = [n for _, n, _ in INSTR] + ["FX"]
DEST_KEYS = [k for k, _, _ in INSTR] + ["fx"]


def targets(P, d):
    """what a lane or a mod slot can move on instrument d (its sound and its pan / sends), or on "fx" (the reverb,
       the delay and the master VOLUME): every control a kit stores, MUTE aside"""
    keys = [p["key"] for p in P]
    if d == "fx":
        return [k for k in keys if k.startswith(("rv_", "dl_"))] + ["volume"]
    kit = keys[keys.index("kit_name") + 1:keys.index("kd_note")]
    return [k for k in kit if k.startswith(d + "_")]


def short_name(P, key):
    """a control's name without its instrument: KICK DECAY -> DECAY (FX keeps REV / DLY: they name the effect)"""
    p = next(q for q in P if q["key"] == key)
    pre = next((n for k, n, _ in INSTR if key.startswith(k + "_")), None)
    return p["name"][len(pre) + 1:] if pre else p["name"]


#: The screen: a tab per instrument (the AlphaBase's instrument keys), then FX and MIX. Each has two Q-Link sub-pages,
#: A and B, as the hardware's PAGE A/B: four rows of up to four controls, a row a panel and a Q-Link column (the
#: hardware's display shows one row of its 4 x 4 encoder matrix at a time). A row the same on A and B is one panel
#: on both. "side": touch-only controls in the right column. Rows: (title, red caption, [keys]); "-" = an empty slot.
def _smp_rows(k, hh):
    # NOISE VOL on page A, with SAMPLE: the metal noise at its full level covers a sample (owner, 2026-10-18: "the sample
    # button doesn't do anything on CL Hat A"); turned down, or ROUTING OFF, the sample comes through
    filt_a = [k + "_cutoff", k + "_hpcut", k + "_nzvol", k + "_rout"] if hh else [k + "_cutoff", k + "_res", k + "_nzvol", k + "_rout"]
    filt_b = [k + "_cutoff", k + "_res", k + "_evamt", k + "_mtnz"]
    ftitle = "HH FILTER" if hh else "MM FILTER"
    return {
        "A": [("SAMPLE", "PLAYBACK", [k + "_pitch", k + "_start", k + "_stop", k + "_sample"]),
              ("VCA", "ENVELOPE", [k + "_vcatt", k + "_vcdec", k + "_vcsus", k + "_vcrel"]),
              (ftitle, "SAMPLE + METAL NOISE", filt_a),
              ("LFO / LEVEL", "TO PITCH", [k + "_lfwav", k + "_lfrat", k + "_lftun", k + "_vol"])],
        "B": [("SAMPLE", "TUNE + LOOP", [k + "_cents", k + "_loop", k + "_loopon", k + "_rev"]),
              ("FILTER", "ENVELOPE", [k + "_fiatt", k + "_fidec", k + "_fisus", k + "_firel"]),
              (ftitle, "ENVELOPE + NOISE", filt_b),
              ("LFO", "TO VCA + CUTOFF", [k + "_lfsyn", k + "_lfvca", k + "_lfcut", k + "_acc"])],
        "side": [k + "_lfclk"],
    }


PAGES = [
    ("KICK", "kd", "KICK DRUM", {
        "A": [("OSCILLATOR", "TUNE ENVELOPE", ["kd_tune", "kd_pitch", "kd_decay", "kd_harm"]),
              ("ATTACK", "PULSE + NOISE", ["kd_attack", "kd_pulse", "kd_noise", "kd_eq"]),
              ("SHAPE", "METAL NOISE", ["kd_compr", "kd_metnz", "kd_evamt", "kd_gate"]),
              ("LFO / LEVEL", "TO PITCH", ["kd_lfwav", "kd_lfrat", "kd_lfint", "kd_vol"])],
        "B": [("OSCILLATOR", "TUNE ENVELOPE", ["kd_tune", "kd_pitch", "kd_decay", "kd_harm"]),
              ("PITCH ENV", "808 GLIDE", ["kd_ptatk", "kd_ptdec", "kd_noise", "kd_eq"]),
              ("SHAPE", "METAL NOISE", ["kd_compr", "kd_metnz", "kd_evamt", "kd_gate"]),
              ("LFO / ACCENT", "TO PITCH", ["kd_lfwav", "kd_lfrat", "kd_lfint", "kd_acc"])],
        "side": []}),
    ("MBRANE", "mb", "MBRANE", {
        "A": [("MEMBRANES", "M1 + M2", ["mb_pitch1", "mb_damp1", "mb_pitch2", "mb_damp2"]),
              ("COUPLING", "NOISE + DECAY", ["mb_cupl1", "mb_cupl2", "mb_noise", "mb_decay"]),
              ("NOISE", "METAL NOISE 1", ["mb_nzfil", "mb_mtnz1", "mb_nzshp", "mb_scene"]),
              ("LFO 1 / LEVEL", "TO M1", ["mb_l1wav", "mb_l1rat", "mb_l1int", "mb_vol"])],
        "B": [("MEMBRANES", "M1 + M2", ["mb_pitch1", "mb_damp1", "mb_pitch2", "mb_damp2"]),
              ("PITCH ENV", "M1 + M2", ["mb_p1atk", "mb_p1dec", "mb_p2atk", "mb_p2dec"]),
              ("ENV / NOISE", "METAL NOISE 2", ["mb_e1amt", "mb_mtnz2", "mb_e2amt", "mb_gate"]),
              ("LFO 2 / ACCENT", "TO M2", ["mb_l2wav", "mb_l2rat", "mb_l2int", "mb_acc"])],
        "side": []}),
    ("CL HAT", "ch", "CLSD HI HAT", _smp_rows("ch", True)),
    ("OP HAT", "oh", "OPEN HI HAT", _smp_rows("oh", True)),
    ("CLAP", "cp", "CLAP", _smp_rows("cp", False)),
    ("RIM", "rm", "RIM SHOT", _smp_rows("rm", False)),
    ("CRASH", "cr", "CRASH", _smp_rows("cr", False)),
    ("RIDE", "rd", "RIDE", _smp_rows("rd", False)),
] + [(n, k, t, {
        "A": [("SAMPLE", "PLAYBACK", [k + "_pitch", k + "_start", k + "_stop", k + "_sample"]),
              ("LFO / LEVEL", "TO PITCH", [k + "_lfwav", k + "_lfrat", k + "_lftun", k + "_vol"])],
        "B": [("SAMPLE", "TUNE + LOOP", [k + "_cents", k + "_loop", k + "_loopon", k + "_rev"]),
              ("LFO", "TO LEVEL", [k + "_lfsyn", k + "_lfvca", "-", k + "_acc"])],
        "side": [k + "_lfclk"]}) for n, k, t in (("X1", "x1", "X SAMPLE 1"), ("X2", "x2", "X SAMPLE 2"))] + [
    ("FM", "fm", "FM SYNTH", {
        "A": [("OP 1", "TUNE + FM OUT", ["fm_op1", "fm_fm12", "fm_fm13", "fm_fm14"]),
              ("OP 2", "TUNE + FM OUT", ["fm_fm21", "fm_op2", "fm_fm23", "fm_fm24"]),
              ("OP 3", "TUNE + FM OUT", ["fm_fm31", "fm_fm32", "fm_op3", "fm_fm34"]),
              ("OP 4", "TUNE + FM OUT", ["fm_fm41", "fm_fm42", "fm_fm43", "fm_op4"])],
        "B": [("OP %d ENV" % i, "ATTACK DECAY LEVEL PITCH",
               ["fm_eg%dat" % i, "fm_eg%drl" % i, "fm_s%damt" % i, "fm_s%dfm" % i]) for i in range(1, 5)],
        "side": ["fm_voices", "fm_chord", "fm_spatial", "fm_vol", "fm_acc"]}),
    ("FX", None, "EFFECTS", {
        "A": [("REVERB", "ROOM + DAMPING", ["rv_size", "rv_lo", "rv_hi", "rv_level"]),
              ("REV SENDS", "KD MB CH OH", ["kd_rsend", "mb_rsend", "ch_rsend", "oh_rsend"]),
              ("REV SENDS", "CP RM CR RD", ["cp_rsend", "rm_rsend", "cr_rsend", "rd_rsend"]),
              ("REV SENDS", "X1 X2 FM + ON", ["x1_rsend", "x2_rsend", "fm_rsend", "rv_on"])],
        "B": [("DELAY", "TIME + REPEATS", ["dl_time", "dl_fb", "dl_spatial", "dl_level"]),
              ("DLY SENDS", "KD MB CH OH", ["kd_dsend", "mb_dsend", "ch_dsend", "oh_dsend"]),
              ("DLY SENDS", "CP RM CR RD", ["cp_dsend", "rm_dsend", "cr_dsend", "rd_dsend"]),
              ("DLY SENDS", "X1 X2 FM + ON", ["x1_dsend", "x2_dsend", "fm_dsend", "dl_on"])],
        "side": ["dl_sync"]}),
    ("MIX", None, "MIX", {
        "pages": [("PAN", [("PAN", "KD MB CH OH", ["kd_pan", "mb_pan", "ch_pan", "oh_pan"]),
                           ("PAN", "CP RM CR RD", ["cp_pan", "rm_pan", "cr_pan", "rd_pan"]),
                           ("PAN", "X1 X2 FM", ["x1_pan", "x2_pan", "fm_pan", "volume"]),
                           ("HATS", "CHOKE", ["hh_choke", "-", "-", "-"])])],
        "side": []}),
]


def _per_inst(suffix, title):
    """a row per group of the instrument keys, as on the MIX page"""
    ks = [k for k, _, _ in INSTR]
    return [(title, "KD MB CH OH", [k + suffix for k in ks[0:4]]),
            (title, "CP RM CR RD", [k + suffix for k in ks[4:8]]),
            (title, "X1 X2 FM", [k + suffix for k in ks[8:11]] + ["-"])]


# Tabs with sub-pages of their own ("pages": (name, rows) each; "info": the right column's notes), after MIX: the mod
# matrix, then MIDI & OUT and MIDI MOD as on Percolator
PAGES += [
    ("MOD", None, "MODULATION", {
        "pages": [("LFO", [("LFO %d" % l, "WAVE RATE CLOCK RESTART",
                            ["l%d_wave" % l, "l%d_rate" % l, "l%d_clock" % l, "l%d_trig" % l]) for l in range(1, NLFO + 1)])]
        + [("MOD %d-%d" % (s, s + 3), [("MOD %d" % m, "SOURCE > DEST CONTROL DEPTH",
                                         ["m%d_src" % m, "m%d_dest" % m, "m%d_ctl" % m, "m%d_depth" % m])
                                        for m in range(s, s + 4)]) for s in range(1, NSLOT + 1, 4)],
        "side": [],
        "info": ["EACH MOD SLOT MOVES ONE CONTROL:", "PICK THE INSTRUMENT (DEST), THEN", "THE CONTROL, AND HOW FAR (DEPTH)",
                 "RANDOM, VELOCITY: AT EACH HIT", "OF THE DEST INSTRUMENT"]}),
    ("MIDI & OUT", None, "MIDI + OUTPUTS", {
        "pages": [("NOTE", _per_inst("_note", "NOTE")), ("KEYS CH", _per_inst("_kch", "KEYS CH")),
                  ("OUT", _per_inst("_out", "OUT"))],
        "side": [],
        "info": ["KEYS CH: A MIDI TRACK SENT TO", "INCIPIENCE MIDI IN ON THAT CHANNEL", "PLAYS IT PITCHED (C3 = AS TUNED)",
                 "OUT N: [FX] INCIPIENCE OUT ON", "ANOTHER TRACK PLAYS IT THERE"]}),
    ("MIDI MOD", None, "MIDI MOD LANES", {
        "pages": [("WHEEL", _per_inst("_wheel", "MOD WHEEL"))]
        + [("GP %d" % g, _per_inst("_gp%d" % g, "GEN PURP %d" % g)) for g in (1, 2, 3)],
        "side": [],
        "info": ["ON AN INSTRUMENT'S KEYS CH, EACH", "LANE SETS THE CONTROL PICKED HERE", "MIDI TRACK LANES: MIDI 1 MOD WHEEL,",
                 "MIDI 16-18 GEN PURPOSE 1-3", "KEYS CH ALL: ON ANY CHANNEL"]}),
]


def subpages(tab, inst, pg):
    """a tab's Q-Link sub-pages: (name, rows, kind); kind C = an instrument's third, page A's matrix with the MIX panel
       and extras on the Q-Links"""
    if "pages" in pg:
        return [(n, rows, n) for n, rows in pg["pages"]]
    return [(n, pg["B" if kind == "B" else "A"], kind) for n, kind in zip(sub_names(tab, inst), "ABC")]


def mix_qlinks(k, side):
    """the instrument's third sub-page: its MIX panel (a Q-Link column), then its extras, four to a column"""
    mix = [k + "_pan", k + "_rsend", k + "_dsend", k + "_mute"]
    # FM VOICE: the voice controls first (its panel is drawn above MIX; owner: "needs to be its own thing")
    q = list(side[:4]) + mix + list(side[4:]) if k == "fm" else mix + list(side)
    return q + ["-"] * (-len(q) % 4)


def sub_names(tab, inst):
    """the Q-Link sub-pages of a tab, as MPC's tab strip names them"""
    if tab == "FX":
        return ["REVERB", "DELAY"]
    return [tab + " A", tab + " B", "FM VOICE" if inst == "fm" else tab + " MIX"]


def check_pages(P):
    """every control on some page, none twice on one sub-page, every key real"""
    keys = {p["key"] for p in P}
    seen = set()
    for tab, k, title, pg in PAGES:
        for sub, rows, _ in subpages(tab, k, pg):
            on = [x for _, _, row in rows for x in row if x != "-"]
            assert len(on) == len(set(on)), (tab, sub, "twice")
            for x in on + pg["side"]:
                assert x in keys, (tab, x)
            seen |= set(on) | set(pg["side"])
        if k:
            seen |= {k + "_pan", k + "_rsend", k + "_dsend", k + "_mute"}   # the instrument's MIX box
    missing = [p["key"] for p in P if p["key"] not in seen and not p["key"].startswith("kit") and not p.get("momentary")]
    assert not missing, missing


def write_header(P):
    L = ["/* generated by mpc/gen.py: do not edit (change gen.py and run it) */", "#pragma once",
         "/* kind: 0 = knob min..max (hardware units, continuous), 1 = option list (value = index), 2 = kit number,",
         " * 3 = not a stored value, 4 = whole-number stepper min..max */",
         "typedef struct { const char *key; int kind; int nopts; float lo, hi, def; } pdef_t;",
         "static const pdef_t PDEF[] = {"]
    enum = []
    for i, p in enumerate(P):
        lo, hi = p.get("min", 0), p.get("max", 0)
        if "options" in p:
            kind, n, d, lo, hi = 1, len(p["options"]), p["options"].index(p["default"]), 0, len(p["options"]) - 1
        elif p["key"] == "kit":
            kind, n, d = 2, 0, 0
        elif p.get("momentary") or p.get("display") == "string":
            kind, n, d = 3, 0, 0
        elif p.get("type") == "stepper":
            kind, n, d = 4, 0, p["default"]
        else:
            kind, n, d = 0, 0, p["default"]
        L.append('    {"%s", %d, %d, %d.0f, %d.0f, %d.0f},' % (p["key"], kind, n, lo, hi, d))
        enum.append("P_%s = %d," % (p["key"].upper(), i))
    L += ["};", "enum {"] + ["    " + e for e in enum] + ["    NP = %d" % len(P), "};"]
    idx = {p["key"]: i for i, p in enumerate(P)}
    # the sample channels' blocks: same order, so S_<NAME> is an offset from a channel's first control
    first = idx["ch_pitch"]
    L.append("/* the hybrid sample channels CH OH CP RM CR RD: P_<CH>_PITCH + S_<NAME> */")
    L.append("static const int SBASE[6] = {%s};" % ", ".join(str(idx[k + "_pitch"]) for k in
                                                         ("ch", "oh", "cp", "rm", "cr", "rd")))
    for p in P[first:idx["ch_hpcut"] + 1]:
        L.append("#define S_%s %d" % (p["key"][3:].upper(), idx[p["key"]] - first))
    first = idx["x1_pitch"]
    L.append("/* X1 and X2: P_X1_PITCH + X_<NAME> */")
    L.append("static const int XBASE[2] = {%d, %d};" % (idx["x1_pitch"], idx["x2_pitch"]))
    for p in P[first:idx["x1_acc"] + 1]:
        L.append("#define X_%s %d" % (p["key"][3:].upper(), idx[p["key"]] - first))
    L.append("#define MIXSTRIDE 3   /* <inst>_pan, _rsend, _dsend from P_KD_PAN */")
    L.append("#define NOTESTRIDE 3  /* <inst>_note, _note_prev, _note_next from P_KD_NOTE */")
    L.append("#define NCHORD %d" % len(CHORDS))
    # MIDI, OUTs, modulation: each block's stride from its first parameter
    L.append("#define IOSTRIDE %d    /* <inst>_kch, its arrows, _out: P_KD_KCH + IOSTRIDE * inst */"
             % (idx["mb_kch"] - idx["kd_kch"]))
    L.append("#define LANESTRIDE %d  /* <inst>_wheel, _gp1, _gp2, _gp3 (each with its arrows) from P_KD_WHEEL */"
             % (idx["mb_wheel"] - idx["kd_wheel"]))
    L.append("#define LANESTEP %d     /* one lane to the next */" % (idx["kd_gp1"] - idx["kd_wheel"]))
    L.append("#define LFOSTRIDE %d    /* l<n>_wave, _rate, _clock, _trig from P_L1_WAVE */"
             % (idx["l2_wave"] - idx["l1_wave"]))
    L.append("#define SLOTSTRIDE %d   /* m<n>_src, _dest, _ctl, its arrows, _depth from P_M1_SRC */"
             % (idx["m2_src"] - idx["m1_src"]))
    for k in ("_out", "_kch_prev"):
        assert idx["kd" + k] - idx["kd_kch"] == idx["mb" + k] - idx["mb_kch"]
    L += ["#define NOUT %d" % NOUT, "#define NLFO %d" % NLFO, "#define NSLOT %d" % NSLOT,
          "#define NDEST %d    /* the instruments, then FX */" % len(DEST_KEYS),
          "enum { SRC_OFF, SRC_LFO1, SRC_RANDOM = SRC_LFO1 + NLFO, SRC_VELOCITY };"]
    assert SOURCES[1 + NLFO:] == ["RANDOM", "VELOCITY"]
    T = [targets(P, d) for d in DEST_KEYS]
    L.append("#define NCTL %d" % max(len(t) for t in T))
    L.append("/* what a lane or a mod slot moves: MODT[dest][control], MODN[dest] controls; their names */")
    L.append("static const short MODT[NDEST][NCTL] = {")
    L += ["    {%s}," % ", ".join("P_" + k.upper() for k in t) for t in T]
    L.append("};")
    L.append("static const unsigned char MODN[NDEST] = {%s};" % ", ".join(str(len(t)) for t in T))
    L.append("static const char *const MODNAME[NDEST][NCTL] = {")
    L += ["    {%s}," % ", ".join('"%s"' % short_name(P, k) for k in t) for t in T]
    L.append("};")
    # RANDOMISE: P_KD_RAND + inst; what each control gets (RAND above)
    assert [idx[k + "_rand"] for k, _, _ in INSTR] == list(range(idx["kd_rand"], idx["kd_rand"] + len(INSTR)))
    L.append("/* RANDOMISE: how 0 = left as it is, 1 = lo..hi (v p % of the time), 2 = what control eq got, 3 = a SAMPLE */")
    L.append("typedef struct { unsigned char how, p; short lo, hi, v, eq; } rnd_t;")
    L.append("static const rnd_t RND[NP] = {")
    for k, _, _ in INSTR:
        sounds = targets(P, k)
        for suf, r in RAND[k].items():
            key = k + "_" + suf
            assert key in sounds and suf not in ("vol", "acc", "pan", "rsend", "dsend"), key
            if r == "smp":
                L.append("    [P_%s] = {3, 0, 0, 0, 0, 0}," % key.upper())
            elif isinstance(r, str):
                L.append("    [P_%s] = {2, 0, 0, 0, 0, P_%s}," % (key.upper(), (k + "_" + r[1:]).upper()))
            else:
                lo, hi, p, v = (r + (0, 0))[:4]
                p_ = next(q for q in P if q["key"] == key)
                top = len(p_["options"]) - 1 if "options" in p_ else p_["max"]
                assert p_.get("min", 0) <= lo <= hi <= top and p_.get("min", 0) <= v <= top, key
                L.append("    [P_%s] = {1, %d, %d, %d, %d, 0}," % (key.upper(), p, lo, hi, v))
    L.append("};")
    open(os.path.join(ROOT, "src", "params_def.h"), "w").write("\n".join(L) + "\n")


if __name__ == "__main__":
    P = build()
    keys = [p["key"] for p in P]
    assert len(keys) == len(set(keys)), "duplicate keys"
    check_pages(P)
    write_header(P)
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    # each tab's sub-pages in order: "subs" their names, "sub_rows" their rows, "sub_kind" A / B / C or the name
    json.dump([{"tab": t, "inst": k, "title": ti, "subs": [s[0] for s in subpages(t, k, pg)],
                "sub_rows": [s[1] for s in subpages(t, k, pg)], "sub_kind": [s[2] for s in subpages(t, k, pg)],
                "qlinks_c": mix_qlinks(k, pg["side"]) if k else None, "side": pg["side"], "info": pg.get("info", [])}
               for t, k, ti, pg in PAGES],
              open(os.path.join(ROOT, "build", "pages.json"), "w"), indent=1)
    json.dump({"name": "Incipience", "params": P}, open(os.path.join(ROOT, "params.json"), "w"), indent=1)
    print("params.json: %d parameters; src/params_def.h" % len(P))
