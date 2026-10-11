"""Summarise procwatch's JSON lines (<dir>/<plugin>.txt) into one table and a list of flags:  summarise.py <dir>"""
import json, pathlib, sys

out = pathlib.Path(sys.argv[1])
rows = []
for f in sorted(f for f in out.glob("*.txt") if not f.name.startswith("ab-")):
    line = next((l for l in f.read_text(errors="replace").splitlines() if l.startswith("JSON ")), None)
    if not line:
        print(f"{f.stem}: no result\n  " + "\n  ".join(f.read_text(errors="replace").splitlines()[-5:]))
        continue
    rows.append((f.stem, json.loads(line[5:])))

BLOCKS_PER_S = 44100 / 128


def calls(stage, op):
    return stage.get("host_calls_per_s", {}).get(str(op), 0.0)


print(f"{'plugin':14} {'thr':>3} {'idle us':>8} {'play us':>8} {'after us':>8} {'idle/play':>9} {'after pk':>9} "
      f"{'getTime/blk':>11} {'idle autom/s':>12} {'idle upd/s':>10} {'bg idle%':>8} {'bg wake/s':>9} "
      f"{'left thr':>8} {'leak B/insert':>13}")
flags = []
for name, j in rows:
    idle, play, after, idlep = j["idle"], j["notes"], j["after"], j["idle_playing"]
    bg_cpu = max([t["cpu_pct"] for s in ("idle", "idle_playing", "after") for t in j[s]["threads"]] or [0])
    bg_wake = max([t["wakeups_per_s"] for s in ("idle", "idle_playing", "after") for t in j[s]["threads"]] or [0])
    gt = max(calls(idle, 7), calls(idlep, 7)) / BLOCKS_PER_S
    ratio = idle["audio_median_us"] / play["audio_median_us"] if play["audio_median_us"] else 0
    leak = j["inst2"]["heap_after_close"]
    print(f"{name:14} {j['threads_after_open']:>3} {idle['audio_median_us']:>8.1f} {play['audio_median_us']:>8.1f} "
          f"{after['audio_median_us']:>8.1f} {ratio:>9.2f} {after['peak_last_quarter']:>9.5f} {gt:>11.2f} "
          f"{calls(idle, 0) + calls(after, 0):>12.1f} {calls(idle, 42) + calls(after, 42):>10.1f} {bg_cpu:>8.1f} "
          f"{bg_wake:>9.1f} {j['closed']['threads']:>8} {leak:>13}")
    if j["closed"]["threads"] > 0:
        flags.append(f"{name}: {j['closed']['threads']} thread(s) still running after every instance closed: {j['closed']['left_threads']}")
    if j["inst2"]["threads_after_close"] > 0:
        flags.append(f"{name}: a second instance leaves {j['inst2']['threads_after_close']} thread(s) behind when closed")
    if bg_cpu > 0.5 or bg_wake > 2:
        flags.append(f"{name}: background thread busy while idle: {bg_cpu:.1f}% CPU, {bg_wake:.0f} wakeups/s")
    for s in ("idle", "after"):
        for op, what in ((0, "automate (parameter changed)"), (42, "updateDisplay")):
            if calls(j[s], op) > 0:
                flags.append(f"{name}: {calls(j[s], op):.1f}/s {what} host calls while {s} (no notes, nothing touched)")
    if gt > 1.01:
        flags.append(f"{name}: {gt:.0f} audioMasterGetTime calls per block")
    if after["audio_median_us"] > 1.5 * idle["audio_median_us"] + 5 and after["peak_last_quarter"] < 1e-4:
        flags.append(f"{name}: silent after release but costs {after['audio_median_us'] / idle['audio_median_us']:.1f}x idle "
                     f"({after['audio_median_us']:.0f} vs {idle['audio_median_us']:.0f} us/block): voices left running?")
    if leak > 4096:
        flags.append(f"{name}: one insert+remove leaves {leak} B of heap")
    if j.get("env_changed"):
        flags.append(f"{name}: changed the host process's environment (on MPC, all of MPC's): {', '.join(j['env_changed'])}")
    if j["inst2"]["fds_after_close"] > 0 or j["closed"]["fds"] > 0:
        flags.append(f"{name}: file descriptors left open (2nd instance {j['inst2']['fds_after_close']}, all closed {j['closed']['fds']})")
    if any(s.get("host_calls_from_other_threads", 0) for s in (idle, play, after, idlep)):
        flags.append(f"{name}: host called from a non-audio thread")
print()
print("\n".join(flags) or "no flags")
