"""Redraw a port's envelope displays and rewrite only their lines in layout.conf (steve/tools/stitch, 2026-10-02).
   python3 steve/tools/stitch/envelope_layout.py <port> [<port> ...]
Finds each envelope's `meter ... strip=images/stitch/env_<name>_*` lines (written by convert.py), keeps their place,
size and parameters, replaces them with the current scheme (A, then D/S/R per sustain band: see envelope.py) and
redraws the strips in the mpc-vst-html-art container. Every other line of the layout is left exactly as it was;
the old file is kept as layout.conf.bak-env."""
import json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEVE = os.path.dirname(os.path.dirname(HERE))
MV = os.path.dirname(STEVE)
# what Docker mounts: the framework checkout, or in a vst_instruments workspace (framework/mpc-vst-plugins inside
# the repo, plugins linked from the repo) the whole repo, so the links resolve in the container too
DOCKER_ROOT = (os.path.dirname(os.path.dirname(MV)) if os.path.basename(os.path.dirname(MV)) == "framework"
               and os.path.exists(os.path.join(os.path.dirname(MV), "setup.sh")) else MV)
FRAMES, BANDS = 32, 11
ENV = re.compile(r"^meter\s.*strip=images/stitch/env_(\w+?)_(a|s|d\d+|r\d+|s\d+)\.png")


def attrs(line):
    return dict(re.findall(r"(\w+)=(\S+)", line))


def lines_for(name, cols, keys, cy, cw, h):
    ka, kd, ks, kr = (list(keys) + [None])[:4]   # three keys: an ADS envelope, no release column
    m = lambda c, k, part, fr: "meter cx=%d cy=%d w=%d h=%d key=%s strip=images/stitch/env_%s_%s.png frames=%d" % (
        cols[c], cy, cw, h, k, name, part, fr)
    out = [m(0, ka, "a", FRAMES)]
    for b in range(BANDS):
        when = " when=%s:%d/%d" % (ks, b, BANDS)
        out += [m(1, kd, "d%d" % b, FRAMES) + when, m(2, ks, "s%d" % b, 4) + when] + \
            ([m(3, kr, "r%d" % b, FRAMES) + when] if kr else [])
    return out


def port(p):
    D = os.path.join(STEVE, "schwung-ports", p)
    lc = os.path.join(D, "layout.conf")
    src = open(lc).read().splitlines()
    theme = dict(re.findall(r"^theme_(\w+)=(\w+)", "\n".join(src), re.M))
    color = "#" + theme.get("accent_hi", theme.get("accent", "ffb85c"))
    envs, first = {}, {}
    for i, l in enumerate(src):
        mm = ENV.match(l)
        if not mm:
            continue
        name, part = mm.groups()
        a = attrs(l)
        e = envs.setdefault(name, {})
        first.setdefault(name, i)
        e.setdefault("cy", int(a["cy"])); e.setdefault("w", int(a["w"])); e.setdefault("h", int(a["h"]))
        slot = {"a": 0, "d": 1, "s": 2, "r": 3}[part[0]]
        e.setdefault("cols", {})[slot] = int(a["cx"])
        e.setdefault("keys", {})[slot] = a["key"]
    if not envs:
        print(p, "has no envelope displays"); return
    out_dir = os.path.join(D, "images", "stitch")
    new, done = [], set()
    for i, l in enumerate(src):
        mm = ENV.match(l)
        if mm:
            name = mm.group(1)
            if name not in done:
                e = envs[name]
                new += lines_for(name, [e["cols"][k] for k in range(4)], [e["keys"][k] for k in range(4)],
                                 e["cy"], e["w"], e["h"])
                done.add(name)
            continue
        new.append(l)
    for name, e in envs.items():
        for f in os.listdir(out_dir):   # old strips of this envelope (band counts change)
            if re.match(r"env_%s_(a|s|d\d+|r\d+|s\d+)\.png$" % name, f):
                os.remove(os.path.join(out_dir, f))
        spec = {"out": out_dir, "name": name, "w": e["w"] * 4, "h": e["h"], "color": color, "frames": FRAMES,
                "bands": BANDS}
        sj = os.path.join(D, "build", "env_%s.json" % name)
        os.makedirs(os.path.dirname(sj), exist_ok=True)
        json.dump(spec, open(sj, "w"))
        r = subprocess.run(["docker", "run", "--rm", "-v", "%s:%s" % (DOCKER_ROOT, DOCKER_ROOT), "-w", MV, "mpc-vst-html-art",
                            "python3", os.path.join(HERE, "envelope.py"), sj], capture_output=True, text=True)
        if r.returncode:
            sys.exit(r.stderr[-2000:])
    if not os.path.exists(lc + ".bak-env"):
        shutil.copy(lc, lc + ".bak-env")
    open(lc + ".new", "w").write("\n".join(new) + "\n")
    os.replace(lc + ".new", lc)
    print(p, ": ", ", ".join("%s (%d lines)" % (n, 1 + 3 * BANDS) for n in envs), sep="")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        port(p)
