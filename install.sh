#!/usr/bin/env bash
# Install plugins from this repo onto an MPC OS device over SSH (see INSTALL.md first):
#   ./install.sh <mpc address> <plugin|group> [...] [--yes] [--register] [--dry-run] [--vst2|--legacy]
#     plugin   a folder name, e.g. hera, grids, rampage (./install.sh --list shows them all)
#     group    all, instruments, sequencers, effects, schwung, mutable-instruments, vcv-rack, originals, mpc-ports
#     --yes        don't ask before restarting MPC
#     --register   (re)write the plugin-list entries even for plugins MPC already lists (--legacy)
#     --dry-run    check the device and say what would happen; change nothing
#     --vst2, --legacy   install the MPC OS 3.10.1 way or the older way (default: whichever this MPC OS uses)
# MPC OS 3.10.1 and later: each plugin's deploy/ folder and its presets/<name>/ folder (fetched first if missing:
# tools/fetch-presets.py) become /sdcard/VST2 Plugins/<maker> - VST - <name>/ with a plugin.json, checked on the device
# against their SHA-256. A new plugin shows after an MPC restart; MPC.settings isn't touched.
# Older MPC OS: the .so goes to /sdcard/vst, its screen to /sdcard/Synths, presets merged into /sdcard/vst/<name>/, then
# plugins MPC doesn't list yet are added to MPC.settings: that needs one MPC restart (MPC.settings is backed up first).
# Set MPC_SSH_KEY=<key file> to log in with a specific key, MPC_SSH_USER for a user other than root.
set -euo pipefail
cd "$(dirname "$0")"
REPO=$PWD

usage() { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-1}"; }
. tools/common.sh

[ "${1:-}" = --list ] && { plugin_dirs | sed 's|^|  |'; exit 0; }
[ "${1:-}" = -h ] || [ "${1:-}" = --help ] && usage 0
[ $# -ge 2 ] || usage
HOST=$1; shift
YES=0; FORCE_REG=0; DRY=0; DIRS=(); MODE=auto
for a in "$@"; do
  case "$a" in
    --yes) YES=1 ;; --register) FORCE_REG=1 ;; --dry-run) DRY=1 ;; --vst2) MODE=vst2 ;; --legacy) MODE=legacy ;;
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
if [ $MODE = auto ]; then
  case "$(side vst2mode)" in
    on) MODE=vst2 ;;
    off) MODE=vst2; echo "External Plugin Support is OFF (Preferences > Enhancements): MPC won't list or load these until it's on" ;;
    *) MODE=legacy ;;
  esac
fi

echo "== plugins (${#DIRS[@]}), $([ $MODE = vst2 ] && echo "into /sdcard/VST2 Plugins (MPC OS 3.10.1)" || echo "into /sdcard/vst and MPC's plugin list")"
NEW=(); SOS=(); OLD_SKINS=()
for d in "${DIRS[@]}"; do SOS+=("$(attr file "$d/deploy/pluginlist-entry.xml")"); done
if [ $MODE = vst2 ]; then
  STATE=$(side vst2state "${SOS[@]}")
  for i in "${!DIRS[@]}"; do
    d=${DIRS[$i]}; e=$d/deploy/pluginlist-entry.xml; so=${SOS[$i]}
    F=$(basename "$(ls -d "$d"/deploy/Synths/*/ | head -n 1)")
    IFS='|' read -r _ hit old <<< "$(printf '%s\n' "$STATE" | grep -F "$so|" | head -n 1)"
    if [ -z "$hit" ]; then state="new: MPC lists it after a restart"; NEW+=("$d")
    elif [ "$hit" != "$F" ]; then state="new name: MPC lists it after a restart (was $hit)"; NEW+=("$d")
    else state="update"; fi
    [ "$old" = 1 ] && state="$state, and its pre-3.10.1 copy"
    printf "  %-34s %-30s %s\n" "$d" "$(attr name "$e")" "$state"
  done
else
# MPC writes characters outside ASCII as XML entities (manufacturer="&#201;milie Gillet"): decode them, so the same name
# reads the same as the entry's UTF-8 and no listed plugin looks changed
LISTED=$(side listed "${SOS[@]}" | python3 -c 'import html, sys; sys.stdout.write(html.unescape(sys.stdin.read()))')
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
fi
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
  EXTRA=()
  if [ $MODE = vst2 ]; then   # plugin.json from the plugin-list entry; VST_DATA: a data folder read from a fixed /sdcard/vst path
    python3 -I tools/plugin-json.py "$d/deploy/pluginlist-entry.xml" "$TMP/$p" "$d/vst.json"
    EXTRA=(plugin.json VST_DATA)
  fi
  { (cd "$d/deploy" && find vst Synths -type f | sort | while IFS= read -r f; do "${SHA[@]}" "$f"; done)
    [ $MODE = vst2 ] && (cd "$TMP/$p" && "${SHA[@]}" plugin.json)
    if [ -d "presets/$p" ]; then   # its data folder: presets/<plugin>/ -> /sdcard/vst/<plugin>/
      (cd presets && find "$p" -type f -not -path "$p/not-installed/*" | sort | while IFS= read -r f; do
        echo "$("${SHA[@]}" "$f" | cut -d' ' -f1)  vst/$f"; done)
    fi; } > "$TMP/$p/SHA256SUMS"
  "${TAR[@]}" -cf - -C "$d/deploy" vst Synths -C "$TMP/$p" SHA256SUMS ${EXTRA[@]+"${EXTRA[@]}"} | "${SSH[@]}" "mkdir -p $STAGE/$p && tar -C $STAGE/$p -xf -"
  if [ -d "presets/$p" ]; then
    "${TAR[@]}" -cf - -C presets --exclude "$p/not-installed" "$p" | "${SSH[@]}" "mkdir -p $STAGE/$p/vst && tar -C $STAGE/$p/vst -xf -"
  fi
done
if [ $MODE = vst2 ]; then
  OUT=$(side place_vst2 "$STAGE" $(for d in "${DIRS[@]}"; do basename "$d"; done))
  printf '%s\n' "$OUT" | grep -v '^new: '
  NNEW=$(printf '%s\n' "$OUT" | sed -n 's/^new: //p')
  "${SSH[@]}" "rm -rf $STAGE"
  if [ "${NNEW:-0}" -gt 0 ]; then
    ok=$YES
    if [ $ok = 0 ]; then
      printf "MPC lists %s new plugin(s) after a restart (about 30 s). Save your project on the MPC first. Restart now? [y/N] " "$NNEW"
      read -r a || a=""; case "$a" in y|Y|yes) ok=1 ;; esac
    fi
    if [ $ok = 1 ]; then side restart; else echo "not now: they show under External Plugins after MPC's next start"; fi
  fi
  echo "done. On the MPC: new plugins are under External Plugins in the plugin browser; for an updated one, remove it"
  echo "from its track and insert it again (a changed screen shows after an MPC restart)."
  exit 0
fi
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
