#!/usr/bin/env python3
"""Split the sampled history into what got faster and what did not exist yet.

    python gains.py pro
    python gains.py 2040

A scene the harness sets up renders on any vintage, so a build that cannot do the
feature draws something cheaper and reads faster. Comparing #32 with HEAD across
every scene therefore mixes two different things: real optimisation on what already
worked, and the whole cost of a capability that arrived. This separates them by
looking for the step, so the classification comes out of the readings rather than
out of an assumption about which mode had what.

A scene is taken to have gained its feature mid-branch if some consecutive pair of
samples rises by more than a third and by more than 2 us - far outside the
between-build floor on either board, and the observed steps are 5 to 40 us.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import results
from timeline import samples

# Indices 119-121 have the 8bpp tier without the RP2040 fixes that landed at 122.
# The RP2040 will not run them at all; the PRO does not crash but is not doing the
# work either, so its readings there are low for a reason that is not performance.
UNUSABLE = {119}

STEP_RATIO = 1.34
STEP_US = 2.0


def classify(values):
    """(arrived_at_index, first, last) - arrived_at_index None if it always worked."""
    usable = [(i, v) for i, v in values if v is not None and i not in UNUSABLE]
    if len(usable) < 2:
        return None, None, None
    arrived = None
    for (_, before), (idx, after) in zip(usable, usable[1:]):
        if after > before * STEP_RATIO and after - before > STEP_US:
            arrived = idx
    return arrived, usable[0][1], usable[-1][1]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    board = sys.argv[1]
    found = samples(board)
    if not found:
        sys.exit("no history records for %s" % board)

    perf = [(i, results.read(t)["perf"].get("one", {})) for i, _, t in found]
    names = sorted(perf[-1][1])

    existed, gained = [], []
    for name in names:
        values = [(i, p.get(name, {}).get("render")) for i, p in perf]
        arrived, first, last = classify(values)
        if first is None:
            continue
        (gained if arrived else existed).append((name, first, last, arrived))

    print("board %s, %d samples, %d scenes\n" % (board, len(perf), len(existed) + len(gained)))

    deltas = sorted(last - first for _, first, last, _ in existed)
    n = len(deltas)
    print("ALREADY WORKED AT THE BRANCH POINT - %d scenes" % n)
    print("  render delta: median %+.2f  mean %+.2f  best %+.2f  worst %+.2f"
          % (deltas[n // 2], sum(deltas) / n, deltas[0], deltas[-1]))
    print("  faster %d, slower %d" % (sum(1 for d in deltas if d < 0),
                                      sum(1 for d in deltas if d > 0)))
    for name, first, last, _ in sorted(existed, key=lambda r: r[2] - r[1])[:8]:
        print("    %-38s %6.2f -> %6.2f  %+.2f" % (name, first, last, last - first))

    print("\nGAINED ITS FEATURE ON THE BRANCH - %d scenes" % len(gained))
    by_sample = {}
    for name, first, last, arrived in gained:
        by_sample.setdefault(arrived, []).append((name, first, last))
    for idx in sorted(by_sample):
        rows = by_sample[idx]
        cost = sum(l - f for _, f, l in rows) / len(rows)
        print("  arrived by #%-4s %2d scenes, mean cost %+.2f us: %s"
              % (idx, len(rows), cost,
                 ", ".join(sorted(n for n, _, _ in rows)[:6])
                 + (" ..." if len(rows) > 6 else "")))


if __name__ == "__main__":
    main()
