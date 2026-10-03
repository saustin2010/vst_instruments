#!/usr/bin/env python3
"""Convert an OB-Xd 1.x LV2 preset bank (presets.ttl, as DISTRHO's LV2 port of Obxd saves them) into an .fxb bank,
the format the OB-Xd plugin reads from /sdcard/vst/obxd/presets/:
    python3 tools/obxd-lv2-to-fxb.py <presets.ttl | a .tar.bz2/.tar.gz/.zip holding one> <out.fxb>
    e.g. python3 tools/obxd-lv2-to-fxb.py obxd_bank.tar.bz2 "presets/obxd/presets/Obxd Bank.fxb"
The bank shows on the MPC under the .fxb's file name. The .fxb is laid out like the plugin's own factory.fxb (an FXB
chunk holding JUCE's binary XML: <program programName=... Val_<index>=...>), so desktop OB-Xd loads it too. LV2 names
a parameter by symbol; OB-Xd's .fxb by index (ParamsEnum.h): the table below maps one to the other. Parameters that
OB-Xd 2 added after 1.x get the values discoDSP's own conversion of the 1.x factory bank gave them."""
import io, os, re, struct, sys, tarfile, zipfile
from xml.sax.saxutils import quoteattr

# OB-Xd 1.x LV2 symbol -> ParamsEnum index (schwung/instruments/obxd/src/dsp/Engine/ParamsEnum.h)
INDEX = {
    "volume": 2, "voicecount": 3, "tune": 4, "octave": 5, "bendrange": 6, "bendosc2only": 7, "legatomode": 8,
    "vibratorate": 9, "vfltfactor": 10, "vampfactor": 11, "asplayedallocation": 12, "portamento": 13, "unison": 14,
    "voicedetune": 15, "oscillator2detune": 16, "lfofrequency": 17, "lfosinewave": 18, "lfosquarewave": 19,
    "lfosampleholdwave": 20, "lfoamount1": 21, "lfoamount2": 22, "lfoosc1": 23, "lfoosc2": 24, "lfofilter": 25,
    "lfopw1": 26, "lfopw2": 27, "osc2hardsync": 28, "xmod": 29, "osc1pitch": 30, "osc2pitch": 31, "pitchquant": 32,
    "osc1saw": 33, "osc1pulse": 34, "osc2saw": 35, "osc2pulse": 36, "pulsewidth": 37, "brightness": 38,
    "envelopetopitch": 39, "osc1mix": 40, "osc2mix": 41, "noisemix": 42, "filterkeyfollow": 43, "cutoff": 44,
    "resonance": 45, "multimode": 46, "filter_warm": 47, "bandpassblend": 48, "fourpole": 49, "filterenvamount": 50,
    "attack": 51, "decay": 52, "sustain": 53, "release": 54, "filterattack": 55, "filterdecay": 56,
    "filtersustain": 57, "filterrelease": 58, "envelopedetune": 59, "filterdetune": 60, "portamentodetune": 61,
    **{"pan%d" % i: 61 + i for i in range(1, 9)},
}
# not sound parameters: the LV2 wrapper's freewheel port and Obxd's unnamed/unused slots
IGNORED = re.compile(r"lv2_freewheel|lv2_port_\d+|unused\d*")
# OB-Xd 2's parameters after PAN8 (UNLEARN .. SELF_OSC_PUSH): as in every program of factory.fxb (economy mode on,
# level variation 0.3, the rest off)
NEWER = {70: 0.0, 71: 1.0, 72: 0.0, 73: 0.0, 74: 0.0, 75: 0.0, 76: 0.0, 77: 0.0, 78: 0.3, 79: 0.0}
COUNT = 80          # Val_0 .. Val_79, as factory.fxb
MAX_PROGRAMS = 128  # what the plugin reads from one bank


def read_ttl(path):
    """presets.ttl text, from the file itself or from the first presets.ttl inside an archive"""
    if tarfile.is_tarfile(path):
        with tarfile.open(path) as t:
            m = next((m for m in t.getmembers() if m.isfile() and m.name.endswith("presets.ttl")), None)
            if m:
                return t.extractfile(m).read().decode("utf-8")
    elif zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            n = next((n for n in z.namelist() if n.endswith("presets.ttl")), None)
            if n:
                return z.read(n).decode("utf-8")
    else:
        return open(path, encoding="utf-8").read()
    raise SystemExit("%s: no presets.ttl inside" % path)


def presets(ttl):
    """[(name, {index: value})] for every pset:Preset block, in the file's order"""
    out = []
    for block in re.split(r"\n(?=<[^>\n]+>\s+a\s+pset:Preset)", ttl):
        label = re.search(r'rdfs:label\s+"((?:[^"\\]|\\.)*)"', block)
        if not re.search(r"a\s+pset:Preset", block) or not label:
            continue
        name = label.group(1).replace('\\"', '"')
        vals, unknown = {}, []
        for sym, v in re.findall(r'lv2:symbol\s+"([^"]+)"\s*;\s*pset:value\s+([-+0-9.eE]+)', block):
            if sym in INDEX:
                vals[INDEX[sym]] = float(v)
            elif not IGNORED.fullmatch(sym):
                unknown.append(sym)
        if unknown:
            raise SystemExit("%s: not an OB-Xd 1.x preset (unknown parameters %s)" % (name, ", ".join(unknown)))
        missing = sorted(set(INDEX) - {s for s, i in INDEX.items() if i in vals})
        if missing:
            raise SystemExit("%s: parameters missing: %s" % (name, ", ".join(missing)))
        out.append((name, vals))
    return out


def fxb(programs):
    """an FXB bank chunk ('CcnK'/'FBCh', id 'Obxd') around JUCE's binary XML ('VC2!', length, text, NUL)"""
    rows = []
    for name, vals in programs:
        v = {**NEWER, **vals}
        rows.append("<program programName=%s isDefault=\"0\" voiceCount=\"32\" %s/>" % (
            quoteattr(name), " ".join('Val_%d="%s"' % (i, repr(float(v.get(i, 0.0)))) for i in range(COUNT))))
    xml = ('<?xml version="1.0" encoding="UTF-8"?> <discoDSP currentProgram="0" midiChannel="0"><programs>%s'
           '</programs></discoDSP>' % "".join(rows)).encode("utf-8")
    chunk = b"VC2!" + struct.pack("<I", len(xml)) + xml + b"\0"
    body = b"FBCh" + struct.pack(">I", 1) + b"Obxd" + struct.pack(">II", 1, len(programs)) + bytes(128) + \
        struct.pack(">I", len(chunk)) + chunk
    return b"CcnK" + struct.pack(">I", len(body)) + body


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__.split("\n\n")[0])
    src, dst = sys.argv[1:]
    progs = presets(read_ttl(src))
    if not progs:
        raise SystemExit("%s: no presets found" % src)
    progs.sort(key=lambda p: p[0].lower())   # as LV2 hosts list them
    if len(progs) > MAX_PROGRAMS:
        raise SystemExit("%d presets: the plugin reads %d per bank; split them over several .fxb files"
                         % (len(progs), MAX_PROGRAMS))
    data = fxb(progs)
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data)
    print("%d presets -> %s" % (len(progs), dst))


if __name__ == "__main__":
    main()
