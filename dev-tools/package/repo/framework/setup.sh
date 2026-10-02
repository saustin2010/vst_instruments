#!/usr/bin/env bash
# Fetch the build framework these plugins are built with: sd88me's mpc-vst-plugins (the VST2 wrapper, the Schwung
# adapter, the skin generator and the offline test) at the commit this repo was made against, plus this repo's changes
# to it (mpc-vst-plugins.patch). Run once before tools/build.sh:
#   framework/setup.sh            -> framework/mpc-vst-plugins/ (ignored by git)
# Safe to run again: it only clones once and doesn't apply the patch twice.
set -euo pipefail
cd "$(dirname "$0")"
URL=https://github.com/sd88me/mpc-vst-plugins.git
BASE=@BASE@
[ -d mpc-vst-plugins/.git ] || git clone "$URL" mpc-vst-plugins
cd mpc-vst-plugins
git fetch -q origin 2>/dev/null || true
if git apply -R --check ../mpc-vst-plugins.patch 2>/dev/null; then
  echo "framework/mpc-vst-plugins: patch already applied"
  exit 0
fi
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "framework/mpc-vst-plugins has local changes; not touching it"; exit 1; }
git -c advice.detachedHead=false checkout -q "$BASE"
git apply ../mpc-vst-plugins.patch
echo "framework/mpc-vst-plugins: $(git rev-parse --short HEAD) + mpc-vst-plugins.patch"
