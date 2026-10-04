#!/usr/bin/env bash
# Install plugins from this repo onto an MPC OS device over SSH (see INSTALL.md first):
#   ./install.sh <mpc address> <plugin|group> [...] [--yes] [--register] [--dry-run]
#     plugin   a folder name, e.g. hera, grids, rampage (./install.sh --list shows them all)
#     group    all, instruments, sequencers, effects, schwung, mutable-instruments, vcv-rack
#     --yes        don't ask before restarting MPC
#     --register   (re)write the plugin-list entries even for plugins MPC already lists
#     --dry-run    check the device and say what would happen; change nothing
# Copies each plugin's deploy/ folder (the .so to /sdcard/vst, its screen to /sdcard/Synths) and its presets/<name>/
# folder (merged into /sdcard/vst/<name>/; fetched first if missing: tools/fetch-presets.py), checks every file on the
# device against its SHA-256, then adds plugins MPC doesn't list
# yet to MPC.settings: that needs one MPC restart (MPC.settings is backed up first). Set MPC_SSH_KEY=<key file> to log
# in with a specific key, MPC_SSH_USER for a user other than root.
set -euo pipefail
cd "$(dirname "$0")"
REPO=$PWD

usage() { sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-1}"; }
. tools/common.sh

[ "${1:-}" = --list ] && { plugin_dirs | sed 's|^|  |'; exit 0; }
[ "${1:-}" = -h ] || [ "${1:-}" = --help ] && usage 0
[ $# -ge 2 ] || usage
HOST=$1; shift
YES=0; FORCE_REG=0; DRY=0; DIRS=()
for a in "$@"; do
  case "$a" in
    --yes) YES=1 ;; --register) FORCE_REG=1 ;; --dry-run) DRY=1 ;;
    -*) echo "unknown option $a"; usage ;;
    *) found=$(lookup "$a") || exit 1; DIRS+=($found) ;;
  esac
done
[ ${#DIRS[@]} -gt 0 ] || usage
DIRS=($(printf '%s\n' "${DIRS[@]}" | awk '!seen[$0]++'))
for d in "${DIRS[@]}"; do
  [ -f "$d/deploy/pluginlist-entry.xml" ] || { echo "$d has no deploy/ folder (build it first: BUILDING.md)"; exit 1; }
done

ssh_setup "$HOST"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"; ssh_close' EXIT
if tar --version 2>/dev/null | grep -q bsdtar; then TAR=(env COPYFILE_DISABLE=1 tar --no-mac-metadata); else TAR=(tar); fi
if command -v sha256sum >/dev/null; then SHA=(sha256sum); else SHA=(shasum -a 256); fi

echo "== $HOST"
"${SSH[@]}" true || { echo "can't log in to ${MPC_SSH_USER:-root}@$HOST over SSH (INSTALL.md, step 1)"; exit 1; }
side check

echo "== plugins (${#DIRS[@]})"
NEW=(); SOS=(); OLD_SKINS=()
for d in "${DIRS[@]}"; do SOS+=("$(attr file "$d/deploy/pluginlist-entry.xml")"); done
LISTED=$(side listed "${SOS[@]}")
for i in "${!DIRS[@]}"; do
  d=${DIRS[$i]}; e=$d/deploy/pluginlist-entry.xml; so=${SOS[$i]}
  have=$(printf '%s\n' "$LISTED" | grep -F "$so|" | head -n 1)
  want="$so|$(attr name "$e")|$(attr manufacturer "$e")|$(attr isInstrument "$e")|$(attr category "$e")"
  if [ "$have" = "$so||||" ]; then state="new: MPC needs to list it"; NEW+=("$d")
  elif [ "$have" != "$want" ] || [ $FORCE_REG = 1 ]; then state="listed, entry to update"; NEW+=("$d")
    # a new name or maker means a new screen folder (<maker> - VST - <name>): the old one goes once MPC lists the new
    IFS='|' read -r _ oname omaker _ <<< "$have"
    if [ -n "$oname" ] && { [ "$oname" != "$(attr name "$e")" ] || [ "$omaker" != "$(attr manufacturer "$e")" ]; }; then
      OLD_SKINS+=("/sdcard/Synths/$omaker - VST - $oname"); state="$state (was $oname)"
    fi
  else state="listed"; fi
  printf "  %-34s %-30s %s\n" "$d" "$(attr name "$e")" "$state"
done
[ $DRY = 1 ] && { echo "dry run: nothing changed"; exit 0; }

# presets, kits, wavetables... live in presets/<plugin>/ (not in git): fetch any that are missing
HAS_PRESETS=" $(python3 tools/fetch-presets.py --list 2>/dev/null) "
MISSING=()
for d in "${DIRS[@]}"; do
  p=$(basename "$d")
  case "$HAS_PRESETS" in *" $p "*) [ -n "$(ls -A "presets/$p" 2>/dev/null)" ] || MISSING+=("$p") ;; esac
done
if [ ${#MISSING[@]} -gt 0 ]; then
  echo "== fetching presets for ${MISSING[*]} (from their original projects, into presets/)"
  python3 tools/fetch-presets.py "${MISSING[@]}" ||
    echo "   couldn't fetch them: those plugins are installed without their presets (python3 tools/fetch-presets.py, then install again)"
fi

echo "== copying"
"${SSH[@]}" "rm -rf $STAGE && mkdir -p $STAGE"
"${TAR[@]}" -C tools -cf - plugin_list.awk | "${SSH[@]}" "tar -C $STAGE -xf -"
for d in "${DIRS[@]}"; do
  p=$(basename "$d")
  mkdir -p "$TMP/$p"
  { (cd "$d/deploy" && find vst Synths -type f | sort | while IFS= read -r f; do "${SHA[@]}" "$f"; done)
    if [ -d "presets/$p" ]; then   # its data folder: presets/<plugin>/ -> /sdcard/vst/<plugin>/
      (cd presets && find "$p" -type f -not -path "$p/not-installed/*" | sort | while IFS= read -r f; do
        echo "$("${SHA[@]}" "$f" | cut -d' ' -f1)  vst/$f"; done)
    fi; } > "$TMP/$p/SHA256SUMS"
  "${TAR[@]}" -cf - -C "$d/deploy" vst Synths -C "$TMP/$p" SHA256SUMS | "${SSH[@]}" "mkdir -p $STAGE/$p && tar -C $STAGE/$p -xf -"
  if [ -d "presets/$p" ]; then
    "${TAR[@]}" -cf - -C presets --exclude "$p/not-installed" "$p" | "${SSH[@]}" "mkdir -p $STAGE/$p/vst && tar -C $STAGE/$p/vst -xf -"
  fi
done
side place "$STAGE" $(for d in "${DIRS[@]}"; do basename "$d"; done)

if [ ${#NEW[@]} -gt 0 ]; then
  echo "== MPC's plugin list: ${#NEW[@]} to add or update"
  for d in "${NEW[@]}"; do cat "$d/deploy/pluginlist-entry.xml"; echo; done | grep . > "$TMP/entries.xml"
  "${TAR[@]}" -C "$TMP" -cf - entries.xml | "${SSH[@]}" "tar -C $STAGE -xf -"
  ok=$YES
  if [ $ok = 0 ]; then
    printf "MPC has to restart to list them (about 30 s). Save your project on the MPC first. Restart now? [y/N] "
    read -r a || a=""; case "$a" in y|Y|yes) ok=1 ;; esac
  fi
  if [ $ok = 1 ]; then
    side register "$STAGE/entries.xml"
    if [ ${#OLD_SKINS[@]} -gt 0 ]; then   # by file: the paths have spaces
      printf '%s\n' "${OLD_SKINS[@]}" > "$TMP/oldskins.txt"
      "${TAR[@]}" -C "$TMP" -cf - oldskins.txt | "${SSH[@]}" "tar -C $STAGE -xf -"
      side rmskins "$STAGE/oldskins.txt"
    fi
  else
    echo "not now: the files are in place; run the same command again with --yes when MPC can restart"
  fi
fi
"${SSH[@]}" "rm -rf $STAGE"
echo "done. On the MPC: add a new plugin to a track from the plugin browser; for an updated one, remove it from its"
echo "track and insert it again (a fresh insert loads the new version)."
