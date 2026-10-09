#!/usr/bin/env python3
"""Fetch this plugin's library (presets, kits, wavetables) into a folder, from the projects it belongs to:
    python3 release/fetch-library.py [<folder>]          (default: library/, next to vst.json)
The sources are in release/library.json: a GitHub repo, the exact commit, a path in it and where it goes. Every file is
checked against that commit's git hash. Needs Python 3 and internet access to github.com; no account or token.
(saustin2010/vst_instruments' tools/fetch-presets.py reads the same library.json.)"""
import hashlib, json, os, sys, urllib.parse, urllib.request
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "mpc-vst-fetch-library"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read()
        except Exception:
            if attempt == 3:
                raise


def git_blob_sha(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def fetch(sources, dest):
    files = []
    for s in sources:
        repo, sha, path, to = s["repo"], s["commit"], s["path"], s["to"]
        tree = json.loads(get("https://api.github.com/repos/%s/git/trees/%s?recursive=1" % (repo, sha)))["tree"]
        hits = [t for t in tree if t["type"] == "blob" and (t["path"] == path or t["path"].startswith(path + "/"))]
        if not hits:
            raise SystemExit("%s has no %s at %s" % (repo, path, sha[:7]))
        for t in hits:
            rel = to if t["path"] == path else os.path.join(to, t["path"][len(path) + 1:])
            files.append((repo, sha, t["path"], t["sha"], os.path.join(dest, rel)))

    def one(f):
        repo, sha, path, blob, out = f
        data = get("https://raw.githubusercontent.com/%s/%s/%s" % (repo, sha, urllib.parse.quote(path)))
        if git_blob_sha(data) != blob:
            raise SystemExit("%s/%s doesn't match its git hash" % (repo, path))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as fh:
            fh.write(data)

    with ThreadPoolExecutor(8) as pool:
        list(pool.map(one, files))
    return len(files)


def main():
    dest = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "library"))
    lib = json.load(open(os.path.join(HERE, "release", "library.json")))
    print("%d files -> %s" % (fetch(lib["sources"], dest), dest))


if __name__ == "__main__":
    main()
