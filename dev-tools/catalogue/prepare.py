#!/usr/bin/env python3
"""Make a plugin folder ready for its own repo (docs/catalogue-migration.md, the per-plugin checklist), writing what the
pilots (303, Hera) got by hand, from the facts in PLUGINS below:
    python3 dev-tools/catalogue/prepare.py <plugin> [<plugin> ...]      (--ready: every plugin in PLUGINS)
For each plugin:
- .gitignore, and a LICENSE when the folder has none: the upstream repo's own file, or the licence text in licenses/;
- .github/workflows/release.yml, pinned to the release tools (framework/setup-release.sh's REF);
- its library: a tools/fetch-presets.py source moves to release/library.json with release/fetch-library.py (Hera's), a
  library made by this repo's own script gets that script in release/ instead; MODULE_SUBDIR beside MODULE_DIR, so the
  data is found next to the .so;
- README sections for a reader of the plugin repo: the files on the MPC, Install, Building and Development, the new
  files in the table, and absolute links into this repo.
- FRAMEWORK.md: which of the fork's changes to sd88me's tools the plugin uses, what each does there, what happens without
  it, and what to change once sd88me has them (--framework-doc [<plugin> ... | --all]: only that).
Run it again after changing PLUGINS or the pin: it rewrites the same things. Then dev-tools/catalogue/check.sh <plugin>."""
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VI = "https://github.com/saustin2010/vst_instruments"
FORK = "https://github.com/saustin2010/mpc-vst-plugins"

# folder name: (catalogue id, SPDX licence, LICENSE source when the folder has none (owner/repo on GitHub, or a text in
# licenses/), folders in the plugin folder where the user adds files (kept by the installer))
PLUGINS = {
    "303": ("open303", "GPL-3.0-only", "GPL-3.0", []),
    "hera": ("hera", "GPL-3.0-only", "GPL-3.0", []),
    "aphex": ("aphex", "MIT", None, []),
    "braids": ("braids", "MIT", "charlesvestal/schwung-braids", []),
    "chordism": ("chordism", "MIT", None, []),
    "denis": ("denis", "MIT", None, []),
    "elements": ("elements", "MIT", None, []),
    "fizzik": ("fizzik", "MIT", None, []),
    "monksynth": ("monksynth", "MIT", None, []),
    "monovoice": ("mono-voice", "MIT", None, []),
    "moog": ("raffosynth", "MIT", "charlesvestal/schwung-moog", []),
    "mrdrums": ("mr-drums", "MIT", "handcraftedcc/schwung-mrdrums", ["mrdrums/kits"]),
    "mrhyde": ("mr-hyde", "MIT", "handcraftedcc/schwung-mrhyde", []),
    "noisemaker": ("noisemaker", "GPL-2.0-only", "GPL-2.0", ["noisemaker/presets"]),
    "nusaw": ("nusaw", "MIT", None, []),
    "obxd": ("obxd", "GPL-3.0-only", "GPL-3.0", ["obxd/presets"]),
    "wurl": ("wurl", "GPL-3.0-only", None, []),
    "grids": ("grids", "GPL-3.0-only", None, []),
    "groovebank": ("groove-bank", "MIT", None, []),
    "midiplayer": ("midi-player", "MIT", None, ["midiplayer/MIDI"]),
    "pixelwalkers": ("pixel-walkers", "MIT", None, []),
    "rampage": ("rampage", "GPL-3.0-or-later", None, []),
    "superarp": ("super-arp", "MIT", None, []),
    "verglas": ("verglas", "MIT", None, []),
    "warps": ("warps", "MIT", None, []),
    "eucalypso": ("eucalypso", "MIT", None, []),
    "rings": ("rings", "MIT", None, []),
    "ringsfx": ("rings-fx", "MIT", None, []),
    "hank": ("hank", "MIT", None, []),   # LICENSE written here: upstream declares MIT (README, module.json), no file
    "tablor": ("tablor", "BSD-3-Clause", None, ["tablor/wavetables"]),
    "stevequencer": ("stevequencer", "MIT", None, []),   # the owner's own, MIT chosen 2026-10-08
    "stevequencer16": ("stevequencer-16", "MIT", None, []),
    "percolator": ("percolator", "MIT", None, ["percolator"]),   # the owner's own (2026-10-10); users add kit packs
}
# releases that ship this repo's own skin (deploy/Synths, built by framework/setup.sh's tools) instead of rebuilding it
# with sd88me's: his skin builder lays out their per-sub-page controls (banks=) and readouts differently
OWN_SKIN = {"stevequencer", "stevequencer16", "percolator"}   # percolator: its readouts (vs=, ink=)
PILOTS = {"303", "hera"}   # done by hand and published first: --ready leaves them alone
# libraries this repo makes with its own scripts (tools/fetch-presets.py MADE): the script, and where its output goes
MADE = {
    "mrdrums": ("dev-tools/skin-redesign/make_starter_kit.py", "kits/01_Starter"),
    "midiplayer": ("dev-tools/midifx/make_demo_mid.py", "MIDI/Demo - Am F C G.mid"),
}


def sh(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True, cwd=REPO).stdout


def folder_of(name):
    dirs = sh("bash", "-c", ". tools/common.sh; plugin_dirs").split()
    hits = [d for d in dirs if os.path.basename(d) == name]
    if len(hits) != 1:
        raise SystemExit("%s: no single plugin folder (%s)" % (name, hits))
    return hits[0]


def release_ref():
    m = re.search(r"^REF=([0-9a-f]{40})", open(os.path.join(REPO, "framework", "setup-release.sh")).read(), re.M)
    return m.group(1)


def fetch_sources(name):
    """tools/fetch-presets.py's sources for this plugin (from its table or its release/library.json)."""
    import runpy
    g = runpy.run_path(os.path.join(REPO, "tools", "fetch-presets.py"), run_name="prepare")
    return g["SOURCES"].get(name)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if not os.path.exists(path) or open(path).read() != text:
        open(path, "w").write(text)
        return True
    return False


def about_of(readme):
    """The README's one-line description: the middle of its '**Kind** · <about> · maker in MPC: ...' line."""
    for line in readme.splitlines()[:6]:
        if line.startswith("**") and " · " in line:
            return line.split(" · ")[1].strip()
    raise SystemExit("README has no '**Kind** · about · ...' line")


def license_file(D, src):
    if os.path.exists(os.path.join(D, "LICENSE")) or os.path.exists(os.path.join(D, "LICENSE.md")):
        return None
    if src is None:
        raise SystemExit("%s has no LICENSE and PLUGINS names no source for one" % D)
    if "/" in src:
        for f in ("LICENSE", "LICENSE.md", "LICENSE.txt"):
            try:
                return urllib.request.urlopen("https://raw.githubusercontent.com/%s/HEAD/%s" % (src, f), timeout=30).read().decode()
            except Exception:
                continue
        raise SystemExit("%s has no LICENSE file to copy" % src)
    return open(os.path.join(REPO, "licenses", src + ".txt")).read()


def workflow(rel, pid, spdx, about, sub, lib_cmd, user_data):
    ref = release_ref()
    build = 'bash "$MPC_VST/tools/build_port.sh" vst.json'
    if os.path.basename(rel) in OWN_SKIN:   # the .so from his tools, the screen from deploy/ (checked on the device)
        build += " && rm -rf build/skin && mkdir -p build/skin && cp -R deploy/Synths/* build/skin/"
    test = '\'"$MPC_VST/tools/test_port.sh" vst.json\''
    extra = ""
    if sub:
        build = "%s library && %s" % (lib_cmd, build)
        test = "'rm -rf build/%s && cp -R library build/%s && \"$MPC_VST/tools/test_port.sh\" vst.json'" % (sub, sub)
        extra = "      extra: library:%s\n" % sub
    ud = "      user_data: %s\n" % " ".join(user_data) if user_data else ""
    note = "      # its library into library/, shipped as the plugin folder's %s/ (MODULE_SUBDIR)\n" % sub if sub else ""
    return """# Build a DRAFT release (tag v<version>) with mpc-vst-plugins' shared workflow: Actions -> "Release (draft)" -> Run
# workflow. Then install that draft's zip on a device, smoke-test it and publish the draft (mpc-vst-plugins
# docs/RELEASING.md). dry_run builds the zip and skin previews as run artifacts only.
# The tools come from saustin2010/mpc-vst-plugins (sd88me's repo plus changes offered to it) until they're merged there:
# then point uses: and tools_ref at sd88me/mpc-vst-plugins and drop tools_repo.
# This plugin is developed in saustin2010/vst_instruments (%(rel)s) and published here.
name: Release (draft)

on:
  workflow_dispatch:
    inputs:
      version:
        description: Version, e.g. 1.0.1
        required: true
      dry_run:
        description: Build only (zip + skin previews as run artifacts), no draft release
        type: boolean
        default: false

jobs:
  vst:
    uses: saustin2010/mpc-vst-plugins/.github/workflows/vst-release.yml@%(ref)s
    permissions:
      contents: write
    with:
      tag: v${{ inputs.version }}
      version: ${{ inputs.version }}
      tools_repo: saustin2010/mpc-vst-plugins
      tools_ref: %(ref)s
      vst_dir: .
%(note)s      build: %(build)s
      host_test: %(test)s
%(extra)s%(ud)s      plugin_id: %(pid)s
      license: %(spdx)s
      about: %(about)s
      dry_run: ${{ inputs.dry_run }}
""" % dict(rel=rel, ref=ref, note=note, build=build, test=test, extra=extra, ud=ud, pid=pid, spdx=spdx,
           about=json.dumps(about, ensure_ascii=False))


def readme(rel, D, cfg, sub, made, lic_new, user_data):
    p = os.path.join(D, "README.md")
    s = open(p).read()
    name = os.path.basename(rel)
    repo = "mpc-vst-" + name
    skins = os.listdir(os.path.join(D, "deploy", "Synths"))
    skin = skins[0] if len(skins) == 1 else "%s - VST - %s" % (cfg["vendor"], cfg["name"])
    so = cfg["so"]
    # the files on the MPC
    data = (" and its %s (`%s/`)" % ("library" if not made else "files", sub)) if sub else ""
    old_data = (" and its data in `/sdcard/vst/%s/`" % sub) if sub else ""
    either = "; the plugin finds them either way (its data folder is `%s/` next to the `.so`)" % sub if sub else ""
    files_line = ("- Files: the release installs one folder, `/sdcard/Synths/%s/`, holding `%s`, its screen%s. The "
                  "vst_instruments installer puts `%s` in `/sdcard/vst/`%s instead%s." % (skin, so, data, so, old_data, either))
    s = re.sub(r"^- Files: .*$", lambda m: files_line, s, count=1, flags=re.M)
    # install
    zipname = re.sub(r"[^A-Za-z0-9]+", "-", cfg["name"]).strip("-")
    install = """## Install

Needs a standalone MPC or Force on MPC OS 3.x with root SSH access (checked on an MPC Live II).

**From the release:** download `%(zip)s-<version>-mpc-armv7.zip` from [Releases](https://github.com/saustin2010/%(repo)s/releases),
copy it to the MPC, unzip it and run `sh install.sh` in its folder as root. The installer stops MPC, backs up
`MPC.settings`, installs the plugin folder and starts MPC again; `INSTALL.md` in the zip has the details and a by-hand route.%(ud)s

**With the rest of the collection:** from [vst_instruments](%(vi)s) ([INSTALL.md](%(vi)s/blob/main/INSTALL.md)):

```
./install.sh <mpc-address> %(name)s
```

Use one or the other for this plugin: both register the same plugin (same uid), so the last one run wins.
""" % dict(zip=zipname, repo=repo, vi=VI, name=name,
           ud=("\nYour own files in %s are kept when you update or uninstall." % ", ".join("`%s/`" % u for u in user_data))
           if user_data else "")
    i = s.index("## Install\n")
    j = s.index("\n## ", i + 5)
    s = s[:i] + install + s[j:]
    # origin, licence
    s = s.replace("- MPC port and screen: this repo, built on",
                  "- MPC port and screen: [vst_instruments](%s) (`%s`), built on" % (VI, rel))
    s = re.sub(r"\(text: \[licenses/\]\([./]*licenses/\)\)", "(text: [LICENSE](LICENSE))", s)
    # building + development, in place of the one-line pointer at the end
    lib_line = ""
    if sub and not made:
        lib_line = "python3 release/fetch-library.py library        # its library, from its project at a pinned commit\n"
    elif made:
        lib_line = "python3 release/make-library.py library         # its starter files, made by a script here\n"
    building = """## Building

With Docker (32-bit ARM emulation for the build), Python 3 and the tools from
[saustin2010/mpc-vst-plugins](%(fork)s/tree/steve-features) (sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)
plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`):

```
git clone -b steve-features %(fork)s
MPC_VST=$PWD/mpc-vst-plugins
%(lib)sbash "$MPC_VST/tools/build_port.sh" vst.json        # build/: %(so)s, the screen, the plugin-list entry
bash "$MPC_VST/tools/test_port.sh" vst.json         # the offline host test (ASan/UBSan): must print PASSED
```

Releases are built by GitHub Actions (`.github/workflows/release.yml`, "Release (draft)") as a draft, installed and checked
on a device, then published. More: [BUILDING.md](%(vi)s/blob/main/BUILDING.md), and to change the screen, [RESKINNING.md](%(vi)s/blob/main/RESKINNING.md).

## Development

This plugin is developed in [vst_instruments](%(vi)s) (`%(rel)s`), next to the other plugins
and the tools that made its screen, and published to [%(repo)s](https://github.com/saustin2010/%(repo)s) for its
releases. Issues and pull requests are welcome in either.""" % dict(fork=FORK, lib=lib_line, so=so, vi=VI, rel=rel, repo=repo)
    if "\n## Building\n" not in s:
        s = re.sub(r"^To rebuild from source see .*$", lambda m: building, s, count=1, flags=re.M)
    # files table
    rows = ["| `LICENSE` | the licence |" if lic_new else None,
            "| `.github/workflows/release.yml` | the release build (GitHub Actions, a draft release) |",
            ("| `release/` | `library.json` (where its library comes from, pinned) and `fetch-library.py` (fetches it) |"
             if sub and not made else "| `release/` | `make-library.py` (makes its starter files) |" if made else None)]
    for r in [r for r in rows if r]:
        if r.split(" | ")[0] not in s:
            s = s.replace("| `screenshots/` | the pages as MPC draws them |",
                          "| `screenshots/` | the pages as MPC draws them |\n" + r, 1)
    s = s.replace("(its presets/kits are in the repo's `presets/` folder, not here) |", "(not used by the release) |")
    # any other link up into this repo -> absolute
    def absolute(m):
        target = os.path.normpath(os.path.join(rel, m.group(1)))
        kind = "tree" if m.group(1).endswith("/") or os.path.isdir(os.path.join(REPO, target)) else "blob"
        return "](%s/%s/main/%s)" % (VI, kind, target)
    s = re.sub(r"\]\(((?:\.\./)+[^)]*)\)", absolute, s)
    return write(p, s)


# The changes on saustin2010/mpc-vst-plugins steve-features (sd88me's main + these), for each plugin's FRAMEWORK.md:
# commit, short title, what it does for a plugin that uses it, what happens without it.
FORK_CHANGES = [
    ("b1c39bc", "module_params", "A hand-made `params.json` beside the Schwung `module`",
     "`vst.json` names both the Schwung module and this folder's `params.json` (readable names, ranges and options the "
     "module doesn't declare). The params file sets the parameter list and the Schwung adapter is still linked.",
     "The build fails: sd88me's tools take `params.json` alone and don't link the adapter (undefined `mpc_engine`)."),
    ("81f473b", "focus_ring", "`focus_ring=1` in `layout.conf`",
     "The control you touch gets a light tint and an accent outline, as on the screen checked on the device.",
     "No highlight on the touched control (sd88me's default since 26 September 2026)."),
    ("01da4b3", "qlink_box", "`qlink_box=slot` in `layout.conf`",
     "The outline MPC draws round the active Q-Link column frames each control's whole slot (a knob's 130 px box down "
     "to its value), leaves trigger buttons out and honours `qbox=no`: the boxes checked on a Live II.",
     "Tighter outlines round the knob rings, which can cut through names and values."),
    ("81a2edd", "when_bands", "`when=<param>:<i>/<N>` in `layout.conf`",
     "A picture shown only while a continuous parameter is in band i of N (an envelope drawing per sustain level).",
     "The build stops (\"is not an option parameter\")."),
    ("8a6ed80", "values_send", "Option `values` / `send` in `params.json`",
     "An option sends the engine its own value or word instead of its index, because this engine parses those.",
     "The options send their index and select the wrong setting."),
    ("2da5066", "transport", "`mpc_engine_transport()`",
     "The engine gets MPC's tempo, song position and play state every buffer, so it runs in time with the transport.",
     "It builds and passes the offline test, but on the MPC it never plays in time."),
    ("ea27eda", "programs", "`\"programs\"` `count` / `name_at` / `name` in `vst.json`",
     "MPC's PRESET menu lists exactly the engine's presets (its own count), named without loading each one "
     "(`name_at`), or by reading them once at creation (`name`).",
     "The PRESET menu lists empty slots up to the parameter's range, or numbered names."),
    ("55e60a0", "clamped", "`\"clamped\": true` in `params.json`",
     "Marks a parameter the engine holds to what it has loaded (a file, pattern or track number); the host test "
     "doesn't expect it to read back as set.",
     "The plugin is the same; the host test fails (and so the release build)."),
    ("94e85dd", "preset_values", "Option `values` / `send` in the presets (`presets.json`)",
     "The host test reads a preset's option setting as the option's own value or word, as `gen_vst.py` writes it.",
     "The plugin is the same; the host test expects the wrong option and fails (and so the release build)."),
    ("7afa72e", "live", "`\"live\"` in `vst.json`",
     "The step light: after every block the wrapper tells MPC when the engine has moved the playing step, so the "
     "skin follows playback.",
     "The step light stays where it was when the page was drawn."),
    ("10d8f5f", "travel", "`QLINK_TRAVEL` and `SET_IF_CHANGED` in `vst.json` `defines`",
     "Q-Link and data-wheel ticks add up like a detented knob, and a switch moves after half an option's width of "
     "turn; a set to the value the engine already holds is skipped. This is how the plugin was checked on the device.",
     "Every tick steps an option: on a Live II, turning one Q-Link flipped other switches (8 October 2026)."),
]
# changes every plugin here relies on: release, install and test tooling
FORK_TOOLING = [
    ("9595071", "The release's `install.sh` matches the plugin's path as text",
     "The plugin's name has a kind tag in brackets (`[SYN]`, `[SEQ]` ...), which sd88me's installer read as a pattern: "
     "it refused to install, leaving `MPC.settings` unchanged."),
    ("6c87b6d", "`tools_repo` in the release workflow",
     "Lets `.github/workflows/release.yml` build with the fork's tools instead of sd88me's."),
    ("50f459d", "The host test runs in Docker on macOS",
     "Apple's AddressSanitizer hangs on macOS 26 before the test starts."),
    ("6303f4e", "Host test: long whole-number ranges",
     "A data-wheel click moves 1/100 of a long range (0-5000 ms), as it should; the test expected one step."),
    ("f210a56", "Host test with `QLINK_TRAVEL`",
     "Skips the travel check on a placeholder parameter, and allows half a step when a saved state is restored."),
    ("7f0653c", "Parameter 0 held until the next block",
     "On insert and on every STOP, MPC's host sets parameter 0 to the far end of its range and straight back. Where "
     "that is a preset it was loaded twice and every knob moved since went back to it (Live II, 10 October 2026); the "
     "wrapper now drops a pair that ends where it started, and the host test replays the toggle."),
]


def fork_uses(D, cfg, params, layout):
    """Which FORK_CHANGES this plugin relies on, from its own files."""
    used = set()
    if cfg.get("module") and cfg.get("params"):
        used.add("module_params")
    if re.search(r"^focus_ring=1", layout, re.M):
        used.add("focus_ring")
    if re.search(r"^qlink_box=slot", layout, re.M):
        used.add("qlink_box")
    if re.search(r"when=[\w.]+:\d+/\d+", layout):
        used.add("when_bands")
    if any(q.get("values") or q.get("send") for q in params):
        used.add("values_send")
    if any(q.get("clamped") for q in params):
        used.add("clamped")
    pr = cfg.get("programs") or {}
    if pr.get("count") or pr.get("name_at") or pr.get("name"):
        used.add("programs")
    d = cfg.get("defines", {})
    if d.get("QLINK_TRAVEL") or d.get("SET_IF_CHANGED"):
        used.add("travel")
    if cfg.get("live"):
        used.add("live")
    if cfg.get("presets") and any(q.get("values") or q.get("send") for q in params):
        used.add("preset_values")
    for sub in ("src", "mpc"):
        for root, _, files in os.walk(os.path.join(D, sub)):
            for f in files:
                if f.endswith((".c", ".cc", ".cpp")) and "mpc_engine_transport(" in open(
                        os.path.join(root, f), errors="replace").read():
                    used.add("transport")
    return used


def framework_doc(name):
    """Write <plugin>/FRAMEWORK.md: the fork changes this plugin uses, why, and what to do when sd88me has them."""
    rel = folder_of(name)
    D = os.path.join(REPO, rel)
    cfg = json.load(open(os.path.join(D, "vst.json")))
    pfile = cfg.get("params") or cfg.get("module")
    params = json.load(open(os.path.join(D, pfile)))
    params = params if isinstance(params, list) else params.get("params") or []
    layout = open(os.path.join(D, cfg["layout"])).read() if cfg.get("layout") else ""
    wf = os.path.join(D, ".github", "workflows", "release.yml")
    pin = re.search(r"tools_ref:\s*([0-9a-f]{40})", open(wf).read()).group(1)
    up = os.path.join(REPO, "framework", "upstream")
    def has(c):   # the commit is in what this plugin builds with (the pilots pin an earlier one)
        return subprocess.run(["git", "-C", up, "merge-base", "--is-ancestor", c, pin], capture_output=True).returncode == 0
    used = fork_uses(D, cfg, params, layout)
    if name in OWN_SKIN:   # its skin isn't built by these tools: their skin changes don't apply
        used -= {"focus_ring", "qlink_box", "when_bands"}
    rows = [(c, t, what, without) for c, key, t, what, without in FORK_CHANGES if key in used and has(c)]
    tooling = [(c, t, why) for c, t, why in FORK_TOOLING if has(c)]
    link = lambda c: "[`%s`](%s/commit/%s)" % (c, FORK, c)
    out = ["# The framework changes this plugin uses", "",
           "This plugin is built with [saustin2010/mpc-vst-plugins](%s/tree/%s) at `%s` (the commit in "
           "`.github/workflows/release.yml`): sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) with "
           "changes added on top, each meant to be offered to him. This page lists the ones this plugin relies on, what "
           "each does here and what happens without it. The full list, for every plugin: "
           "[framework/README.md](%s/blob/main/framework/README.md) in vst_instruments." % (FORK, pin, pin[:7], VI), ""]
    if rows:
        out += ["## What this plugin needs", "", "| Change | What it does here | Without it |", "|---|---|---|"]
        out += ["| %s %s | %s | %s |" % (t, link(c), what, without) for c, t, what, without in rows]
        out.append("")
    lines = [l for l in ("qlink_bounds=column", "qlink_box=slot", "label_scale=1", "focus_ring=1")
             if re.search("^" + re.escape(l), layout, re.M) and name not in OWN_SKIN]
    if lines:
        out += ["## The layout lines at the top of `layout.conf`", "",
                "They make the tools draw the screen as it was checked on the device:", ""]
        notes = {"qlink_bounds=column": "one Q-Link outline per column (sd88me's own option)",
                 "qlink_box=slot": "those outlines measured by whole slots (a fork change, above)",
                 "label_scale=1": "names and values at their designed size (sd88me's option; his default is 1.15)",
                 "focus_ring=1": "the touched control highlighted (a fork change, above)"}
        out += ["- `%s`: %s" % (l, notes[l]) for l in lines] + [""]
    if name in OWN_SKIN:
        out += ["## The screen ships as built here", "",
                "The release builds the plugin with these tools but ships the skin in `deploy/Synths/`, built by "
                "vst_instruments' own tools (`tools/build.sh`) and checked on the device: sd88me's skin builder lays out "
                "this plugin's per-sub-page controls (`banks=`) and readouts differently. Rebuild `deploy/` with "
                "`tools/build.sh` in vst_instruments after changing `layout.conf` or `mpc/gen.py`, before a release.", ""]
    out += ["## Every plugin here also relies on", "", "| Change | Why |", "|---|---|"]
    out += ["| %s %s | %s |" % (t, link(c), why) for c, t, why in tooling] + [""]
    out += ["## When sd88me's mpc-vst-plugins has them", "",
            "1. In `.github/workflows/release.yml`, point `uses:` at `sd88me/mpc-vst-plugins/.github/workflows/vst-release.yml@<his "
            "commit>`, set `tools_ref` to the same commit and delete the `tools_repo` line.",
            "2. Nothing else changes: the layout lines, `defines`, `params.json` and `vst.json` stay as they are.",
            "3. If he leaves one out or names it differently, keep building from the fork until that's settled; the table "
            "above says what changes for this plugin without it.", ""]
    write(os.path.join(D, "FRAMEWORK.md"), "\n".join(out))
    r = os.path.join(D, "README.md")
    t = open(r).read()
    row = "| `FRAMEWORK.md` | the changes to sd88me's tools this plugin is built with, and why |"
    if row not in t:
        anchor = "| `.github/workflows/release.yml` | the release build (GitHub Actions, a draft release) |"
        if anchor in t:
            t = t.replace(anchor, anchor + "\n" + row)
    t = t.replace("plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`):",
                  "plus changes offered to it, until they're merged there; the commit is the one in `.github/workflows/release.yml`, "
                  "and [FRAMEWORK.md](FRAMEWORK.md) says which changes this plugin uses and why):")
    write(r, t)
    return len(rows)


def prepare(name):
    if name not in PLUGINS:
        raise SystemExit("%s: not in PLUGINS (dev-tools/catalogue/prepare.py)" % name)
    pid, spdx, lic_src, user_data = PLUGINS[name]
    rel = folder_of(name)
    D = os.path.join(REPO, rel)
    vj = os.path.join(D, "vst.json")
    cfg = json.load(open(vj))
    changed = []
    if write(os.path.join(D, ".gitignore"), "build/\nlogs/\npreview/\nlibrary/\ndist/\n"):
        changed.append(".gitignore")
    lic = license_file(D, lic_src)
    if lic:
        write(os.path.join(D, "LICENSE"), lic)
        changed.append("LICENSE")
    # the library
    sub, made, lib_cmd = None, False, None
    md = cfg.get("defines", {}).get("MODULE_DIR")
    if md:
        sub = md.strip('"').rstrip("/").split("/")[-1]
        if "MODULE_SUBDIR" not in cfg["defines"]:
            text = open(vj).read()
            new = re.sub(r'("MODULE_DIR":\s*"\\"[^"]*\\"")', r'\1,\n    "MODULE_SUBDIR": "\\"%s\\""' % sub, text, count=1)
            json.loads(new)
            write(vj, new)
            changed.append("vst.json MODULE_SUBDIR")
    src = fetch_sources(name)
    if name in MADE:
        script, out = MADE[name]
        made = True
        os.makedirs(os.path.join(D, "release"), exist_ok=True)
        shutil.copy(os.path.join(REPO, script), os.path.join(D, "release", os.path.basename(script)))
        write(os.path.join(D, "release", "make-library.py"), '''#!/usr/bin/env python3
"""Make this plugin's starter files into a folder (default library/), with the script that made them for
saustin2010/vst_instruments (original material, free to ship): python3 release/make-library.py [<folder>]"""
import os, subprocess, sys
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dest = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "library"))
subprocess.run([sys.executable, os.path.join(HERE, "release", %r), os.path.join(dest, %r)], check=True)
print("made", os.path.join(dest, %r))
''' % (os.path.basename(script), out, out))
        lib_cmd = "python3 release/make-library.py"
        changed.append("release/make-library.py")
    elif src:
        lib = {"about": "This plugin's library, fetched from the projects it belongs to at the commit it was built and tested "
                        "with (every file checked against its git hash). release/fetch-library.py puts it in library/, which "
                        "the release ships as the plugin folder's %s/ (its data folder, MODULE_SUBDIR)." % sub,
               "sources": [{"repo": r, "commit": c, "path": pth, "to": to} for r, c, pth, to in src]}
        write(os.path.join(D, "release", "library.json"), json.dumps(lib, indent=2) + "\n")
        shutil.copy(os.path.join(REPO, "schwung", "instruments", "hera", "release", "fetch-library.py"),
                    os.path.join(D, "release", "fetch-library.py"))
        lib_cmd = "python3 release/fetch-library.py"
        changed.append("release/library.json")
    if (src or made) and not sub:
        raise SystemExit("%s has a library but no MODULE_DIR" % name)
    if sub and not (src or made):
        sub = None   # a data folder only for the user's own files (Noisemaker's banks): nothing shipped
    readme_text = open(os.path.join(D, "README.md")).read()
    if write(os.path.join(D, ".github", "workflows", "release.yml"),
             workflow(rel, pid, spdx, about_of(readme_text), sub, lib_cmd, user_data)):
        changed.append("release.yml")
    if readme(rel, D, cfg, sub, made, bool(lic), user_data):
        changed.append("README.md")
    framework_doc(name)
    print("%-14s %-14s %-16s %s" % (name, pid, spdx, ", ".join(changed) or "unchanged"))
    return src


def main():
    if "--framework-doc" in sys.argv:   # only FRAMEWORK.md (and its README pointers), e.g. for the released pilots
        names = sorted(PLUGINS) if "--all" in sys.argv else [a for a in sys.argv[1:] if not a.startswith("-")]
        for n in names:
            print("%-14s FRAMEWORK.md: %d changes it needs" % (n, framework_doc(n)))
        return
    names = (sorted(set(PLUGINS) - PILOTS) if "--ready" in sys.argv
             else [a for a in sys.argv[1:] if not a.startswith("-")])
    if not names:
        raise SystemExit(__doc__)
    moved = [n for n in names if prepare(n) and n not in ("hera",)]
    if moved:
        print("now in their release/library.json, drop from tools/fetch-presets.py SOURCES:", " ".join(moved))


if __name__ == "__main__":
    main()
