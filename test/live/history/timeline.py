#!/usr/bin/env python3
"""Per-scene render time across the sampled history, oldest sample first.

    python timeline.py pro                 every scene
    python timeline.py pro gm1 t40         scenes matching a substring
    python timeline.py pro --stable        only scenes present at every sample

Reads the `hist<index>-<commit>` records `series.sh` saved plus the board's record
at HEAD, and orders them by branch index rather than by commit, because a record's
commit field is the worktree's and says nothing about the firmware in it.

`render` only. It is the one metric whose meaning holds across the branch: `line`
moved at index 87 to close after the diagnostic overlay draws.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import results


def samples(board, head_tag=None):
    """Every historical record for this board, by branch index, with the tip last."""
    found = []
    for tag in results.available():
        # the middle segment is the worktree's commit and may carry -dirty, so it is
        # not a single \w run; the -hist<n>- anchor is what splits the name
        match = re.match(r"^%s-[\w-]+-hist(\d+)-(\w+)$" % board, tag)
        if match:
            found.append((int(match.group(1)), match.group(2), tag))
    found.sort()

    # The tip is a record for this board with no hist suffix. Pick it by the date
    # inside the record: a commit hash sorts alphabetically, which silently chooses
    # whichever old record happens to start with the highest character.
    if head_tag is None:
        candidates = [t for t in results.available()
                      if t.startswith(board + "-") and "-hist" not in t]
        dated = []
        for tag in candidates:
            try:
                dated.append((results.read(tag)["run"].get("date", ""), tag))
            except (SystemExit, KeyError, ValueError):
                continue
        head_tag = max(dated)[1] if dated else None
    if head_tag:
        found.append((999, "tip", head_tag))
    return found


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    board = sys.argv[1]
    args = sys.argv[2:]
    stable_only = "--stable" in args
    filters = [a for a in args if not a.startswith("--")]

    found = samples(board)
    if not found:
        sys.exit("no history records for %s - run series.sh %s first" % (board, board))

    perf = []
    for idx, commit, tag in found:
        record = results.read(tag)
        perf.append((idx, commit, record["perf"].get("one", {})))

    names = set(perf[-1][2])
    if stable_only:
        for _, _, p in perf:
            names &= set(p)
    if filters:
        names = {n for n in names if any(f in n for f in filters)}

    header = "".join("%9s" % ("#%d" % i if c != "HEAD" else "HEAD") for i, c, _ in perf)
    print("%-42s%s%10s" % ("scene", header, "first->last"))
    for name in sorted(names):
        cells, first, last = "", None, None
        for _, _, p in perf:
            value = p.get(name, {}).get("render")
            cells += "%9s" % ("-" if value is None else "%.2f" % value)
            if value is not None:
                first = value if first is None else first
                last = value
        delta = "%+.2f" % (last - first) if (first is not None and last is not None) else ""
        print("%-42s%s%10s" % (name, cells, delta))

    print("\nsamples: " + ", ".join("#%s=%s" % (i, c) for i, c, _ in perf))


if __name__ == "__main__":
    main()
