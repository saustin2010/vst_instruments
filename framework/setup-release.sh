#!/usr/bin/env bash
# Fetch the tools the plugins' own repos are released with (docs/catalogue-migration.md): sd88me's mpc-vst-plugins at
# a recent main plus the changes offered to it, as the branch steve-features of saustin2010/mpc-vst-plugins, at the
# commit the plugins' .github/workflows/release.yml pin. Until sd88me merges those changes, this differs from the
# framework tools/build.sh uses (framework/setup.sh: an older commit plus mpc-vst-plugins.patch).
#   framework/setup-release.sh      -> framework/release-tools/ (ignored by git); dev-tools/catalogue/check.sh uses it
set -euo pipefail
cd "$(dirname "$0")"
URL=https://github.com/saustin2010/mpc-vst-plugins.git
REF=6c87b6dc87334fdd04b5a1cb54a12d2fccbc97fd   # keep equal to tools_ref in the plugins' release.yml
[ -d release-tools/.git ] || git clone -q -b steve-features "$URL" release-tools
cd release-tools
git fetch -q origin steve-features 2>/dev/null || true
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "framework/release-tools has local changes; not touching it"; exit 1; }
git -c advice.detachedHead=false checkout -q "$REF"
echo "framework/release-tools: $(git rev-parse --short HEAD) (saustin2010/mpc-vst-plugins steve-features)"
