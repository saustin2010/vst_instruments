#!/usr/bin/env python3
"""Fetch the plugins' presets, kits, wavetables and patches into presets/<plugin>/ (not kept in git):
    python3 tools/fetch-presets.py [<plugin> ...] [--force]        (no names: all of them; --list: which have some)
Each library comes from the project it belongs to, at the exact commit these plugins were built and tested with, and
every file is checked against that commit's git hash. Two are made here instead (they're this repo's own): Mr Drums'
starter kit and MIDI Player's demo file. presets/<plugin>/ mirrors the plugin's data folder on the MPC,
/sdcard/vst/<plugin>/, which is where install.sh copies it (except a not-installed/ folder). Already-fetched
plugins are skipped unless --force. Needs Python 3 and internet access to github.com; no account or token."""
import glob, hashlib, json, os, shutil, subprocess, sys, tempfile, urllib.parse, urllib.request
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRESETS = os.path.join(REPO, "presets")

# plugin: [(GitHub repo, commit, path in that repo (folder or file), where it goes inside presets/<plugin>/)]
SOURCES = {
    "libpo32": [("mestela/schwung-libpo32", "4125e2989df1747a66f56388eb0b5bd7e1310079", "src/kits", "kits")],
    "helm": [("mtytel/helm", "abdedd527e6e1cf86636f0f1e8a3e75b06ed166a", "patches/Factory Presets",
              "helm-data/patches/Factory Presets"),
             ("andree182/schwung-helm", "10823a8ebc3a0e1a0cd43d0c459e8bae63996c9d", "data/Move Organ.helm",
              "helm-data/patches/Factory Presets/Keys/Move Organ.helm")],
}
# a plugin that pins its own library in its folder (release/library.json: its release build reads it too, through
# release/fetch-library.py, so the pin is in one place)
for _lib in sorted(glob.glob(os.path.join(REPO, "*", "*", "release", "library.json")) +
                   glob.glob(os.path.join(REPO, "*", "*", "*", "release", "library.json"))):
    SOURCES[os.path.basename(os.path.dirname(os.path.dirname(_lib)))] = [
        (s["repo"], s["commit"], s["path"], s["to"]) for s in json.load(open(_lib))["sources"]]
# made by this repo's own scripts (original material)
MADE = {
    "mrdrums": (["dev-tools/skin-redesign/make_starter_kit.py", "{out}/kits/01_Starter"], "kits/01_Starter"),
    "midiplayer": (["dev-tools/midifx/make_demo_mid.py", "{out}/MIDI/Demo - Am F C G.mid"], "MIDI"),
}
ALL = sorted(set(SOURCES) | set(MADE))


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "vst_instruments-fetch-presets"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read()
        except Exception:
            if attempt == 3:
                raise


def git_blob_sha(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def fetch(plugin, dest):
    files = []
    for repo, sha, path, to in SOURCES[plugin]:
        tree = json.loads(get("https://api.github.com/repos/%s/git/trees/%s?recursive=1" % (repo, sha)))["tree"]
        hits = [t for t in tree if t["type"] == "blob" and (t["path"] == path or t["path"].startswith(path + "/"))]
        if not hits:
            raise SystemExit("%s: %s has no %s at %s" % (plugin, repo, path, sha[:7]))
        for t in hits:
            rel = to if t["path"] == path else os.path.join(to, t["path"][len(path) + 1:])
            files.append((repo, sha, t["path"], t["sha"], os.path.join(dest, rel)))

    def one(f):
        repo, sha, path, blob, out = f
        data = get("https://raw.githubusercontent.com/%s/%s/%s" % (repo, sha, urllib.parse.quote(path)))
        if git_blob_sha(data) != blob:
            raise SystemExit("%s: %s/%s doesn't match its git hash" % (plugin, repo, path))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as fh:
            fh.write(data)

    with ThreadPoolExecutor(8) as pool:
        list(pool.map(one, files))
    return len(files)


def make(plugin, dest):
    script, sub = MADE[plugin]
    args = [a.format(out=dest) for a in script]
    subprocess.run([sys.executable, os.path.join(REPO, args[0])] + args[1:], check=True, capture_output=True)
    return sum(len(fs) for _, _, fs in os.walk(os.path.join(dest, sub.split("/")[0])))


def main():
    if "--list" in sys.argv:   # the plugins that have presets to fetch (install.sh, build.sh)
        print(" ".join(ALL))
        return
    names = [a for a in sys.argv[1:] if not a.startswith("-")] or ALL
    force = "--force" in sys.argv
    bad = [n for n in names if n not in ALL]
    if bad:
        raise SystemExit("no presets to fetch for: %s (plugins with presets: %s)" % (", ".join(bad), ", ".join(ALL)))
    for p in names:
        dest = os.path.join(PRESETS, p)
        if os.path.isdir(dest) and os.listdir(dest) and not force:
            print("  %-11s already in presets/%s (--force to fetch again)" % (p, p))
            continue
        tmp = tempfile.mkdtemp(prefix=".fetch-%s-" % p, dir=os.path.join(REPO))
        try:
            n = fetch(p, tmp) if p in SOURCES else make(p, tmp)
            shutil.rmtree(dest, ignore_errors=True)
            os.makedirs(PRESETS, exist_ok=True)
            os.rename(tmp, dest)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        print("  %-11s %4d files -> presets/%s" % (p, n, p))


if __name__ == "__main__":
    main()
