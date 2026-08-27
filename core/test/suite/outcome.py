#!/usr/bin/env python3
"""How a property suite reports itself.

A property is not a golden: it asserts a rule over many generated cases rather
than reproducing one frozen frame, so what it has to say is a count of checks, a
list of failures and the notes that make a pass legible. Both shapes are here so
that every property answers in the same one.
"""


def property_result(failures, notes, checks):
    """The shape every property test returns. `checks` is how much was actually
    asserted - a property that passes over zero comparisons is the failure mode
    these tests exist to avoid, so the number is part of the result rather than a
    line of prose in the output."""
    return {"ok": not failures, "failures": list(failures),
            "notes": list(notes), "checks": checks}


def finish(name, result, limit=20):
    """Print a property result and give main() its exit code."""
    for note in result["notes"]:
        print(note)
    if result.get("skipped"):
        print("\n%s: not applicable to this build" % name)
        return 0
    if result["failures"]:
        print("\n%d FAILURES" % len(result["failures"]))
        for f in result["failures"][:limit]:
            print("  " + f)
        if len(result["failures"]) > limit:
            print("  ...and %d more" % (len(result["failures"]) - limit))
        return 1
    print("\n%s: PASS, %d checks" % (name, result["checks"]))
    return 0
