#!/usr/bin/env python3
"""Run a GPU program on the F18A's TMS9900 and check what it drew.

    python gpu.py                     every program, on the PRO
    python gpu.py --board 2040        ...on the RP2040
    python gpu.py --desktop           ...against the library, on the C core
    python gpu.py --update            freeze the picture a program draws
    python gpu.py --png shots         also write a PNG per program

The programs are other people's work, and `data/gpu-programs/README.md` credits each one.
`gpu-mandel` is **Tursi's** F18A GPU Mandelbrot, which is the point of it: a program
written by someone who was not thinking about our renderer.

**The program is the scene.** Every other stage here writes a register file and a
VRAM image and asks what the renderer makes of it. A GPU program is handed a blank
VDP and writes both itself, so what goes in is 548 bytes and a start address, and
what comes out is a picture that took twenty-three million TMS9900 instructions to
produce. That covers the GPU the way no register write can: the whole instruction
set, the VDP register file at >6000, and the F18A's hidden workspace at >FFFE.

**Two numbers, and only one of them is the test.** The picture is compared against
a frozen reference, byte for byte, the same way a scene is - that is the assertion.
The microseconds are reported and recorded: they are the firmware's own accumulator,
the one the diagnostics overlay draws its GPU percentage from, so they say how fast
this board runs a real program. They are not compared against anything, because the
same program on a desktop and on a board are two different processors.

**Which core runs it.** On the board, the hand-written Thumb core in
gpu/platform/thumb9900_*.S. With --desktop, the portable C core in gpu/tms9900.c.
The reference is shared, so running both is a differential test of the two cores
against each other - and the C core has never had one before.

`python view.py --gpu` watches one draw, in a window, while this is what checks it.
"""

import argparse
import collections
import os
import sys
import time

import suite.scenes as scenes
from suite.access.backend import backend_args, open_backend
from suite.oracle import golden
from suite.access.vdp import VRAM_REGISTERS

HERE = os.path.dirname(os.path.abspath(__file__))
PROGRAMS_DIR = os.path.join(os.path.dirname(HERE), "data", "gpu-programs")

# The GPU addresses a program may occupy: base VRAM, below the F18A's GRAM window.
# A program is loaded into it and the whole of it is blanked first, so nothing an
# earlier stage left behind can end up as part of the picture.
PROGRAM_SPACE = 0x4000

Program = collections.namedtuple("Program", "file entry credit note timeout")

PROGRAMS = {
    "gpu-mandel": Program(
        "mandel.bin", 0x1B02, "Tursi",
        "the Mandelbrot set in Graphics II, 14 iterations of Q13 fixed point - it "
        "sets its own registers, builds its own tables and halts on IDLE",
        timeout=120.0),
}


def load(program):
    """The program's bytes, or None if it is not here. Missing is reported rather
    than fatal: the machinery is worth having on a checkout that did not fetch a
    program, and the stage then says which one it wanted."""
    path = os.path.join(PROGRAMS_DIR, program.file)
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        return f.read()


def apply(t, program, data):
    """A blank VDP with the program in it, and nothing else.

    Unlocked, because a locked TMS9918A has no GPU to run this on. The register file
    is written blank rather than set up: a GPU program that needs a display sets its
    own registers, and one that does not is telling us something. VR1 is zero, so the
    display starts off and comes on when the program turns it on."""
    t.unlock()
    t.vram(VRAM_REGISTERS, bytes(64))
    vram = bytearray(PROGRAM_SPACE)
    vram[program.entry:program.entry + len(data)] = data
    t.vram(0, bytes(vram))
    t.palette_all(t.default_palette())
    scenes.quiet(t)


def spin(t, program, watching=None):
    """Start the program and wait for it. Returns (microseconds, frames, wall_ms).

    Both backends run it the same way and for the same reason: the program executes
    beside the renderer, not instead of it - on a board because core 0 runs it while
    core 1 renders, and on the desktop because the shim gives it a thread. So the
    picture builds up while this waits, and `watching()` is where a viewer draws a
    frame of it. Without one, this just sleeps.

    **Three numbers, because one of them cannot be checked against anything.** The
    microseconds are the firmware's accumulator, measured around the execution call
    with none of this harness inside the window. The frames are the display's own
    count over the same interval, which is the only figure here that compares with a
    capture card. And the wall clock is measured out here, so the gap between it and
    the accumulator IS the harness overhead rather than a thing to be argued about.

    The timeout belongs to the program rather than to the backend, which is why the
    loop is here. A program that overruns it is stopped rather than waited out."""
    t.gpu_start(program.entry)
    began = time.time()
    deadline = began + program.timeout
    while time.time() < deadline:
        us = t.gpu_poll()
        if us is not None:
            return us, t.gpu_frames(), (time.time() - began) * 1000.0
        if watching:
            watching()
        else:
            time.sleep(0.02)
    t.gpu_stop()
    raise TimeoutError("did not stop within its %g s" % program.timeout)


def run(t, names, update=False, png=None, progress=None, watching=None):
    """Run each program and compare what it drew. One entry per program, the shape
    `freeze.run` returns plus the microseconds."""
    out = {}
    for name in names:
        program = PROGRAMS[name]
        # the credit travels in the record, so a saved run and the page made from it
        # say whose program produced the number as well as what the number was
        entry = {"us": None, "frames": None, "wall_ms": None, "rows": 0,
                 "width": t.width, "entry": program.entry, "credit": program.credit,
                 "state": None, "differ": None, "first": None}
        data = load(program)
        if data is None:
            entry.update(state="MISSING",
                         why="no %s in %s" % (program.file,
                                              os.path.relpath(PROGRAMS_DIR, HERE)))
        elif program.entry + len(data) > PROGRAM_SPACE:
            entry.update(state="TOO BIG",
                         why="%d bytes at %#06x runs past the %#06x a program has"
                             % (len(data), program.entry, PROGRAM_SPACE))
        else:
            apply(t, program, data)
            try:
                us, frames, wall = spin(t, program, watching)
                entry.update(us=us, frames=frames, wall_ms=round(wall, 1))
            except TimeoutError as e:
                # recorded, not raised: a program that will not stop is one program's
                # failure, and a runner that lost the whole record over it would lose
                # every other stage's result too
                entry.update(state="DID NOT STOP", why=str(e))
        if entry["state"] is None:
            rows, pixels = t.capture()
            entry.update(rows=rows, width=t.width)
            if not entry["us"]:
                # the accumulator is written by the loop that ran the program, so a
                # zero means nothing ran it - a picture compared here would be
                # comparing whatever the blank VDP rendered
                entry.update(state="DID NOT RUN",
                             why="the GPU reported no time at all: was it started?")
            else:
                result = golden(name, rows, pixels, update, t.width)
                entry.update(differ=result["differ"], first=result["first"],
                             why=result["why"])
                entry["state"] = ("FREEZE" if result["wrote"]
                                  else "ok" if result["ok"] else "REGRESSION")
                if png:
                    t.png(os.path.join(png, name + ".png"), (rows, pixels))

        out[name] = entry
        if progress:
            progress(name, entry)
    return out


def line(name, entry):
    """The credit is in the line rather than in a summary underneath it, so it is
    printed wherever a program is run - by this file or by runner.py."""
    took = "%9.1f ms" % (entry["us"] / 1000.0) if entry["us"] else " " * 12
    frames = "%4d frames" % entry["frames"] if entry.get("frames") else " " * 10
    return "%-26s %-11s %s %s  rows=%3d  %s" % (
        "%s (%s)" % (name, entry.get("credit") or "?"),
        entry["state"], took, frames, entry["rows"], entry["why"])


def report(out):
    bad = [n for n, e in out.items() if e["state"] not in ("ok", "FREEZE")]
    print("\n%d program%s, %d ran" % (len(out), "" if len(out) == 1 else "s",
                                      sum(1 for e in out.values() if e["us"])))
    if bad:
        print("FAILED: " + ", ".join(bad))
    return 1 if bad else 0


def select(filters):
    names = [n for n in PROGRAMS if not filters or any(f in n for f in filters)]
    if not names:
        raise SystemExit("no GPU program matches %s" % " ".join(filters))
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("filter", nargs="*", help="substrings; empty means every program")
    ap.add_argument("--update", action="store_true", help="write references instead of comparing")
    ap.add_argument("--png", metavar="DIR", help="also write a PNG per program")
    backend_args(ap)
    args = ap.parse_args()

    names = select(args.filter)
    if args.png:
        os.makedirs(args.png, exist_ok=True)

    with open_backend(args) as t:
        out = run(t, names, args.update, args.png,
                  progress=lambda n, e: print(line(n, e)))
    return report(out)


if __name__ == "__main__":
    sys.exit(main())
