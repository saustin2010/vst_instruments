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
}
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
           about=json.dumps(about))


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
    print("%-14s %-14s %-16s %s" % (name, pid, spdx, ", ".join(changed) or "unchanged"))
    return src


def main():
    names = (sorted(set(PLUGINS) - PILOTS) if "--ready" in sys.argv
             else [a for a in sys.argv[1:] if not a.startswith("-")])
    if not names:
        raise SystemExit(__doc__)
    moved = [n for n in names if prepare(n) and n not in ("hera",)]
    if moved:
        print("now in their release/library.json, drop from tools/fetch-presets.py SOURCES:", " ".join(moved))


if __name__ == "__main__":
    main()
