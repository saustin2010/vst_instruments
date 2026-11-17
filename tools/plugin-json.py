#!/usr/bin/env python3
"""MPC OS 3.10.1's plugin.json for a plugin, from its plugin-list entry (deploy/pluginlist-entry.xml):
    python3 tools/plugin-json.py <pluginlist-entry.xml> <out dir> [<vst.json>]
writes <out dir>/plugin.json (name, manufacturer, uid, kind, ins/outs, file32: what the browser lists before loading it)
and <out dir>/VST_DATA: the data folder the plugin reads from a fixed /sdcard/vst/<name> (vst.json MODULE_DIR without
MODULE_SUBDIR), or nothing. Used by install.sh (and anything else that installs into VST2 Plugins)."""
import html, json, os, re, sys

entry, out = sys.argv[1], sys.argv[2]
a = {k: html.unescape(v) for k, v in re.findall(r'(\w+)="([^"]*)"', open(entry, encoding="utf-8").read())}
j = {"name": a["name"], "manufacturer": a["manufacturer"], "uid": "0x" + a["uid"].lower(), "format": "vst2",
     "kind": "instrument" if a.get("isInstrument", "1") == "1" else "effect"}
for k in ("version", "category"):
    if a.get(k):
        j[k] = a[k]
j.update(numInputs=int(a.get("numInputs") or 0), numOutputs=int(a.get("numOutputs") or 2),
         file32=os.path.basename(a["file"]))
with open(os.path.join(out, "plugin.json"), "w", encoding="utf-8") as f:
    f.write(json.dumps(j, indent=2, ensure_ascii=False) + "\n")

fixed = ""
if len(sys.argv) > 3 and os.path.exists(sys.argv[3]):
    v = json.load(open(sys.argv[3])).get("defines") or {}
    sub, mdir = str(v.get("MODULE_SUBDIR", "")).strip('"'), str(v.get("MODULE_DIR", "")).strip('"')
    if mdir and not sub:
        fixed = os.path.basename(mdir) + "\n"
with open(os.path.join(out, "VST_DATA"), "w") as f:
    f.write(fixed)
