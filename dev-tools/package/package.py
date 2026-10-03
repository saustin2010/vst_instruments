"""Assemble the standalone vst_instruments repo from this working folder:
    python3 steve/tools/package/package.py [<target dir>] [--no-shots]      (default target: ~/my_code/vst_instruments)
Everything in the target except .git is rewritten, so it can be re-run after any change here. What goes in:
  <group>/<port>/      each port from steve/schwung-ports (catalog.py says which group): sources, design inputs, the
                       ready deploy/ payload, screenshots/ (showcase.py), its Stitch design (design/), a README.md
                       written for musicians, and upstream-changes.diff (from bak-upstream/) instead of the originals
  install.sh, uninstall.sh, tools/   the installer (steve/tools/package/repo/, our own code)
  framework/           our changes to sd88me/mpc-vst-plugins as one patch + setup.sh (his files are not copied)
  dev-tools/           steve/tools: the reskin pipeline, adapters' sources, fuzz/probe/screengrab helpers
  README.md, INSTALL.md, BUILDING.md, RESKINNING.md, CREDITS.md, docs/, licenses/
Left out: build output, logs, backups, PRIVATE/, firmware/, plugins/ (sd88me's releases), euclidier/, chiptune (doesn't
link), native_instruments/. Screenshots need the mpc-vst-html-art image and build/state.json (dump_state.sh)."""
import datetime, glob, json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEVE = os.path.dirname(os.path.dirname(HERE))
MV = os.path.dirname(STEVE)
PORTS = os.path.join(STEVE, "schwung-ports")
SL = os.path.join(STEVE, "stitch_layouts")
SHOTS = os.path.join(SL, "build", "showcase")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(MV, "tools"))
from catalog import PORTS as CAT  # noqa: E402
import shadow_skin  # noqa: E402

FRAMEWORK_URL = "https://github.com/sd88me/mpc-vst-plugins"
TODAY = datetime.date.today().isoformat()
SKIP_NAMES = {"build", "logs", "preview", "scope", "__pycache__", ".DS_Store", "bak-upstream", "README.md"}
SKIP_RE = re.compile(r"^(bak-.*|.*\.log|.*\.bak-.*|layout\.conf\.bak.*)$")
GROUP_TITLES = {
    "schwung/instruments": "Schwung modules: instruments",
    "schwung/sequencers": "Schwung modules: MIDI sequencers",
    "schwung/effects": "Schwung modules: audio effects",
    "mutable-instruments": "Mutable Instruments (from Mutable's own firmware source)",
    "vcv-rack": "VCV Rack modules",
}
# what the catalogue shows, by what a plugin does (a port's folder says how it was ported)
SECTIONS = [("Synths", ("Synth",)), ("Drum machines", ("Drum synth", "Drum sampler")),
            ("MIDI sequencers and generators", ("MIDI sequencer", "Arpeggiator", "Modulator")),
            ("Audio effects", ("Audio effect",))]
SEQ_KINDS = ("MIDI sequencer", "Arpeggiator", "Modulator")


def sh(*cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


def licence_of(path):
    if not os.path.exists(path):
        return None
    t = open(path, errors="replace").read()
    if "GNU GENERAL PUBLIC LICENSE" in t:
        return "GPL-3.0" if "Version 3" in t else "GPL-2.0"
    if "Permission is hereby granted" in t:
        return "MIT"
    if "Redistribution and use in source and binary forms" in t:
        return "BSD-3-Clause" if "Neither the name" in t else "BSD"
    return None


def port_facts(port):
    d = os.path.join(PORTS, port)
    v = json.load(open(os.path.join(d, "vst.json")))
    params = [p for p in json.load(open(os.path.join(d, "params.json")))["params"] if not p["key"].endswith("__open")]
    mod = {}
    if os.path.exists(os.path.join(d, "src", "module.json")):
        mod = json.load(open(os.path.join(d, "src", "module.json")))
    up = open(os.path.join(d, "UPSTREAM")).read().splitlines() if os.path.exists(os.path.join(d, "UPSTREAM")) else []
    lic = None
    for f in ("LICENSE", "LICENSE.md", "src/LICENSE"):
        lic = lic or licence_of(os.path.join(d, f))
    if not lic and os.path.exists(os.path.join(d, "LICENSE")) and "Emilie Gillet" in open(os.path.join(d, "LICENSE")).read():
        lic = "MIT"   # Mutable's eurorack code: the MIT notice in each file, LICENSE holds the header
    lic = lic or mod.get("license") or "not stated upstream"
    entry = open(os.path.join(d, "deploy", "pluginlist-entry.xml")).read()
    attr = lambda k: (re.search(r' %s="([^"]*)"' % k, entry) or [None, ""])[1]
    md = v.get("defines", {}).get("MODULE_DIR", "").strip('"')
    lay, _ = shadow_skin.parse_layout(os.path.join(d, "layout.conf"))
    return dict(d=d, v=v, params=params, mod=mod, up=up, lic=lic, name=attr("name"), maker=attr("manufacturer"),
                category=attr("category"), so=attr("file"), data=os.path.basename(md) if md else None, layout=lay,
                skin="%s - VST - %s" % (attr("manufacturer"), attr("name")))


# libraries (presets, kits, wavetables, patches) live in the repo's presets/<port>/, which git ignores
# (tools/fetch-presets.py gets them from their projects): left out of the plugin folders and their deploy/
LIBS = {"braids": ["src/presets"], "hera": ["src/presets"], "obxd": ["src/presets"],
        "libpo32": ["src/kits", "src/presets"], "mrdrums": ["src/kits"], "tablor": ["src/wavetables", "src/presets"],
        "groovebank": ["src/patterns"], "midiplayer": ["data"],
        "helm": ["src/dsp/helm/patches/Factory Presets", "src/data"]}


def copy_port(port, dst):
    src = os.path.join(PORTS, port)
    os.makedirs(dst)
    libs = {os.path.join(src, x) for x in LIBS.get(port, [])}
    libs |= {d.rstrip("/") for d in glob.glob(os.path.join(src, "deploy", "vst", "*/"))}
    junk = shutil.ignore_patterns("__pycache__", ".DS_Store", "*.o", "*.pyc")

    def ignore(d, names):
        return set(junk(d, names)) | {n for n in names if os.path.join(d, n) in libs}

    for n in sorted(os.listdir(src)):
        if n in SKIP_NAMES or SKIP_RE.match(n) or os.path.join(src, n) in libs:
            continue
        s = os.path.join(src, n)
        if os.path.isdir(s):
            shutil.copytree(s, os.path.join(dst, n), symlinks=True, ignore=ignore)
        else:
            shutil.copy2(s, os.path.join(dst, n))
    # its libraries, laid out as on the MPC (/sdcard/vst/<port>/), into presets/<port>/
    data = os.path.join(src, "deploy", "vst", port)
    if os.path.isdir(data):
        shutil.copytree(data, os.path.join(OUT, "presets", port), ignore=junk)
    if port == "tablor":   # its factory presets: no control for them yet, so not copied to the MPC
        os.makedirs(os.path.join(OUT, "presets", port, "not-installed"), exist_ok=True)
        shutil.copy2(os.path.join(src, "src", "presets", "factory.tbl"),
                     os.path.join(OUT, "presets", port, "not-installed", "factory.tbl"))
    # the Stitch design the screen was converted from
    dd = os.path.join(dst, "design")
    if os.path.exists(os.path.join(SL, port + ".md")):
        os.makedirs(dd, exist_ok=True)
        shutil.copy2(os.path.join(SL, port + ".md"), os.path.join(dd, "stitch.html"))
    for md in glob.glob(os.path.join(dst, "*.md")):   # notes copied from the working folder: repo-relative tool paths
        t = open(md).read()
        if "steve/" in t:
            open(md, "w").write(sanitize(t))
    # local changes to upstream files as one diff, not the originals
    bak = os.path.join(src, "bak-upstream")
    if os.path.isdir(bak):
        diffs = []
        for f in sorted(os.listdir(bak)):
            name = f[len("stmlib-"):] if f.startswith("stmlib-") else f
            cands = [c for c in glob.glob(os.path.join(src, "**", name), recursive=True)
                     if "/build/" not in c and "/deploy/" not in c and "/bak-" not in c]
            if f.startswith("stmlib-"):
                cands = [c for c in cands if "/stmlib/dsp/" in c] or cands
            if len(cands) != 1:
                raise SystemExit("%s: can't tell which file bak-upstream/%s is the original of: %s" % (port, f, cands))
            rel = os.path.relpath(cands[0], src)
            r = subprocess.run(["diff", "-u", "--label", "a/" + rel, "--label", "b/" + rel, os.path.join(bak, f), cands[0]],
                               capture_output=True, text=True)
            if r.returncode == 1:
                diffs.append(r.stdout)
        if diffs:
            open(os.path.join(dst, "upstream-changes.diff"), "w").write(
                "# Local changes to the upstream source in src/ (or mpc/), as unified diffs against the files as they\n"
                "# were vendored. Each is described in README.md under \"Changes for the MPC\".\n" + "".join(diffs))


def sanitize(t):
    t = t.replace("steve/tools/", "dev-tools/")
    t = t.replace("`steve/patches/`", "`framework/`").replace("steve/patches/", "framework/")
    t = re.sub(r"Originals?(?: of every patched file)?: `?(?:\.\./\w+/)?bak-upstream/[^`\s]*`?\.?", "Diff: `upstream-changes.diff`.", t)
    t = re.sub(r"[Oo]riginal in `?bak-upstream/?[\w.-]*`?", "diff in `upstream-changes.diff`", t)
    t = re.sub(r"`?bak-upstream/?[\w.-]*`?", "`upstream-changes.diff`", t)
    t = re.sub(r" ?\((?:second install|installed) 2026-10-0\d\)", "", t)
    t = t.replace("steve/stitch_layouts/", "design/").replace("steve/", "")
    t = re.sub(r" ?\(see the build\.sh data arguments in its README\)", "", t)
    return t


EXTRA_CHANGES = {
    "hera": ["**VOLUME restored (2026-10-03)**: the engine saved VOLUME in its state but never read it back, so a project "
             "reopened at the default level. `src/dsp/hera_plugin.cpp` now reads it."],
    "grids": ["Reports itself to MPC as a **Sequencer** (vst.json `\"category\"`), so the plugin browser can group it "
              "with sequencers instead of synths. Not yet checked on a device; the other sequencers still report Synth."],
}

# bullets about the screens these ports had before the Stitch designs (replaced, so no longer true)
OLD_SCREEN = re.compile(r"^(New .*screen|Minimoog-style look|Hand-designed|Six pages .*theme|The 47 algorithms open|"
                        r"Screen from the Stitch mockup)")


def old_sections(port):
    """The hand-written "Local changes" / "What changed" bullets from the working README, made repo-relative."""
    t = open(os.path.join(PORTS, port, "README.md")).read()
    out = []
    for head in ("Local changes", "What changed"):
        m = re.search(r"^## %s\n(.*?)(?=^## |\Z)" % re.escape(head), t, re.S | re.M)
        if m:
            for b in re.split(r"\n(?=- )", m.group(1).strip()):
                b = " ".join(b.split())
                if b.startswith("- ") and "Q-Links on stepped controls" not in b and not OLD_SCREEN.match(b[2:]):
                    out.append(sanitize(b[2:]))
    out.append("New touchscreen page from its Google Stitch design (`design/`): the design's artwork as the background, "
               "its own knob art, live names and values, and controls the design left out added in its style "
               "(see RESKINNING.md).")
    return out


def page_lines(f):
    names = {p["key"]: p.get("name") or p["key"] for p in f["params"]}
    L = []
    for i, t in enumerate(f["layout"]):
        L += ["### %d. %s" % (i + 1, t["name"]), ""]
        shot = "screenshots/page_%d.png" % i
        L += ['<img src="%s" width="760" alt="%s, page %s">' % (shot, f["name"], t["name"]), ""]
        if t["qlinks"]:
            for b, (bank, keys) in enumerate(t["qlinks"]):
                cols = [keys[k:k + 4] for k in range(0, len(keys), 4)]
                pre = "Q-Link columns" + (" (bank %d)" % (b + 1) if len(t["qlinks"]) > 1 else "")
                L.append("%s: %s" % (pre, "  ·  ".join("**%d** %s" % (c + 1, ", ".join(names.get(k, k) for k in col))
                                                       for c, col in enumerate(cols))))
                L.append("")
    return L


def playing(f, c):
    n = f["name"]
    if c["kind"] in SEQ_KINDS:
        notes_in = f["v"]["name"] not in ("Grids", "Marbles", "MIDI Player", "Maze Lite")
        L = ["MPC OS ignores a plugin's own MIDI output, so %s opens its own MIDI port (named **%s**, port "
             "**MIDI Out**), the way a USB MIDI device would appear. MPC picks the port up without a restart:" % (n, n), "",
             ("1. Put %s on a plugin track and play or hold notes into it (pads, keys or a MIDI clip)." % n) if notes_in else
             "1. Put %s on a plugin track. It needs no notes: it plays when MPC's transport runs." % n,
             "2. **Menu → Preferences → MIDI**: switch **Track** on for the %s port." % n]
        if c["kind"] == "Modulator":
            L += ["3. On the track to modulate, set **MIDI input** to that port, then MIDI-learn the parameter you want "
                  "moved to CC OUT A or CC OUT B (MIDI page). MIN and MAX send too when given a CC number.",
                  "4. EOC NOTES sends a short note at the end of each cycle, e.g. to fire a drum pad on another track."]
        else:
            L += ["3. On the track(s) that should play, set **MIDI input** to that port (not *All*, and not on %s's own "
                  "track, or it hears itself)." % n,
                  "4. Press play: it follows MPC's tempo and transport (MIDI clock from the plugin host)."]
        if f["v"]["name"] == "Maze Lite":
            L += ["", "A note you play on its track sets the key (the root its patterns spread around)."]
        L += ["", "%s makes no sound of its own%s." % (n, " unless AUDIO is on" if c["kind"] == "Modulator" else "")]
        return L
    if c["kind"] == "Audio effect":
        return ["An audio effect: insert it in a track's or program's insert effects (it reports two inputs and "
                "outputs and the Effect category). It processes 128-sample blocks, about 3 ms of latency at 44.1 kHz.",
                "", "**Not yet tried on a device:** whether MPC OS offers third-party VST effects in its insert list. "
                "The plugin builds and passes the offline test with audio in; please report what you find."]
    L = ["Add it to a **plugin track** and play it from the pads, a keyboard or a MIDI clip. Every control can be "
         "turned with the Q-Links and automated, and the settings are saved with your project."]
    if c["kind"] in ("Drum synth", "Drum sampler"):
        L += ["", "Its 16 sounds sit on MIDI notes 36-51, one per pad."]
    if f["v"].get("programs"):
        L += ["", "Its presets also appear in MPC's own **PRESET** menu in the plugin header."]
    return L


def write_port_readme(port, f, c, dst):
    lic, mod, up = f["lic"], f["mod"], f["up"]
    top = "../" * (dst.count("/") + 1)   # from the port's folder to the top of the repo
    L = ["# %s" % f["name"], "",
         "**%s** · %s · maker in MPC: %s · licence: %s" % (c["kind"], c["tagline"], f["maker"], lic), "",
         '<img src="screenshots/page_0.png" width="760" alt="%s on the MPC touchscreen">' % f["name"], "",
         " ".join(c["about"].split()), ""]
    L += ["## On the MPC", "",
          "- In the plugin browser: **%s** by **%s** (%s)" % (f["name"], f["maker"], f["category"] or "Synth"),
          "- Files: `%s`%s, screen in `/sdcard/Synths/%s/`" % (
              f["so"], (", presets/data in `/sdcard/vst/%s/` (from this repo's `presets/%s/`, which "
                        "`tools/fetch-presets.py` fills; install.sh does that for you)" % (f["data"], port))
              if os.path.isdir(os.path.join(OUT, "presets", port)) else "", f["skin"]),
          "- %d parameters (all automatable) on %d page%s" % (
              len(f["params"]), len(f["layout"]), "" if len(f["layout"]) == 1 else "s"), ""]
    L += ["## Playing it", ""] + playing(f, c) + [""]
    L += ["## Pages", "",
          "Screenshots are rendered from the built skin with the engine's real values right after it's inserted. "
          "On the MPC each page is a tab under the plugin header. The Q-Links follow the page column by column: on a "
          "4-knob MPC the Q-Link button steps through the columns, and MPC outlines the active one.", ""]
    L += page_lines(f)
    L += ["## Install", "", "From the top of this repo (see [INSTALL.md](%sINSTALL.md)):" % top, "",
          "```", "./install.sh <mpc-address> %s" % port, "```", ""]
    # credits
    L += ["## Where it comes from", ""]
    if up:
        L.append("- Upstream: %s" % up[0].strip())
        if len(up) > 1 and up[1].startswith("commit"):
            L.append("- Vendored at %s" % up[1].strip())
    if c.get("origin"):
        L.append("- %s" % c["origin"])
    if mod.get("author") and not c.get("origin"):
        L.append("- Schwung module \"%s\" v%s by %s" % (mod.get("name", port), mod.get("version", "?"), mod["author"]))
    lic_files = [n for n in ("LICENSE", "LICENSE.md", "LICENSE-GPLv3.txt", "NOTICE", "THIRD_PARTY_LICENSES")
                 if os.path.exists(os.path.join(f["d"], n))]
    if lic_files:
        L.append("- Licence: %s (%s)" % (lic, ", ".join("[`%s`](%s)" % (n, n) for n in lic_files)))
    elif lic.startswith("GPL") or lic == "MIT":
        L.append("- Licence: %s, as declared in the module's `src/module.json` (text: [licenses/](%slicenses/))" % (lic, top))
    else:
        L.append("- Licence: **not stated upstream**. The source is included as published by its author; ask them "
                 "before reusing it elsewhere.")
    L.append("- MPC port and screen: this repo, built on [sd88me's mpc-vst-plugins](%s) (wrapper, Schwung adapter, "
             "skin tools)." % FRAMEWORK_URL)
    L.append("")
    ch = old_sections(port) + EXTRA_CHANGES.get(port, [])
    if os.path.exists(os.path.join(OUT, dst, "upstream-changes.diff")) and not any("upstream-changes.diff" in x for x in ch):
        ch.append("Every change to upstream files is in `upstream-changes.diff`.")
    if ch:
        L += ["## Changes for the MPC", ""] + ["- " + x for x in ch] + [""]
    # files
    rows = [("README.md", "this page"), ("screenshots/", "the pages as MPC draws them"),
            ("deploy/", "ready to install: `vst/` → `/sdcard/vst/`, `Synths/` → `/sdcard/Synths/`, plus the plugin-list "
                        "entry (its presets/kits are in the repo's `presets/` folder, not here)"),
            ("vst.json", "build settings: name, maker, sources, compiler flags"),
            ("params.json", "the plugin's parameters as MPC sees them (VST index = order)"),
            ("params.base.json", "the engine's own parameter list it was derived from"),
            ("params.pre-stitch.json", "the parameter list before the Stitch screen renamed controls"),
            ("chain_params.engine.json", "what the engine reports it takes (its `chain_params`)"),
            ("layout.conf", "the screen: control positions, art, Q-Links (generated from the Stitch design)"),
            ("layout.grid.conf", "the plan of pages and controls the Stitch conversion fills in"),
            ("%s.css" % port, "the artwork stylesheet"), ("images/", "artwork: backgrounds, knobs, displays"),
            ("design/", "the Google Stitch design this screen was converted from (`stitch.html`, as Stitch wrote it)"),
            ("src/", "the engine's source, vendored from upstream"),
            ("mpc/", "MPC-side glue: the engine wrapper or MIDI/audio adapter, and headers"),
            ("include/", "extra headers the build needs"), ("data/", "data shipped to the MPC (e.g. demo files)"),
            ("upstream-changes.diff", "every local change to the upstream source"),
            ("UPSTREAM", "where the source came from, and at which commit"),
            ("VENDORED.md", "notes on the vendored source and its patches")]
    L += ["## Files", "", "| Path | What |", "|---|---|"]
    for p, what in rows:
        if os.path.exists(os.path.join(OUT, dst, p.rstrip("/"))):
            L.append("| `%s` | %s |" % (p, what))
    L += ["", "To rebuild from source see [BUILDING.md](%sBUILDING.md); to change the screen, [RESKINNING.md](%sRESKINNING.md)." % (top, top), ""]
    open(os.path.join(OUT, dst, "README.md"), "w").write("\n".join(L))


def shots(port, out_dir):
    src = os.path.join(SHOTS, port)
    if "--no-shots" not in sys.argv or not os.path.isdir(src):
        r = subprocess.run(["docker", "run", "--rm", "-v", "%s:%s" % (MV, MV), "-w", MV, "mpc-vst-html-art", "python3",
                            os.path.join(STEVE, "tools", "stitch", "showcase.py"), port, src], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit("%s: showcase failed\n%s" % (port, r.stderr[-2000:]))
    shutil.copytree(src, out_dir)


def framework_patch(dst):
    os.makedirs(dst)
    base = sh("git", "-C", MV, "rev-parse", "HEAD").strip()
    diff = sh("git", "-C", MV, "diff", base, "--", ".", ":(exclude)steve")
    for f in sh("git", "-C", MV, "ls-files", "--others", "--exclude-standard").split():
        if f.startswith("steve/") or os.path.basename(f) == ".DS_Store":
            continue
        r = subprocess.run(["git", "-C", MV, "diff", "--no-index", "--", "/dev/null", f], capture_output=True, text=True)
        diff += r.stdout
    open(os.path.join(dst, "mpc-vst-plugins.patch"), "w").write(diff)
    for n in ("setup.sh", "README.md"):
        s = open(os.path.join(HERE, "repo", "framework", n)).read().replace("@BASE@", base)
        open(os.path.join(dst, n), "w").write(s)
    os.chmod(os.path.join(dst, "setup.sh"), 0o755)
    return base


def dev_tools(dst):
    src = os.path.join(STEVE, "tools")
    keep = ["stitch", "skin-redesign", "midifx", "midiout", "audiofx", "mi", "rack", "fuzz", "probe", "screengrab", "package"]
    os.makedirs(dst)
    for k in keep:
        shutil.copytree(os.path.join(src, k), os.path.join(dst, k), symlinks=True,
                        ignore=shutil.ignore_patterns("__pycache__", ".DS_Store", "*.pyc", "build", "*.o"))
    for n in ("README.md", "workspace.sh"):
        shutil.copy2(os.path.join(HERE, "repo", "dev-tools", n), os.path.join(dst, n))
    shutil.copytree(os.path.join(HERE, "repo", "dev-tools", "fake-mpc"), os.path.join(dst, "fake-mpc"),
                    ignore=shutil.ignore_patterns("state", ".DS_Store"))
    for f in glob.glob(os.path.join(dst, "**", "*.sh"), recursive=True):   # no personal key name as a default
        t = open(f).read()
        t2 = re.sub(r'-i "\$\{MPC_KEY:-[^}]*\}"', '${MPC_KEY:+-i "$MPC_KEY"}', t)
        if t2 != t:
            open(f, "w").write(t2)


def main():
    global OUT
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    OUT = os.path.abspath(os.path.expanduser(args[0] if args else "~/my_code/vst_instruments"))
    os.makedirs(OUT, exist_ok=True)
    # since 2026-10-03 the repo is developed in place: never overwrite one with history unless told to
    if os.path.isdir(os.path.join(OUT, ".git")) and "--force" not in sys.argv and subprocess.run(
            ["git", "-C", OUT, "rev-parse", "-q", "--verify", "HEAD"], capture_output=True).returncode == 0:
        raise SystemExit("%s has commits: it's developed in place now. Re-run with --force to overwrite it anyway "
                         "(everything but .git), or give another target." % OUT)
    for n in os.listdir(OUT):
        if n != ".git":
            p = os.path.join(OUT, n)
            shutil.rmtree(p) if os.path.isdir(p) and not os.path.islink(p) else os.remove(p)
    only = [a.split("=", 1)[1].split(",") for a in sys.argv if a.startswith("--only=")]   # a trial run on a few
    facts = {}
    for port, c in sorted(CAT.items()):
        if only and port not in only[0]:
            continue
        dst = "%s/%s" % (c["group"], port)
        print("  %-26s" % dst, end=" ", flush=True)
        copy_port(port, os.path.join(OUT, dst))
        shots(port, os.path.join(OUT, dst, "screenshots"))
        f = facts[port] = port_facts(port)
        write_port_readme(port, f, c, dst)
        print("%-12s %s" % (f["lic"], f["name"]))
    # static files: installer, docs templates
    R = os.path.join(HERE, "repo")
    for n in ("install.sh", "uninstall.sh", ".gitignore", "CLAUDE.md", "ROADMAP.md"):
        shutil.copy2(os.path.join(R, n), os.path.join(OUT, n))
    os.makedirs(os.path.join(OUT, "presets"), exist_ok=True)
    shutil.copy2(os.path.join(R, "presets", "README.md"), os.path.join(OUT, "presets", "README.md"))
    shutil.copytree(os.path.join(R, "tools"), os.path.join(OUT, "tools"))
    shutil.copytree(os.path.join(R, "licenses"), os.path.join(OUT, "licenses"))
    shutil.copytree(os.path.join(R, "docs"), os.path.join(OUT, "docs"))
    base = framework_patch(os.path.join(OUT, "framework"))
    dev_tools(os.path.join(OUT, "dev-tools"))
    import docs   # noqa: E402  (the top-level documents, from the facts gathered above)
    docs.write(OUT, CAT, facts, base, GROUP_TITLES, SECTIONS)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
