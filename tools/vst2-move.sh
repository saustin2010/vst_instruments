#!/usr/bin/env bash
# MPC OS 3.10.1 and later list plugins only from a "VST2 Plugins" folder (Preferences > Enhancements: Enable Enhancements and
# External Plugin Support); the ones registered in MPC.settings (every install before 3.10.1) are hidden from the plugin
# browser, though saved projects still load them. This copies every plugin MPC.settings lists, as it is on the MPC (your own
# builds included), into /sdcard/VST2 Plugins/<maker> - VST - <name>/ with its data folder, its skin and a plugin.json.
#   tools/vst2-move.sh <mpc address>                     the plan: what would be copied (changes nothing)
#   tools/vst2-move.sh <mpc address> --apply             copy (then restart MPC: it reads the internal drive at start-up)
#   tools/vst2-move.sh <mpc address> --retire [--apply]  once the copies work: set the old /sdcard/Synths skins aside
#                                                        (MPC 3.10.1 draws an old copy's skin over the new one's)
#   tools/vst2-move.sh <mpc address> --unretire [--apply]  put those skins back (before going back to MPC OS 3.9.1)
#   tools/vst2-move.sh <mpc address> --labels [--apply]    list the copies under the names MPC.settings gives them (labels
#                                                        like [SYN] added there), not the names their binaries report
#   tools/vst2-move.sh <mpc address> --unlabel [--apply]   back to the binaries' names
# Nothing is deleted and MPC.settings isn't edited: /sdcard/vst stays, for saved projects and for the plugins that read
# their data from /sdcard/vst/<name> directly. MPC_SSH_KEY / MPC_SSH_USER / MPC_SSH_BIN as in install.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh

[ $# -ge 1 ] && [ "${1#-}" = "$1" ] || { sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
HOST=$1; shift
APPLY=(); MODE=vst2; UNDO=()
for a in "$@"; do
  case "$a" in
    --apply) APPLY=(--apply) ;; --retire) MODE=vst2retire ;; --unretire) MODE=vst2retire; UNDO=(--undo) ;;
    --labels) MODE=vst2labels ;; --unlabel) MODE=vst2labels; UNDO=(--undo) ;;
    *) echo "unknown option $a"; exit 1 ;;
  esac
done

ssh_setup "$HOST"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"; ssh_close' EXIT
"${SSH[@]}" true || { echo "can't log in to ${MPC_SSH_USER:-root}@$HOST over SSH"; exit 1; }

if [ $MODE != vst2 ]; then side $MODE ${APPLY[@]+"${APPLY[@]}"} ${UNDO[@]+"${UNDO[@]}"}; exit; fi

# where each plugin of this repo keeps its data: vst.json's MODULE_SUBDIR, else the folder MODULE_DIR names
plugin_dirs | python3 -I -c '
import json, os, sys
for d in sys.stdin.read().split():
    j = json.load(open(os.path.join(d, "vst.json")))
    defs = j.get("defines") or {}
    sub = str(defs.get("MODULE_SUBDIR", "")).strip("\"") or os.path.basename(str(defs.get("MODULE_DIR", "")).strip("\""))
    if j.get("so") and sub and sub != ".":
        print(j["so"], sub)
' > "$TMP/vst2-map.txt"

"${SSH[@]}" "rm -rf $STAGE && mkdir -p $STAGE"
tar -C "$TMP" -cf - vst2-map.txt | "${SSH[@]}" "tar -C $STAGE -xf -"
side vst2 "${APPLY[@]}" "$STAGE/vst2-map.txt"
"${SSH[@]}" "rm -rf $STAGE"
if [ ${#APPLY[@]} -gt 0 ]; then
  echo "Restart MPC to see them under External Plugins (save your project first). Then check a few, and run"
  echo "  tools/vst2-move.sh $HOST --retire --apply"
fi
