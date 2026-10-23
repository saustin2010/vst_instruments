#!/usr/bin/env bash
# Fetch the tools the plugins' own repos are released with (docs/catalogue-migration.md): sd88me's mpc-vst-plugins at
# a recent main plus the changes offered to it, as the branch steve-features of saustin2010/mpc-vst-plugins (since
# 2026-10-23 with his wrapper fix #486-#489 on top), at the commit the plugins' .github/workflows/release.yml pin.
# framework/setup.sh checks out the same commit for tools/build.sh. Only ever add commits to that branch: older
# releases pin its older commits.
#   framework/setup-release.sh      -> framework/release-tools/ (ignored by git); dev-tools/catalogue/check.sh uses it
set -euo pipefail
cd "$(dirname "$0")"
URL=https://github.com/saustin2010/mpc-vst-plugins.git
BRANCH=steve-features
REF=e3a0af4e8cb7eacd4bfcc2fbcbcde834aef50fa4   # keep equal to tools_ref in the plugins' release.yml
[ -d release-tools/.git ] || git clone -q -b "$BRANCH" "$URL" release-tools
cd release-tools
git fetch -q "$URL" "$BRANCH" 2>/dev/null || true
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "framework/release-tools has local changes; not touching it"; exit 1; }
git -c advice.detachedHead=false checkout -q "$REF"
echo "framework/release-tools: $(git rev-parse --short HEAD) ($URL $BRANCH)"
