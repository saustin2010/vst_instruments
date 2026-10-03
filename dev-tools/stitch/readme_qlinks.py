"""Rewrite the "Q-Link columns" lines of a plugin's README.md from its layout.conf (steve/tools/stitch, 2026-10-03).
   python3 steve/tools/stitch/readme_qlinks.py <plugin dir> [...]
Each "### <n>. <PAGE>" section gets one line per `qlinks` line of that page ("(bank 1)", "(bank 2)"... when there are
several): the columns of four in order, by parameter name (what MPC shows), skipping empty "-" slots and empty columns.
Run it after changing a page's Q-Links, so the README says what the device does."""
import json, os, re, sys


def pages(layout):
    """[(page name, [(bank title, [keys])])] in page order"""
    out = []
    for line in open(layout):
        line = line.strip()
        m = re.match(r"\[tab (.+)\]$", line)
        if m:
            out.append((m.group(1).strip(), []))
            continue
        m = re.match(r'qlinks\s+"([^"]+)"\s*=\s*(.+)$', line)
        if m and out:
            out[-1][1].append((m.group(1), [k.strip() for k in m.group(2).split(",") if k.strip()]))
    return out


def columns(keys, names):
    cols = []
    for c in range(0, len(keys), 4):
        ks = [names.get(k, k) for k in keys[c:c + 4] if k != "-"]
        if ks:
            cols.append("**%d** %s" % (c // 4 + 1, ", ".join(ks)))
    return "  ·  ".join(cols)


def main():
    for d in sys.argv[1:]:
        names = {p["key"]: p.get("name", p["key"]) for p in json.load(open(os.path.join(d, "params.json")))["params"]}
        readme = os.path.join(d, "README.md")
        text = open(readme).read()
        changed = 0
        for n, (page, banks) in enumerate(pages(os.path.join(d, "layout.conf")), 1):
            head = re.search(r"(?m)^### %d\. .*$" % n, text)
            if not head or not banks:
                continue
            nxt = re.search(r"(?m)^#{2,3} ", text[head.end():])
            end = head.end() + (nxt.start() if nxt else len(text) - head.end())
            sec = text[head.end():end]
            lines = ["Q-Link columns%s: %s" % (" (bank %d)" % (b + 1) if len(banks) > 1 else "", columns(keys, names))
                     for b, (_, keys) in enumerate(banks)]
            body = re.sub(r"(?m)^Q-Link columns.*\n(\nQ-Link columns.*\n)*", "\n\n".join(lines) + "\n", sec, count=1)
            if body != sec:
                text = text[:head.end()] + body + text[end:]
                changed += 1
        open(readme, "w").write(text)
        print("%s: %d page(s) updated" % (os.path.basename(d), changed))


if __name__ == "__main__":
    main()
