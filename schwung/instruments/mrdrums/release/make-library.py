#!/usr/bin/env python3
"""Make this plugin's starter files into a folder (default library/), with the script that made them for
saustin2010/vst_instruments (original material, free to ship): python3 release/make-library.py [<folder>]"""
import os, subprocess, sys
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dest = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "library"))
subprocess.run([sys.executable, os.path.join(HERE, "release", 'make_starter_kit.py'), os.path.join(dest, 'kits/01_Starter')], check=True)
print("made", os.path.join(dest, 'kits/01_Starter'))
