#!/usr/bin/env bash
# Rebuild, test, preview and package every port (after a wrapper or generator change):
#   steve/tools/skin-redesign/build_all.sh [port ...]      (no args: all of them)
# Prints one result line per port; details in each port's logs/.
set -u
cd "$(dirname "$0")/../../.."
B=steve/tools/skin-redesign/build.sh
declare -A DATA=(
  [braids]="src/presets:presets" [hera]="src/presets:presets" [obxd]="src/presets:presets"
  [libpo32]="src/kits:kits" [mrdrums]="src/kits:kits" [tablor]="src/wavetables:wavetables"
  [groovebank]="src/patterns:patterns" [midiplayer]="data/MIDI:MIDI"
)
ALL=(303 braids hank hera hush1 libpo32 moog mrdrums mrhyde noisemaker obxd
     nusaw aphex denis wurl fizzik monksynth chordism monovoice tablor helm
     eucalypso superarp groovebank pixelwalkers mazelite midiplayer   # MIDI sequencers
     plaits rings elements grids marbles                              # Mutable Instruments
     verglas warps ringsfx                                            # audio effects
     rampage)                                                         # VCV Rack modules
[ $# -gt 0 ] && ALL=("$@")
for p in "${ALL[@]}"; do
  printf "== %-11s " "$p"
  if [ "$p" = helm ]; then
    out=$("$B" helm "src/dsp/helm/patches/Factory Presets:helm-data/patches/Factory Presets" \
          "src/data/Move Organ.helm:helm-data/patches/Factory Presets/Keys/Move Organ.helm" 2>&1)
  elif [ -n "${DATA[$p]:-}" ]; then
    out=$("$B" "$p" ${DATA[$p]} 2>&1)
  else
    out=$("$B" "$p" 2>&1)
  fi
  echo "$out" | grep -E "test:|^Traceback|rror" | head -n 2 | tr '\n' ' '
  echo
done
