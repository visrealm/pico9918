#!/usr/bin/env python3
"""One entry point for the whole rig, and one record of what it found.

    python runner.py --board 2040                 everything, in the order that works
    python runner.py --board 2040 --quick         the canaries and the properties, ~1 min
    python runner.py --board 2040 --only freeze perf
    python runner.py --board 2040 --skip diag perf-panels
    python runner.py --board 2040 --flash         program the board first
    python runner.py --board 2040 --report        write an HTML report beside the record
    python runner.py --board 2040 --against 2040-abc1234    ...comparing against that run

Everything runs in **one openocd session**, which is what makes the whole rig
cheap enough to run every iteration. `d4` goes first out of the old habit, but
enforcing that rule here is what proved it was never enough: the state `d4`
inherited came from the *previous run*, not the previous stage, so it failed
even from first place. It now blanks the register file and VRAM itself.

The record it writes is the point. A verdict tells you this build passed; a record
tells you what moved since the last one, which scene started dropping a line, and
against which commit - none of which survives in scrollback.
"""

import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
# The library's half of the harness is the `suite` package under core/test, so
# one path reaches all of it. web/ is a folder rather than a package, and needs
# its own line.
CORE_TEST = os.path.join(os.path.dirname(os.path.dirname(HERE)), "core", "test")
for folder in (HERE, CORE_TEST, os.path.join(HERE, "web")):
    sys.path.insert(0, folder)
import suite.stages.freeze as freeze
import perf
import results
import suite.scenes as scenes
from live9918 import board_args, open_board
from suite.stages import RUNNERS as RENDERER_RUNNERS, say, verdict

# Order matters twice over: d4 first because it inherits board state, and the
# goldens before the timings because a dropped row is the acceptance test and an
# average is not - there is no point pricing a build that renders the wrong thing.
STAGES = ("d4", "freeze", "diag", "perf", "perf-panels",
          "scroll", "colour", "ecm", "t80-8bpp", "gpu")

# What --quick runs: the canary scenes, which drop a line before any average
# moves, plus the properties, which are cheap and catch what a golden cannot.
QUICK = ("d4", "freeze", "scroll", "colour", "ecm")

# The stages that measure the device rather than the renderer. `--desktop` runs
# everything else: the goldens and the properties assert what the renderer computes,
# which is the same in either place, and these three assert what it cost, which is
# not. Keeping the split in one tuple is what stops a new stage from quietly
# reporting PC microseconds as if they were a board's.
TIMED = ("diag", "perf", "perf-panels")

# The renderer's stages are the library's and are defined once, in
# core/test/suite/stages/. These three are this repository's: they measure a
# device, and a microsecond off a PC is not a smaller version of one.


def stage_diag(t, record, args):
    names = freeze.select(args.filter, args.canaries or args.quick)
    record["freeze"]["diag"] = freeze.run(t, names, diag=True, progress=say(freeze.line))


def stage_perf(t, record, args):
    record["perf"]["one"] = perf.run(t, perf_names(args), progress=say(perf.line))


def stage_perf_panels(t, record, args):
    record["perf"]["all"] = perf.run(t, perf_names(args), panels=True, progress=say(perf.line))


RUNNERS = dict(RENDERER_RUNNERS, **{"diag": stage_diag, "perf": stage_perf,
                                    "perf-panels": stage_perf_panels})


def perf_names(args):
    if args.canaries or args.quick:
        return list(scenes.CANARIES)
    return [n for n in scenes.SCENES if not args.filter or any(f in n for f in args.filter)]


def chosen(args):
    picked = args.only or (QUICK if args.quick else STAGES)
    unknown = (set(picked) | set(args.skip)) - set(STAGES)
    if unknown:
        raise SystemExit("no such stage: %s - there is %s"
                         % (", ".join(sorted(unknown)), ", ".join(STAGES)))
    stages = [s for s in STAGES if s in picked and s not in args.skip]
    if getattr(args, "desktop", False):
        # Dropped rather than refused, so `--desktop` needs no second spelling of the
        # stage list - and dropped rather than run, because a microsecond off a PC is
        # not a smaller version of a microsecond off the device, it is a different
        # measurement wearing the same name.
        timed = [s for s in stages if s in TIMED]
        stages = [s for s in stages if s not in TIMED]
        if timed:
            print("desktop: skipping %s - the device measures time, this does not\n"
                  % ", ".join(timed))
    return stages


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("filter", nargs="*", help="substrings; empty means every scene")
    ap.add_argument("--only", nargs="+", default=None, metavar="STAGE",
                    help="stages to run: " + ", ".join(STAGES))
    ap.add_argument("--skip", nargs="+", default=[], metavar="STAGE")
    ap.add_argument("--quick", action="store_true",
                    help="the canary scenes and the properties - what to run after every edit")
    ap.add_argument("--canaries", action="store_true",
                    help="restrict the scene stages to those that drop a line first")
    ap.add_argument("--flash", action="store_true", help="program the board first")
    ap.add_argument("--save", metavar="SUFFIX", nargs="?", const="", default=None,
                    help="write the record to runs/<board>-<commit>[-SUFFIX].json")
    ap.add_argument("--report", metavar="PATH", nargs="?", const="", default=None,
                    help="also write an HTML report; defaults to reports/<record name>.html")
    ap.add_argument("--against", metavar="TAG",
                    help="a saved run to compare against in the report; `last` picks the newest "
                         "record for this board")
    board_args(ap)
    args = ap.parse_args()

    stages = chosen(args)
    started = time.time()
    with open_board(args) as t:
        if args.flash:
            t.flash()
            print("programmed %s\n" % t.elf)
        # after the flash, not before: the board resets into whatever clock preset
        # its stored config holds, and that is the denominator of every number below.
        #
        # A desktop run records itself as its own board, which is load-bearing rather
        # than cosmetic: `results.latest` picks a drift baseline by board, so calling
        # it "pro" would let a run with no timings at all become the baseline for one
        # that has them.
        record = results.new("desktop" if args.desktop else args.board, t.elf, t.clock_hz())
        print("%s\n" % results.describe(record))
        for stage in stages:
            print("== %s" % stage)
            RUNNERS[stage](t, record, args)
    # both lists, so the report can say "partial run" without knowing how many
    # stages there are - a number written down anywhere else goes stale the day
    # one is added
    record["run"]["stages"] = stages
    record["run"]["allStages"] = list(STAGES)
    record["run"]["seconds"] = round(time.time() - started, 1)
    # What each scene was, snapshotted rather than looked up later. Deriving it at
    # report time meant an archived record rendered against whatever the library
    # had become, so the timings were pinned to a commit and the description of
    # what produced them was not.
    record["scenes"] = scenes.matrix(results.scenes_touched(record), results.BUDGET_US)

    print("\n%s in %.0f s" % (", ".join(stages), record["run"]["seconds"]))

    # Always against the newest saved record for this board, and before the verdict:
    # a systematic shift is the one regression class no golden and no assembly extract
    # can see, so it belongs in the verdict rather than in a report a reader may not
    # open. Resolved before saving, so it cannot resolve to this run.
    last_tag = results.latest(args.board)
    record["drift"] = results.drift(record, results.load(last_tag) if last_tag else None,
                                   last_tag)
    if record["drift"]:
        print("drift: %+.2f us mean render against %s (%d scenes, %d slower, %d faster)"
              % (record["drift"]["mean_us"], last_tag, record["drift"]["scenes"],
                 record["drift"]["worse"], record["drift"]["better"]))
    problems = verdict(record)

    # resolve --against before saving, so `last` cannot resolve to this run
    against, against_tag = None, None
    if args.against == "last":
        against_tag = results.latest(args.board)
        against = results.load(against_tag) if against_tag else None
        print("against: %s" % (against_tag or "nothing saved for this board yet"))
    elif args.against:
        against_tag, against = args.against, results.load(args.against)

    if args.save is not None:
        path = results.save(record, suffix=args.save or None)
        print("record: %s" % os.path.relpath(path))
    if args.report is not None:
        import report
        out = args.report or report.default_path(record, args.save or None)
        stem = os.path.basename(results.path_for(record, args.save or None))[:-5]
        report.write(out, record, against, labels=(against_tag, stem))
        print("report: %s" % os.path.relpath(out))

    if problems:
        print("\nFAILED")
        for p in problems:
            print("  " + p)
        return 1
    print("\nPASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
