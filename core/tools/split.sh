#!/usr/bin/env sh
#
# Project: pico9918-core
#
# Produce the published library history from the firmware repository.
#
# Copyright (c) 2026 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918-core
#
# This library is generated, not developed in place: it lives at core/ in the
# firmware repository, where a change can be validated against a device, and is
# split out from there. This script is that split.
#
# It writes a local branch and stops. It has no remote, no push and no tag - a
# publish is a deliberate act, and `origin` on the firmware side is a public
# repository with an unprotected default branch.
#
# Usage: tools/split.sh [branch]        (default: publish)
#
# Run it from the FIRMWARE repository root, not from core/ - subtree walks the
# whole history and needs the firmware's refs.
#
# Why not `git subtree split --prefix=core --onto=<pre>`, which is what the
# documentation suggests: --onto primes the split cache with identity mappings
# for the commits in the ref it is given. Those are synthetic commits that do not
# appear in the firmware history, so nothing matches, and the walk emits the
# entire firmware history verbatim with one "delete everything but the library"
# commit on top. The tip tree is right; the ancestry is 1024 firmware commits and
# a boundary commit deleting the firmware. Measured, not assumed.
#
# What this does instead: split the pre-move path to recover the library's real
# history, split the post-move path to get the current tree, and rebase the second
# onto the first. The move commit's library-side diff is empty, which is why
# dropping it as the rebase base is exactly right and the replay is clean.
#
# Expect 60-110 seconds per split - each walks the whole firmware history. It is
# not hung.

set -eu

# The commit that moved the library from submodules/vrEmuTms9918 to core/. Its
# parent is the last commit where the old path exists, and that is the checkout
# the first split needs: `git subtree split` refuses a prefix that is absent from
# the current tree, which a detached checkout of the parent lifts.
MOVE=85c4291
PREFIX_BEFORE=submodules/vrEmuTms9918
PREFIX_AFTER=core

BRANCH=${1:-publish}
SOURCE=${SPLIT_SOURCE:-core}

# --show-prefix, which is empty only at the repository root. Not a test for .git,
# which is a file in a worktree, and not --show-toplevel, which reports a native
# path that will not string-compare against an MSYS `pwd`.
PREFIX=$(git rev-parse --show-prefix 2> /dev/null) || {
  echo "split.sh: not a git repository" >&2; exit 1; }
[ -z "$PREFIX" ] && [ -d "$PREFIX_AFTER" ] || {
  echo "split.sh: run from the firmware repository root, the directory holding $PREFIX_AFTER/" >&2
  exit 1
}
git diff --quiet && git diff --cached --quiet || {
  echo "split.sh: the tree has uncommitted changes; the split reads refs, not the worktree," >&2
  echo "          but it checks out other commits and would carry them around" >&2
  exit 1
}

START=$(git rev-parse --abbrev-ref HEAD)
cleanup() { git checkout --quiet "$START" 2> /dev/null || true; }
trap cleanup EXIT

echo "split.sh: the library's history before the move ($PREFIX_BEFORE)"
git checkout --quiet --detach "$MOVE^"
git branch -D "$BRANCH-pre" 2> /dev/null || true
git subtree split --prefix="$PREFIX_BEFORE" -b "$BRANCH-pre" > /dev/null

echo "split.sh: the library's tree after the move ($PREFIX_AFTER)"
git checkout --quiet "$SOURCE"
git branch -D "$BRANCH-post" 2> /dev/null || true
git subtree split --prefix="$PREFIX_AFTER" -b "$BRANCH-post" > /dev/null

# The root of -post is the move commit. Replaying everything ABOVE it onto -pre
# joins the two halves without it.
echo "split.sh: joining them"
git branch -f "$BRANCH" "$BRANCH-post"
git rebase --onto "$BRANCH-pre" "$(git rev-list --max-parents=0 "$BRANCH-post")" "$BRANCH" \
  > /dev/null

git checkout --quiet "$START"
trap - EXIT

ROOT=$(git rev-list --max-parents=0 "$BRANCH")
echo
echo "$BRANCH: $(git rev-list --count "$BRANCH") commits, root $ROOT"
echo "  $(git log -1 --format='%ad %s' --date=short "$ROOT")"
echo "  tip $(git log -1 --format='%h %s' "$BRANCH")"
echo
# The check that matters: a firmware commit in the published ancestry would ship
# the whole firmware history under a library's name.
if git merge-base --is-ancestor "$SOURCE" "$BRANCH" 2> /dev/null; then
  echo "split.sh: FAILED - $SOURCE is an ancestor of $BRANCH, so the firmware history came with it" >&2
  exit 1
fi
echo "no firmware commit is an ancestor. Nothing has been pushed."
