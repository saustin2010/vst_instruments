"""Synthesise mrdrums' bundled "Starter" kit (8 original drum hits, 44.1 kHz mono 16-bit) -- no samples from
anywhere else, so it can ship. Usage: python3 make_starter_kit.py <out dir>"""
import math, os, random, struct, sys, wave

SR = 44100
random.seed(32)


def write(path, xs):
    peak = max(1e-9, max(abs(x) for x in xs))
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, x / peak * 0.89)) * 32767)) for x in xs))


def env(n, decay, attack=0.0015):
    a = int(attack * SR)
    return [(i / a if i < a else 1.0) * math.exp(-(i - a) / (decay * SR) if i >= a else 0) for i in range(n)]


def noise(n):
    return [random.uniform(-1, 1) for _ in range(n)]


def hp(xs, c):   # one-pole high-pass
    out, y, px = [], 0.0, 0.0
    for x in xs:
        y = c * (y + x - px); px = x; out.append(y)
    return out


def bp(xs, f, q=4.0):   # state-variable band-pass
    out, low, band = [], 0.0, 0.0
    k = 2 * math.sin(math.pi * f / SR)
    for x in xs:
        low += k * band; high = x - low - band / q; band += k * high; out.append(band)
    return out


def tone(n, f0, f1, sweep):
    ph, out = 0.0, []
    for i in range(n):
        f = f1 + (f0 - f1) * math.exp(-i / (sweep * SR)); ph += 2 * math.pi * f / SR; out.append(math.sin(ph))
    return out


def mix(*parts):
    return [sum(p[i] * g for p, g in parts) for i in range(len(parts[0][0]))]


out = sys.argv[1]
os.makedirs(out, exist_ok=True)
N = lambda s: int(s * SR)
kick = [a * b for a, b in zip(tone(N(0.5), 160, 44, 0.035), env(N(0.5), 0.18))]
snare = [a * b for a, b in zip(mix((tone(N(0.3), 210, 180, 0.02), 0.55), (hp(noise(N(0.3)), 0.92), 0.8)), env(N(0.3), 0.075))]
chh = [a * b for a, b in zip(hp(hp(noise(N(0.12)), 0.82), 0.82), env(N(0.12), 0.018))]
ohh = [a * b for a, b in zip(hp(hp(noise(N(0.6)), 0.82), 0.82), env(N(0.6), 0.16))]
cn = bp(noise(N(0.4)), 1300, 3)
clap = [x * (sum(math.exp(-max(0, i - o) / (0.006 * SR)) * (i >= o) for o in (0, N(0.011), N(0.022))) + 0.6 * env(N(0.4), 0.09)[i] * (i >= N(0.03)))
        for i, x in enumerate(cn)]
tlo = [a * b for a, b in zip(tone(N(0.5), 125, 82, 0.05), env(N(0.5), 0.16))]
thi = [a * b for a, b in zip(tone(N(0.45), 215, 150, 0.04), env(N(0.45), 0.13))]
rim = [a * b for a, b in zip(mix((tone(N(0.08), 1700, 1650, 0.01), 0.7), (bp(noise(N(0.08)), 3000, 5), 0.6)), env(N(0.08), 0.012, 0.0005))]
for name, xs in (("01_kick", kick), ("02_snare", snare), ("03_closed_hat", chh), ("04_open_hat", ohh),
                 ("05_clap", clap), ("06_tom_low", tlo), ("07_tom_high", thi), ("08_rim", rim)):
    write(os.path.join(out, name + ".wav"), xs)
print("wrote 8 hits to", out)
