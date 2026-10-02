#!/usr/bin/env bash
# MPC CPU while something runs (steve/tools/screengrab, 2026-10-02):  cpuwatch.sh [seconds=180] [every=3] [host]
# Every `every` seconds prints MPC's total CPU % (of one core) and its three busiest threads by name, from
# /proc/<pid>/task/*/stat deltas. Read-only. For checking what a skin animation costs on MPC's UI side.
SECS=${1:-180}; EV=${2:-3}; HOST=${3:-mpc-live-ii.local}
ssh -o ConnectTimeout=10 ${MPC_KEY:+-i "$MPC_KEY"} "root@$HOST" "
P=\$(pidof MPC); HZ=100; end=\$((\$(date +%s) + $SECS))
snap() { for t in /proc/\$P/task/*; do echo \"\$(basename \$t) \$(cat \$t/comm 2>/dev/null | tr ' ' _) \$(cut -d')' -f2- \$t/stat | awk '{print \$12+\$13}')\"; done; }
snap > /tmp/cw.\$\$.a
while [ \$(date +%s) -lt \$end ]; do sleep $EV; snap > /tmp/cw.\$\$.b
  awk -v ev=$EV 'NR==FNR{a[\$1]=\$3;next}{d=\$3-a[\$1]; if(d>0){tot+=d; print d/ev \" \" \$2}} END{print tot/ev \" TOTAL\"}' /tmp/cw.\$\$.a /tmp/cw.\$\$.b |
    sort -rn | awk -v t=\"\$(date +%H:%M:%S)\" 'NR<=4{printf \"%s%s %.0f%%\", (NR==1?t\" \":\" | \"), \$2, \$1} END{print \"\"}'
  mv /tmp/cw.\$\$.b /tmp/cw.\$\$.a; done; rm -f /tmp/cw.\$\$.a"
