#!/usr/bin/env bash
# Stress-test several ports in a row (steve/tools/fuzz, 2026-10-02):
#   steve/tools/fuzz/fuzz_all.sh [blocks] <port>...
# Runs fuzz_port.sh on each port twice (one thread, then audio and control on two threads as MPC does) and prints
# one line per run: SURVIVED, or the first sanitizer/crash lines. Full output: <port>/logs/fuzz-<mode>.log.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); STEVE=$(dirname "$(dirname "$HERE")")
BLOCKS=1500
case "${1:-}" in ''|*[!0-9]*) ;; *) BLOCKS=$1; shift ;; esac
for p in "$@"; do
  mkdir -p "$STEVE/schwung-ports/$p/logs"
  for t in 0 1; do
    mode=$([ $t = 1 ] && echo threaded || echo single)
    log="$STEVE/schwung-ports/$p/logs/fuzz-$mode.log"
    "$HERE/fuzz_port.sh" "$p" 1 "$BLOCKS" "$t" > "$log" 2>&1
    rc=$?
    printf "== %-12s %-8s " "$p" "$mode"
    if grep -q "^SURVIVED" "$log"; then
      echo "SURVIVED $(grep -c 'runtime error' "$log") UB reports; $(grep '^blocks' "$log")"
    else
      echo "FAILED (exit $rc): $(grep -m 3 -E 'ERROR|runtime error|terminate|what\(\)|#[0-3] ' "$log" | tr '\n' ' ' | cut -c1-400)"
    fi
  done
done
