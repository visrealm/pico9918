#!/usr/bin/env sh
#
# Project: pico9918-core
#
# Publish the library's current tree to visrealm/pico9918-core.
#
# Copyright (c) 2026 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918-core
#
# The library is generated, not developed in place: it lives at core/ in the
# firmware repository, where a change can be validated against a device, and is
# published from there. This script is that publish.
#
# It writes a local branch and stops. It has no push and no tag - a publish is a
# deliberate act, and the message it commits is the one the world reads.
#
# Usage: tools/publish.sh [-m <message>] [branch]     (default branch: publish)
#
# Run it from the FIRMWARE repository root, not from core/.
#
# This is not split.sh. That one reconstructed the library's history through the
# commit that moved it out of submodules/vrEmuTms9918, which was a one-time
# bootstrap; the result is published and settled. Here the published history is
# the parent, the library's current tree is the content, and nothing walks the
# firmware history at all - so this takes a second rather than two minutes.

set -eu

PREFIX=core
UPSTREAM=https://github.com/visrealm/pico9918-core.git
BRANCH=publish
SOURCE=${PUBLISH_SOURCE:-HEAD}
MESSAGE=

while [ $# -gt 0 ]; do
  case $1 in
    -m) MESSAGE=$2; shift 2 ;;
    -*) echo "publish.sh: unknown option $1" >&2; exit 1 ;;
    *)  BRANCH=$1; shift ;;
  esac
done

# --show-prefix, which is empty only at the repository root. Not a test for .git,
# which is a file in a worktree.
AT=$(git rev-parse --show-prefix 2> /dev/null) || {
  echo "publish.sh: not a git repository" >&2; exit 1; }
[ -z "$AT" ] && [ -d "$PREFIX" ] || {
  echo "publish.sh: run from the firmware repository root, the directory holding $PREFIX/" >&2
  exit 1
}

# The tree comes from a ref, so anything uncommitted would be silently left out.
git diff --quiet && git diff --cached --quiet || {
  echo "publish.sh: the tree has uncommitted changes, and they would not be published" >&2
  exit 1
}

TREE=$(git rev-parse "$SOURCE:$PREFIX")

echo "publish.sh: fetching the published tip"
git fetch --quiet "$UPSTREAM" main
PARENT=$(git rev-parse FETCH_HEAD)

if [ "$(git rev-parse "$PARENT^{tree}")" = "$TREE" ]; then
  echo "publish.sh: $PREFIX/ already matches $(git rev-parse --short "$PARENT") - nothing to publish"
  exit 0
fi

[ -n "$MESSAGE" ] || MESSAGE=$(git log -1 --format='%s' "$SOURCE")

git update-ref "refs/heads/$BRANCH" \
  "$(git commit-tree "$TREE" -p "$PARENT" -m "$MESSAGE")"

# The check that matters: a firmware commit in the published ancestry would ship
# the whole firmware history under a library's name.
if git merge-base --is-ancestor "$SOURCE" "$BRANCH" 2> /dev/null; then
  echo "publish.sh: FAILED - $SOURCE is an ancestor of $BRANCH, so the firmware history came with it" >&2
  exit 1
fi

echo
echo "$BRANCH: $(git log -1 --format='%h %s' "$BRANCH")"
echo "  onto  $(git log -1 --format='%h %s' "$PARENT")"
git diff --stat "$PARENT" "$BRANCH" | sed 's/^/  /'
echo
echo "no firmware commit is an ancestor. Nothing has been pushed. To publish:"
echo "  git push $UPSTREAM $BRANCH:main"
