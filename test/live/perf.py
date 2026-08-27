#!/usr/bin/env python3
"""Read the per-scanline render time over SWD, so the line budget can be checked
without a person and a photograph of the diagnostic overlay.

    python perf.py                  every scene
    python perf.py gm1 gm2          scenes whose name contains one of these
    python perf.py --canaries       the scenes that drop a line before an average moves
    python perf.py --save before    write the readings to test/live/perf-before.json
    python perf.py --against before compare against a saved set, or a runner.py record

Two instruments, one measurement each:

  render  `pico9918_scan_line`'s own microseconds, which the overlay accumulates
          and averages every four frames. This turns the diagnostic flags on over
          SWD, waits for an average to settle, and reads the formatted string back.
  line    the whole per-line total, a row at a time, out of `liveTestCapture`.
          `worst` is the largest of them - the line that decides whether a frame
          holds together, which no average can show.

**Both halves of a comparison must be live-test builds**, which cost one load and
a branch per scanline over a shipping build. Otherwise the comparison is not a
control: it has to be two builds differing only in the thing being priced.

**Neither number is the acceptance test.** A scene can sit inside budget on every
reading here and still drop scanlines; `freeze.py` is what reports a drop. Read a
delta against the floor below before believing it.
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import results
import suite.scenes as scenes
from live9918 import board_args, open_board

HERE = os.path.dirname(os.path.abspath(__file__))


def read_float(t, symbol):
    """The firmware formats these into a fixed 12-byte buffer with a start index,
    which is cheaper to read than to recompute from the accumulators - those reset
    every four frames.

    A build from before an instrument was added carries no symbol for it. That is
    absence, not a reading of zero, so it comes back as None and prints as n/a
    rather than landing in a comparison as a number nothing measured."""
    if symbol not in t.sym:
        return None
    addr = t.sym[symbol]
    raw = t.read(addr, 16)
    start = int.from_bytes(raw[12:16], "little")
    text = raw[start:12].split(b"\x00")[0].decode(errors="replace")
    return float(text) if text else 0.0


def line_times(t):
    """Every row's whole-line microseconds, this scene's only.

    Read from `liveTestCapture.lineTimes` rather than off the diagnostics panel:
    the same measurement feeds both, since pico9918_frame.c notes the row here and
    hands the identical value to the diagnostics accumulator on the next line.

    It is the better instrument anyway: a row apiece rather than a four-frame
    mean, so the worst line in the frame is visible, and that is the number a
    line budget is actually about. Whole microseconds, saturating at 255.

    `lineTimes` is written every row whether or not a capture is armed, and
    nothing clears it, so a taller mode's rows would linger under a shorter one.
    Zeroed before the settle wait, which makes a non-zero entry a row this scene
    drew - no row costs zero microseconds, and the saturation only clamps the top.
    """
    n = t.off["capture.lineTimes.size"]
    raw = t.read(t.capture_addr + t.off["capture.lineTimes"], n)
    return [b for b in raw if b]


def clear_line_times(t):
    n = t.off["capture.lineTimes.size"]
    t.write(t.capture_addr + t.off["capture.lineTimes"], b"\x00" * n)


# Frames to let pass before reading. The accumulators reset every four, and a
# reading is measurably the new scene's by the eighth, so twelve is margin.
SETTLE_FRAMES = 12


def measure(t, name, panels=False, frames=SETTLE_FRAMES):
    scenes.apply(t, name)
    for conf in (scenes.CONF_DIAG_PANELS if panels
                 else (scenes.PICO9918_CONF_DIAG, scenes.PICO9918_CONF_DIAG_PERFORMANCE)):
        t.write(t.inst + t.off["config"] + conf, b"\x01")
    clear_line_times(t)
    t.wait_frames(frames)
    rows = line_times(t)
    return {"render": read_float(t, "renderTimePerScanlineStr"),
            "line": sum(rows) / len(rows) if rows else None,
            "worst": float(max(rows)) if rows else None}


def run(t, names, panels=False, frames=SETTLE_FRAMES, progress=None):
    out = {}
    for name in names:
        out[name] = measure(t, name, panels, frames)
        if progress:
            progress(name, out[name])
    return out


def control(tag, panels=False):
    """A control is either a run record or one of the older `perf-<tag>.json`
    sets. Records are tried first so there is one durable format and the light
    loop below can measure against something that knows which commit it came
    from; the flat files still work, and are still only as good as your memory of
    what produced them."""
    try:
        results.resolve(tag)
    except SystemExit:
        with open(os.path.join(HERE, "perf-%s.json" % tag)) as f:
            flat = json.load(f)
        # A record carries a schema and is refused when it is not the current one;
        # a flat file carries nothing, so the shape has to be sniffed. No `worst` key
        # means a schema-2 shape: whole-microsecond `render`, and a `line` from a
        # different instrument.
        if flat and not any("worst" in e for e in flat.values()):
            print("warning: perf-%s.json is a schema-2 shape - its render column is\n"
                  "         whole microseconds and its line column is a different\n"
                  "         instrument. Deltas are indicative.\n"
                  % tag)
        return flat, "perf-%s.json" % tag
    # the record exists, so a schema complaint from here is the real answer and
    # must not be swallowed into a search for a flat file that was never meant
    record = results.load(tag)
    return record["perf"].get("all" if panels else "one", {}), results.describe(record)


def us(value):
    return " n/a  " if value is None else "%6.2f" % value


def line(name, entry, before=None):
    text = "%-18s render %s us  line %s us  worst %s us" % (
        name, us(entry["render"]), us(entry["line"]), us(entry.get("worst")))
    was = before["render"] if before else None
    if was is not None and entry["render"] is not None:
        delta = entry["render"] - was
        text += "   was %6.2f  %+6.2f us  %+6.2f%%" % (was, delta, delta / was * 100.0 if was else 0)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("filter", nargs="*")
    ap.add_argument("--save", metavar="TAG")
    ap.add_argument("--against", metavar="TAG")
    ap.add_argument("--canaries", action="store_true",
                    help="only the scenes that drop a line before any average moves")
    ap.add_argument("--panels", action="store_true",
                    help="every overlay panel, not just the performance one - what a user's "
                         "diagnostic mode actually costs. Not comparable with a set saved without it")
    board_args(ap)
    args = ap.parse_args()

    names = (list(scenes.CANARIES) if args.canaries else
             [n for n in scenes.SCENES if not args.filter or any(f in n for f in args.filter)])
    before, source = {}, None
    if args.against:
        before, source = control(args.against, args.panels)
        print("against %s\n" % source)

    with open_board(args) as t:
        floor = results.NOISE_FLOOR[args.board]
        now = run(t, names, args.panels,
                  progress=lambda n, e: print(line(n, e, before.get(n))))

    if args.save:
        path = os.path.join(HERE, "perf-%s.json" % args.save)
        with open(path, "w") as f:
            json.dump(now, f, indent=1, sort_keys=True)
        print("\nsaved perf-%s.json" % args.save)
    if before:
        moved = [(abs(now[n]["render"] - before[n]["render"]), n)
                 for n in now if n in before]
        worst = max(moved) if moved else (0.0, "-")
        print("\nworst render change %.2f us on %s - the floor on this board is %.2f, so anything "
              "under that is placement" % (worst[0], worst[1], floor))
    return 0


if __name__ == "__main__":
    sys.exit(main())
