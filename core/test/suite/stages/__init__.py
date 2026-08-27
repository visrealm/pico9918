"""The stages that assert what the renderer computed, and the verdict over them.

Every one of these takes a `VdpAccess` and fills its own key of a record, so the
same seven run against the shim here and against a board from the firmware
repository - which is the point: the acceptance criteria are the library's, and
the firmware adds three stages that measure a device rather than replacing these.

A stage takes `(t, record, args)` and returns nothing. `args` needs `filter`,
`canaries` and `quick`; `run.py` supplies them for a desktop run and the
firmware's runner supplies its own.
"""

import suite.stages.freeze as freeze
import suite.stages.gpu as gpu
import suite.scenes as scenes
import suite.stages.properties.test_d4 as test_d4
import suite.stages.properties.test_text80_8bpp as test_text80_8bpp
import suite.stages.properties.test_text_colour as test_text_colour
import suite.stages.properties.test_text_ecm as test_text_ecm
import suite.stages.properties.test_text_scroll as test_text_scroll

# Order matters: d4 first because it inherits whatever VDP state preceded it, and
# gpu last because a GPU program is the only stage that leaves the VDP set up by
# something other than this harness.
RENDERER = ("d4", "freeze", "scroll", "colour", "ecm", "t80-8bpp", "gpu")


def say(formatter):
    # *rest, because freeze's hook also hands over the frame it compared and the
    # timing stages have nothing to hand over
    return lambda name, entry, *rest: print("   " + formatter(name, entry))


def stage_d4(t, record, args):
    record["properties"]["d4"] = test_d4.run(t)


def stage_freeze(t, record, args):
    names = freeze.select(args.filter, args.canaries or args.quick)
    record["freeze"]["off"] = freeze.run(t, names, progress=say(freeze.line))


def stage_scroll(t, record, args):
    record["properties"]["text scroll"] = test_text_scroll.run(t)


def stage_colour(t, record, args):
    record["properties"]["text colour"] = test_text_colour.run(t)


def stage_ecm(t, record, args):
    record["properties"]["text ECM"] = test_text_ecm.run(t)


def stage_t80_8bpp(t, record, args):
    record["properties"]["8bpp 80-column text"] = test_text80_8bpp.run(
        t, test_text80_8bpp.select(args.filter))


def stage_gpu(t, record, args):
    # filtered like the timings rather than through gpu.select, so a run narrowed
    # to scene names runs no program instead of refusing to start
    record["gpu"] = gpu.run(t, [n for n in gpu.PROGRAMS
                                if not args.filter or any(f in n for f in args.filter)],
                            progress=say(gpu.line))


RUNNERS = {"d4": stage_d4, "freeze": stage_freeze, "scroll": stage_scroll,
           "colour": stage_colour, "ecm": stage_ecm, "t80-8bpp": stage_t80_8bpp,
           "gpu": stage_gpu}


def verdict(record):
    """What the run decided, and why.

    A regression fails wherever it appears. A dropped row fails only with the
    overlay **off**, which is the renderer's own cost and the budget that matters;
    with every panel on it is the overlay's cost, which is measured rather than
    asserted - `freeze.py --diag` exists to report that number, and a runner that
    failed on it would report FAILED on every RP2040 run. Provisional scenes that
    moved are printed and pass, because that is the feature arriving.

    Drift is read out of the record rather than measured here, so an archived run is
    judged against what it was compared with at the time. It is the only entry that
    can fail while every scene still renders the right pixels - and it is absent
    from a desktop record, which has no timings to drift."""
    problems, notes = [], []
    board = record["run"]["board"]
    for setting, out in record["freeze"].items():
        s = freeze.summarise(out)
        if s["regressions"]:
            problems.append("regressions (%s): %s" % (setting, ", ".join(s["regressions"])))
        if s["dropped"]:
            # a scene already known not to fit this board is a standing fact, not a
            # new failure - the drop is still measured and still printed
            known = [n for n in s["dropped"] if scenes.over_budget_on(n, board)]
            fresh = [n for n in s["dropped"] if n not in known]
            if fresh:
                where = problems if setting == "off" else notes
                where.append("dropped a line (%s): %s" % (setting, ", ".join(fresh)))
            if known:
                notes.append("still over budget on %s (%s): %s"
                             % (board, setting, ", ".join(known)))
        fits = [n for n in out
                if scenes.over_budget_on(n, board) and n not in s["dropped"]]
        if fits and setting == "off":
            notes.append("fits now, so take the overbudget marker off: %s" % ", ".join(fits))
        if s["changed"]:
            notes.append("provisional scenes moved (%s), review then re-freeze: %s"
                         % (setting, ", ".join(s["changed"])))
    shift = record.get("drift")
    if shift and shift["systematic"]:
        # slower fails, faster is reported: a cost can be honestly earned, and the
        # run that earns it should say so once rather than every time. Saving the
        # record is what settles it - the next run compares against this one.
        where = problems if shift["mean_us"] > 0 else notes
        where.append("%+.2f us mean render against %s, %d of %d scenes %s"
                     % (shift["mean_us"], shift["against"], max(shift["worse"], shift["better"]),
                        shift["scenes"], "slower" if shift["mean_us"] > 0 else "faster"))
    for name, entry in record.get("gpu", {}).items():
        # A program that drew the wrong picture fails; one that is not in this
        # checkout is a note, the same way a property the build cannot run is.
        # Its microseconds are reported and never compared: two processors running
        # the same program are not two readings of one instrument.
        if entry["state"] == "MISSING":
            notes.append("%s: %s" % (name, entry["why"]))
        elif entry["state"] not in ("ok", "FREEZE"):
            problems.append("%s: %s" % (name, entry["why"]))
    for name, result in record["properties"].items():
        if result.get("skipped"):
            notes.append("%s: not applicable to this build" % name)
        elif not result["ok"]:
            problems.append("%s: %d failures" % (name, len(result["failures"])))
    for n in notes:
        print("  " + n)
    return problems
