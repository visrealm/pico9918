#!/usr/bin/env python3
"""Run the renderer's suite against the shim.

    cmake -S test/suite/shim -B build-suite -DCMAKE_C_FLAGS=-O2
    cmake --build build-suite
    python test/suite/run.py            (or: cd test && python -m suite.run)

111 scenes, five property suites and a TMS9900 GPU program, each compared against
a committed frame. It asserts what the library computes and never what it cost -
the microseconds belong to a device, and the firmware repository's runner is what
reads them.

`tools/ci.sh suite` does the build and runs both line-width tiers, which is what
CI uses. This is here for a single run against a shim you already have.
"""

import argparse
import os
import sys
import time

import suite.scenes as scenes
import suite.stages as stages
from suite.access.backend import backend_args, open_backend


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("filter", nargs="*", help="substrings; empty means every scene")
    ap.add_argument("--only", nargs="+", default=None, metavar="STAGE",
                    help="stages to run: " + ", ".join(stages.RENDERER))
    ap.add_argument("--skip", nargs="+", default=[], metavar="STAGE")
    ap.add_argument("--canaries", action="store_true",
                    help="restrict the scene stages to those that drop a line first")
    backend_args(ap)
    args = ap.parse_args()
    # the firmware's runner has a --quick that pairs with its timings; here every
    # stage is cheap, so the flag would only ever mean "run less of a 2 s suite"
    args.quick = False

    picked = args.only or stages.RENDERER
    unknown = (set(picked) | set(args.skip)) - set(stages.RENDERER)
    if unknown:
        raise SystemExit("no such stage: %s - there is %s"
                         % (", ".join(sorted(unknown)), ", ".join(stages.RENDERER)))
    chosen = [s for s in stages.RENDERER if s in picked and s not in args.skip]

    started = time.time()
    # "desktop" rather than a board name is load-bearing: `scenes.over_budget_on`
    # asks which board a scene is known not to fit, and no scene is over budget on
    # a machine that is not rendering in real time.
    record = {"run": {"board": "desktop"}, "freeze": {}, "perf": {},
              "properties": {}, "gpu": {}, "scenes": {}}
    with open_backend(args) as t:
        for stage in chosen:
            print("== %s" % stage)
            stages.RUNNERS[stage](t, record, args)

    print("\n%s in %.0f s" % (", ".join(chosen), time.time() - started))
    problems = stages.verdict(record)
    if problems:
        print("\nFAILED")
        for p in problems:
            print("  " + p)
        return 1
    print("\nPASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
