"""Writes the MIDI Player port's demo file (steve/schwung-ports/midiplayer/data/MIDI/): a 4-bar Am-F-C-G loop, format 1,
one track each for chords, bass and a top line, so the TRACK selector has something to pick. Original material
(generated here), free to ship."""
import os
import sys
import struct

PPQ = 96
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "schwung-ports", "midiplayer", "data", "MIDI",
                   "Demo - Am F C G.mid")


def vlq(n):
    b = [n & 0x7F]
    while n > 0x7F:
        n >>= 7
        b.insert(0, (n & 0x7F) | 0x80)
    return bytes(b)


def track(name, notes, ch=0):
    """notes: (start_tick, length_ticks, note, velocity)"""
    ev = [(0, b"\xff\x03" + vlq(len(name)) + name.encode())]
    for t, ln, n, v in notes:
        ev.append((t, bytes([0x90 | ch, n, v])))
        ev.append((t + ln, bytes([0x80 | ch, n, 0])))
    ev.sort(key=lambda e: (e[0], e[1][0] & 0xF0 == 0x90))   # note-offs before note-ons at the same tick
    out, last = b"", 0
    for t, data in ev:
        out += vlq(t - last) + data
        last = t
    out += vlq(PPQ * 16 - last if PPQ * 16 > last else 0) + b"\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(out)) + out


BAR = PPQ * 4
CHORDS = [(57, [57, 60, 64]), (53, [53, 57, 60]), (48, [55, 60, 64]), (55, [55, 59, 62])]   # Am F C G (root, voicing)
chords, bass, top = [], [], []
for b, (root, voicing) in enumerate(CHORDS):
    for beat in range(4):
        t = b * BAR + beat * PPQ
        chords += [(t, PPQ // 2, n, 92 if beat % 2 == 0 else 74) for n in voicing]
        bass += [(t, PPQ // 2 - 8, root - 24, 100), (t + PPQ // 2, PPQ // 4, root - 12, 80)]
    top += [(b * BAR + PPQ * 2, PPQ, voicing[-1] + 12, 88), (b * BAR + PPQ * 3, PPQ // 2, voicing[1] + 12, 80)]
tempo = b"\x00\xff\x51\x03" + (500000).to_bytes(3, "big") + b"\x00\xff\x58\x04\x04\x02\x18\x08" + b"\x00\xff\x2f\x00"
data = (b"MThd" + struct.pack(">IHHH", 6, 1, 4, PPQ) + b"MTrk" + struct.pack(">I", len(tempo)) + tempo
        + track("Chords", chords) + track("Bass", bass) + track("Top", top))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "wb").write(data)
print("wrote", os.path.normpath(OUT), len(data), "bytes")
