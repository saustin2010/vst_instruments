#!/usr/bin/env bash
# Remove plugins installed from this repo from an MPC OS device over SSH:
#   ./uninstall.sh <mpc address> <plugin|group> [...] [--yes] [--purge]
# Stops MPC, backs up MPC.settings, drops the plugins' entries from its plugin list, deletes their .so and screen
# folders, and starts MPC again. Their data folders (/sdcard/vst/<name>/: presets, kits, your own files) are kept
# unless you add --purge. Remove the plugins from your projects' tracks first. Same groups and SSH settings as install.sh.
set -euo pipefail
cd "$(dirname "$0")"
[ $# -ge 2 ] || { sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
HOST=$1; shift
YES=0; PURGE=0; NAMES=()
for a in "$@"; do
  case "$a" in --yes) YES=1 ;; --purge) PURGE=1 ;; -*) echo "unknown option $a"; exit 1 ;; *) NAMES+=("$a") ;; esac
done
. tools/common.sh
DIRS=()
for n in "${NAMES[@]}"; do found=$(lookup "$n") || exit 1; DIRS+=($found); done
[ ${#DIRS[@]} -gt 0 ] || { echo "which plugins?"; exit 1; }
DIRS=($(printf '%s\n' "${DIRS[@]}" | awk '!seen[$0]++'))

ssh_setup "$HOST"
trap ssh_close EXIT

SOS=(); PATHS=()
for d in "${DIRS[@]}"; do
  e=$d/deploy/pluginlist-entry.xml
  so=$(attr file "$e"); SOS+=("$so"); PATHS+=("$so")
  for s in "$d"/deploy/Synths/*/; do PATHS+=("/sdcard/Synths/$(basename "$s")"); done
  if [ $PURGE = 1 ] && grep -q MODULE_DIR "$d/vst.json"; then PATHS+=("/sdcard/vst/$(basename "$d")"); fi
  echo "  $d: $(attr name "$e")"
done
printf '    %s\n' "${PATHS[@]}"
if [ $YES = 0 ]; then
  printf "Remove these %d plugins? MPC will stop and restart (save your project first). [y/N] " "${#DIRS[@]}"
  read -r a || a=""; case "$a" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi
"${SSH[@]}" "rm -rf $STAGE && mkdir -p $STAGE"
COPYFILE_DISABLE=1 tar -C tools -cf - plugin_list.awk | "${SSH[@]}" "tar -C $STAGE -xf -"
side check
side unregister "${SOS[@]}" -- "$(printf '%q ' "${PATHS[@]}")"
"${SSH[@]}" "rm -rf $STAGE"
echo "done."
