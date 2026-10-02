"""Write a port's params.base.json from its engine's mpc_mi::Param table (mpc/<port>_engine.cc), so the VST parameter
list and the engine can't drift apart:  python3 steve/tools/mi/params_from_engine.py <port dir> <Name>"""
import json, os, re, sys

d, name = sys.argv[1], sys.argv[2]
src = open(next(os.path.join(d, "mpc", f) for f in os.listdir(os.path.join(d, "mpc")) if f.endswith("_engine.cc"))).read()
opts = {m.group(1): re.findall(r'"([^"]*)"', m.group(2))
        for m in re.finditer(r"const char \*const (k\w+)\[\] = \{(.*?)\};", src, re.S)}
ps = []
for m in re.finditer(r'\{"(\w+)", (-?[\d.]+)f?, (-?[\d.]+)f?, (-?[\d.]+)f?, (\w+), (\d+), (true|false)\}', src):
    key, lo, hi, de, op, n, integer = m.groups()
    p = {"key": key, "name": key.upper().replace("_", " ")}
    if op != "0":
        p.update(options=opts[op], default=int(float(de)))
    elif integer == "true":
        p.update(min=int(float(lo)), max=int(float(hi)), default=int(float(de)), display="int")
    else:
        p.update(min=float(lo), max=float(hi), default=float(de))
    ps.append(p)
json.dump({"name": name, "params": ps}, open(os.path.join(d, "params.base.json"), "w"), indent=1)
print("%s: %d params: %s" % (name, len(ps), " ".join(p["key"] for p in ps)))
