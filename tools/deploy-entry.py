#!/usr/bin/env python3
"""Write a plugin's deploy/pluginlist-entry.xml for this repo's installer from the one the build made:
    tools/deploy-entry.py <plugin folder>
The release tools write the entry a release zip installs (file= next to its screen in /sdcard/Synths/, category Synth or
Effect). This repo installs the .so in /sdcard/vst/ (deploy/vst/ -> /sdcard/vst/), so file= is /sdcard/vst/<so>, and
the category is vst.json's own "category" when it has one (Sequencer, Drum Machine, ...), as the entries MPC lists
for these plugins already say. Everything else is the build's."""
import json, re, sys

folder = sys.argv[1].rstrip("/")
cfg = json.load(open(folder + "/vst.json"))
entry = open(folder + "/build/pluginlist-entry.xml").read()
entry, n = re.subn(r'\bfile="[^"]*"', 'file="/sdcard/vst/%s"' % cfg["so"], entry)
if n != 1:
    sys.exit("%s: no single file= in build/pluginlist-entry.xml" % folder)
if cfg.get("category"):
    entry, n = re.subn(r'\bcategory="[^"]*"', 'category="%s"' % cfg["category"], entry)
    if n != 1:
        sys.exit("%s: no single category= in build/pluginlist-entry.xml" % folder)
open(folder + "/deploy/pluginlist-entry.xml", "w").write(entry)
