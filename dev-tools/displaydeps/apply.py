#!/usr/bin/env python3
"""Add "refresh_display": true (or --field <name>: another flag) to parameters of a plugin's params.json, keeping the
file's layout (added right after each named entry's "key", on its own line when the entry spans lines):
    dev-tools/displaydeps/apply.py [--field <name>] <plugin folder> <key> [<key> ...]
Keys that already have it are left alone; an unknown key is an error. Nothing is reordered (params are append-only).
A plugin whose params.json is generated (originals/*/mpc/gen.py) needs the same change in its generator."""
import json, re, sys

args = sys.argv[1:]
field = "refresh_display"
if args[:1] == ["--field"]:
    field, args = args[1], args[2:]
folder, keys = args[0], args[1:]
path = folder.rstrip("/") + "/params.json"
text = open(path).read()
data = json.loads(text)
params = data["params"] if isinstance(data, dict) else data
have = {p["key"]: p for p in params}
for k in keys:
    if k not in have:
        sys.exit("%s: no parameter %r" % (path, k))
    if have[k].get(field):
        continue
    m = list(re.finditer(r'"key":\s*"%s"\s*,' % re.escape(k), text))
    if len(m) != 1:
        sys.exit("%s: %r found %d times" % (path, k, len(m)))
    m = m[0]
    line_start = text.rfind("\n", 0, m.start()) + 1
    rest = text[m.end():text.find("\n", m.end())]
    if rest.strip():   # the entry goes on along this line
        text = text[:m.end()] + ' "%s": true,' % field + text[m.end():]
    else:              # one key per line
        indent = text[line_start:m.start()]
        text = text[:m.end()] + "\n" + indent + '"%s": true,' % field + text[m.end():]
json.loads(text)
open(path, "w").write(text)
new = json.loads(text)
print("%s: %s on %d" % (path, field, sum(1 for p in (new["params"] if isinstance(new, dict) else new) if p.get(field))))
