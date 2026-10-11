"""Read abwatch's AB lines (one file per trial) and flag plugins that work while silent, against a baseline.

  ab_report.py <ab-trial*.txt...> [--baseline <dir>] [--min-us 5]

A plugin is flagged when, averaged over the trials:
  ALWAYS ON    idle/play >= 0.5  : a never-played instance costs at least half its playing cost
  PARTLY ON    idle/play >= 0.2  : a fifth or more, and at least 3x --min-us
  NO SLEEP     after/fresh >= 2  : once it has played, it keeps working after its sound has ended
and the extra cost is at least --min-us (tiny plugins' ratios are noise). The baseline defaults to the newest folder
in baseline/ with ab-trial files; each flag is marked new, still, or fixed against it."""
import pathlib, re, sys

LINE = re.compile(r"AB (\S+?)(?:\.so)?\s+idle/play\s+([\d.]+)\s+after/fresh\s+([\d.]+)\s+\|\s+play\s+([\d.]+) idle\s+([\d.]+)"
                  r" after\s+([\d.]+) fresh\s+([\d.]+)")


def load(files):
    runs = {}
    for f in files:
        for line in pathlib.Path(f).read_text(errors="replace").splitlines():
            m = LINE.match(line)
            if m:
                runs.setdefault(m[1], []).append([float(x) for x in m.groups()[1:]])
            elif line.startswith("AB ") and "FAILED" in line:
                runs.setdefault(line.split()[1], [])
    out = {}
    for name, rs in runs.items():
        if not rs:
            out[name] = None
            continue
        avg = lambda i: sum(r[i] for r in rs) / len(rs)
        out[name] = dict(idle_play=avg(0), after_fresh=avg(1), play=min(r[2] for r in rs), idle=min(r[3] for r in rs),
                         after=min(r[4] for r in rs), fresh=min(r[5] for r in rs))
    return out


def flags(r, min_us):
    if r is None:
        return {"FAILED"}
    f = set()
    if r["idle_play"] >= 0.5 and r["idle"] >= min_us:
        f.add("ALWAYS ON")
    elif r["idle_play"] >= 0.2 and r["idle"] >= 3 * min_us:
        f.add("PARTLY ON")
    if r["after_fresh"] >= 2 and r["after"] - r["fresh"] >= min_us:
        f.add("NO SLEEP")
    return f


def main(argv):
    files, base, min_us = [], None, 5.0
    it = iter(argv)
    for a in it:
        if a == "--baseline":
            base = pathlib.Path(next(it))
        elif a == "--min-us":
            min_us = float(next(it))
        else:
            files.append(a)
    if not files:
        sys.exit(__doc__)
    if base is None:
        dirs = sorted(d for d in (pathlib.Path(__file__).parent / "baseline").iterdir() if list(d.glob("ab-trial*.txt")))
        base = dirs[-1] if dirs else None
    now = load(files)
    was = load(sorted(base.glob("ab-trial*.txt"))) if base else {}
    print(f"{'plugin':16} {'idle/play':>9} {'(was)':>6} {'after/fresh':>11} {'(was)':>6} {'idle us':>8} {'after us':>8}"
          f"  flags (baseline {base.name if base else 'none'})")
    for name in sorted(now):
        r, b = now[name], was.get(name)
        fn, fb = flags(r, min_us), (flags(b, min_us) if name in was else set())
        marks = [f"{x} ({'still' if x in fb else 'new'})" for x in sorted(fn)] + [f"{x} (fixed)" for x in sorted(fb - fn)]
        if r is None:
            print(f"{name:16} FAILED")
            continue
        bw = lambda k: f"{b[k]:6.2f}" if b else f"{'-':>6}"
        print(f"{name:16} {r['idle_play']:9.2f} {bw('idle_play')} {r['after_fresh']:11.2f} {bw('after_fresh')} "
              f"{r['idle']:8.1f} {r['after']:8.1f}  {', '.join(marks)}")


if __name__ == "__main__":
    main(sys.argv[1:])
