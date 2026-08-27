#!/usr/bin/env python3
"""Capture every scene and compare it against the frozen reference.

    python freeze.py                 compare, and fail if a frozen scene moved
    python freeze.py --update        write the references (the initial freeze)
    python freeze.py gm2 mcm         only scenes whose name contains one of these
    python freeze.py --canaries      the scenes that drop a line before any average moves
    python freeze.py --png shots     also write a PNG per scene
    python freeze.py --board 2040    use the RP2040 build, not the PRO
    python freeze.py --audit         which features are still missing (no board)
    python freeze.py --diag          with every overlay panel on: which scenes it pushes over budget

The overlay never reaches a capture - `liveTestCaptureRow` runs before it draws -
so `--diag` compares the same goldens and only the dropped-row report moves. That
makes it the instrument for the overlay's own cost, and a per-line one, which an
average over a frame cannot be: the panels cover part of the screen, so what they
cost lands on some lines and not others. Use a **simple, unlocked** scene to price
the overlay itself - the register panel is 228 rows unlocked and 48 locked, and a
scene with headroom to spare leaves the overlay as the only thing near the edge.

A frozen scene that changes is a regression. A provisional one records something
this firmware does not do yet (see scenes.py), so when it changes that is the
feature arriving - re-freeze it with --update and say so in the commit.

`run()` returns the per-scene record `runner.py` collects; `main()` prints it.
"""

import argparse
import os
import sys
import zlib

import suite.scenes as scenes
from suite.access.backend import backend_args, open_backend
from suite.oracle import golden, reference
from suite.access.vdp import PIXELS_X


def audit(width):
    """Every provisional scene names the scene it is identical to while the
    feature it exercises is missing. Comparing the stored references says which
    gaps in the feature matrix are still open, without touching hardware.

    `width` picks which tier to report on: the 8bpp 80-column tier fills two of
    these gaps and has references of its own, so auditing it means resolving them
    at 512."""
    missing = present = 0
    for name in scenes.SCENES:
        base = scenes.SCENES[name].base
        if not base:
            continue
        try:
            a, b = (zlib.decompress(open(reference(n, width), "rb").read())
                    for n in (name, base))
        except OSError as e:
            print("%-18s no reference: %s" % (name, e))
            continue
        if len(a) != len(b):
            # one of the pair resolved to another width's reference, so there is no comparison to
            # make: a scene with no reference of its own falls back to the shared one, and a scene
            # that is over budget never got one written. Counting this as "present" would report a
            # feature working on the strength of the two captures being different lengths
            print("%-18s %-8s vs %-14s %d bytes against %d - no comparison at this width"
                  % (name, "UNKNOWN", base, len(a), len(b)))
            continue
        same = a == b
        missing += same
        present += not same
        print("%-18s %-8s vs %-14s %s"
              % (name, "absent" if same else "present", base, scenes.changes(name)))
    print("\n%d features still absent, %d already doing something" % (missing, present))
    return 0


def stored(name, width=PIXELS_X):
    """The reference bytes, or None if there is not one yet. Read once per scene so
    the capture can check its CRC against them instead of transferring pixels."""
    path = reference(name, width)
    if not os.path.exists(path):
        return None
    try:
        with open(path, "rb") as f:
            return zlib.decompress(f.read())
    except (OSError, zlib.error):
        return None


def run(t, names, update=False, diag=False, png=None, progress=None, between=None):
    """Capture and compare each scene, returning one entry per scene.

    A scene that dropped a row is recorded and not compared: an over-budget line
    is skipped rather than drawn late (vga.c), so those rows hold the previous
    capture's pixels and comparing them reports a rendering difference where what
    happened is that the scene does not fit.

    `progress(name, entry, frame)` is called per scene. It gets the frame as well as
    the verdict so a watcher can show the very bytes that were compared rather than
    taking a second capture of a board that has already moved on - which is what the
    console's sweep view draws. A progress hook may also RAISE to stop the run: the
    console cancels a sweep that way rather than by a flag this loop would have to
    test.

    `between(name)` runs after the scene is applied and before it is captured, which
    is where the viewer plays the scene at 60 frames a second. It exists so watching
    the suite run means running THIS function rather than a copy of it that has to be
    kept in step - the frames it renders are ahead of the capture, so they cannot
    change what gets compared."""
    out = {}
    for name in names:
        scenes.apply(t, name)
        if diag:
            for conf in scenes.CONF_DIAG_PANELS:
                t.write(t.inst + t.off["config"] + conf, b"\x01")
        if between:
            between(name)
        # hand the capture what it is about to be compared against: a window whose
        # hardware CRC matches the reference need not come over the wire at all, and
        # on a passing sweep that is nearly every window
        rows, pixels = t.capture(expect=stored(name, t.width))
        provisional = scenes.changes(name)
        entry = {"rows": rows, "width": t.width, "dropped": list(t.dropped),
                 "provisional": provisional, "differ": None, "first": None}

        if t.dropped:
            entry["state"] = "OVER BUDGET"
            entry["why"] = ("dropped %d row%s: %s - the line did not fit, so this capture "
                            "cannot be compared"
                            % (len(t.dropped), "" if len(t.dropped) == 1 else "s",
                               t.dropped[:8]))
        else:
            result = golden(name, rows, pixels, update, t.width)
            entry.update(differ=result["differ"], first=result["first"], why=result["why"])
            if result["wrote"]:
                entry["state"] = "FREEZE"
            elif result["ok"]:
                entry["state"] = "ok"
            elif provisional:
                entry["state"] = "CHANGED"
            else:
                entry["state"] = "REGRESSION"
            if png:
                t.png(os.path.join(png, name + ".png"), (rows, pixels))

        out[name] = entry
        if progress:
            progress(name, entry, (rows, pixels))
    return out


def summarise(out):
    """The four groups worth acting on, in the order they matter."""
    by = lambda *states: [n for n, e in out.items() if e["state"] in states]
    return {"regressions": by("REGRESSION"), "changed": by("CHANGED"),
            "dropped": by("OVER BUDGET"), "wrote": by("FREEZE")}


def line(name, entry):
    return "%-18s %-11s rows=%3d  %s" % (name, entry["state"], entry["rows"], entry["why"])


def report(names, out, diag=False):
    """Print the summary and give main() its exit code."""
    s = summarise(out)
    print("\n%d scenes, %d frozen, %d provisional"
          % (len(names), sum(1 for n in names if not scenes.changes(n)),
             sum(1 for n in names if scenes.changes(n))))
    if s["wrote"]:
        print("wrote %d reference%s" % (len(s["wrote"]), "" if len(s["wrote"]) == 1 else "s"))
    if s["changed"]:
        print("changed (provisional, review then re-freeze): " + ", ".join(s["changed"]))
        for name in s["changed"]:
            print("    %-18s expected eventually: %s" % (name, out[name]["provisional"]))
    if s["dropped"]:
        print("OVER BUDGET, not compared: " + ", ".join(s["dropped"])
              + "\n  a scanline that overruns is skipped rather than drawn late (vga.c:600)."
              + ("\n  --diag is on, so these are the lines the overlay costs, not the renderer."
                 if diag else
                 "\n  The overlay is off here, so this is the renderer's own cost. --diag reports"
                 "\n  what the overlay adds, per line rather than as an average over the frame."))
    if s["regressions"]:
        print("REGRESSIONS: " + ", ".join(s["regressions"]))
    return 1 if s["regressions"] or s["dropped"] else 0


def select(filters, canaries=False):
    """Which scenes a filter names. `--canaries` is the set that have each dropped
    a line while their own average sat well inside budget."""
    if canaries:
        return list(scenes.CANARIES)
    names = [n for n in scenes.SCENES if not filters or any(f in n for f in filters)]
    if not names:
        raise SystemExit("no scene matches %s" % " ".join(filters))
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("filter", nargs="*", help="substrings; empty means every scene")
    ap.add_argument("--update", action="store_true", help="write references instead of comparing")
    ap.add_argument("--png", metavar="DIR", help="also write a PNG per scene")
    ap.add_argument("--canaries", action="store_true",
                    help="only the scenes that drop a line before any average moves")
    ap.add_argument("--audit", action="store_true", help="report which features are missing")
    ap.add_argument("--width", type=int, default=PIXELS_X,
                    help="which tier --audit reports on: 512 is the 8bpp 80-column one")
    ap.add_argument("--diag", action="store_true",
                    help="leave every overlay panel on: the goldens still hold, so what this "
                         "reports is which scenes the overlay pushes over budget")
    backend_args(ap)
    args = ap.parse_args()

    if args.audit:
        return audit(args.width)

    names = select(args.filter, args.canaries)
    if args.png:
        os.makedirs(args.png, exist_ok=True)

    with open_backend(args) as t:
        out = run(t, names, args.update, args.diag, args.png,
                  progress=lambda n, e, f: print(line(n, e)))
    return report(names, out, args.diag)


if __name__ == "__main__":
    sys.exit(main())
