#!/usr/bin/env bash
# Publish one plugin folder to its own repo, github.com/saustin2010/mpc-vst-<plugin>, where its releases are made and
# which the catalogue lists (docs/catalogue-migration.md). This repo stays the master:
#   tools/publish.sh <plugin> [--create] [--dry-run]
#     --create    make the GitHub repo first if it doesn't exist (public; description from the release workflow's "about")
#     --dry-run   split and say what would be pushed, push nothing
# `git subtree split` turns the folder's history into commits whose root is the folder; they go to the plugin repo's
# main. The same history always splits to the same commits, so a publish is a fast-forward. It publishes what is
# committed on the current branch, so the folder must have no uncommitted changes, and it refuses when the plugin repo
# has commits this repo lacks: a pull request merged there comes back first with
#   git subtree pull --prefix=<plugin folder> https://github.com/saustin2010/mpc-vst-<plugin> main
# The folder must be catalogue-ready: LICENSE and .github/workflows/release.yml (see the migration doc's checklist).
# MPC_PUBLISH_OWNER overrides the account. Needs git (with subtree) and, for --create, gh.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO"
. tools/common.sh

[ $# -ge 1 ] || { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
name=$1; shift
create=0 dry=0
for a in "$@"; do
  case "$a" in
    --create) create=1 ;;
    --dry-run) dry=1 ;;
    *) echo "unknown option $a"; exit 1 ;;
  esac
done
rel=$(lookup "$name")
[ "$(echo "$rel" | wc -l)" -eq 1 ] || { echo "$name: more than one plugin"; exit 1; }
plugin=$(basename "$rel")
owner=${MPC_PUBLISH_OWNER:-saustin2010}
target=$owner/mpc-vst-$plugin
url=https://github.com/$target

for f in LICENSE .github/workflows/release.yml README.md; do
  git cat-file -e "HEAD:$rel/$f" 2>/dev/null || { echo "$rel/$f is not committed: the folder isn't ready to publish"; exit 1; }
done
if ! git diff --quiet HEAD -- "$rel" || [ -n "$(git ls-files --others --exclude-standard -- "$rel")" ]; then
  echo "$rel has uncommitted changes: commit them first (a publish sends what is committed)"; exit 1
fi

echo "splitting $rel ..."
split=$(git subtree split -q --prefix="$rel" HEAD)
echo "  $split ($(git rev-list --count "$split") commits)"

remote=$(git ls-remote "$url" refs/heads/main 2>/dev/null | cut -f1 || true)
if [ -z "$remote" ] && ! git ls-remote "$url" >/dev/null 2>&1; then
  if [ $create = 1 ]; then
    about=$(sed -n 's/^ *about: *"\{0,1\}\([^"]*\)"\{0,1\} *$/\1/p' "$rel/.github/workflows/release.yml")
    desc="$about A native VST2 plugin with its own touchscreen skin for MPC OS standalone devices."
    if [ $dry = 1 ]; then
      echo "would create $target: $desc"
    else
      gh repo create "$target" --public --description "$desc" >/dev/null
      gh repo edit "$target" --add-topic mpc --add-topic akai-mpc --add-topic vst2 --add-topic mpc-vst-plugins >/dev/null
      echo "created $url"
    fi
  else
    echo "$url doesn't exist (or isn't reachable): add --create to make it"; exit 1
  fi
elif [ -n "$remote" ] && [ "$remote" != "$split" ]; then
  git fetch -q "$url" main
  git merge-base --is-ancestor FETCH_HEAD "$split" || {
    echo "$target has commits this repo lacks: bring them back first with"
    echo "  git subtree pull --prefix=$rel $url main"
    exit 1
  }
fi

if [ "$remote" = "$split" ]; then
  echo "$target is up to date"
elif [ $dry = 1 ]; then
  echo "would push $split to $target main (now: ${remote:-empty})"
else
  git push -q "$url" "$split:refs/heads/main"
  echo "pushed to $url (main = ${split:0:7})"
fi
