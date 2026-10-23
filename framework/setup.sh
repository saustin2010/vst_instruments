#!/usr/bin/env bash
# Fetch the build framework these plugins are built with: sd88me's mpc-vst-plugins (the VST2 wrapper, the Schwung
# adapter, the skin generator and the offline test) with the changes offered to him, as a branch of the fork
# saustin2010/mpc-vst-plugins, at the commit framework/setup-release.sh pins (the one the plugins' releases build
# with: one framework for both since 2026-10-23). Run once before tools/build.sh:
#   framework/setup.sh            -> framework/mpc-vst-plugins/ (ignored by git)
# Safe to run again: it moves a clean checkout to the pin and leaves one with local changes alone.
set -euo pipefail
cd "$(dirname "$0")"
eval "$(grep -E '^(URL|BRANCH|REF)=' setup-release.sh)"   # the pin lives in setup-release.sh
[ -d mpc-vst-plugins/.git ] || git clone -q -b "$BRANCH" "$URL" mpc-vst-plugins
cd mpc-vst-plugins
git fetch -q "$URL" "$BRANCH" 2>/dev/null || true
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
  echo "framework/mpc-vst-plugins has local changes; not touching it"
  echo "(a checkout made by the old setup.sh, with mpc-vst-plugins.patch applied: git -C framework/mpc-vst-plugins checkout -- . && rm -f framework/mpc-vst-plugins/wrapper/scope.h, then run this again)"
  exit 1
fi
git -c advice.detachedHead=false checkout -q "$REF"
echo "framework/mpc-vst-plugins: $(git rev-parse --short HEAD) ($URL $BRANCH)"
