#!/usr/bin/env python3
"""The 8bpp 80-column tier must draw the same picture the 4bpp path drew.

That is the property, and it is the one worth having: the tier adds ECM, the tile
palette select, the bitmap layer and the shared composite to 80-column text, and
adds *nothing else*. So for every T80 scene using none of the four, an 8bpp
capture must be the frozen 4bpp reference with each byte's two nibbles spread
into two bytes - left pixel first, which is the high nibble.

It compares two paths rather than restating either, so it covers the name and
attribute addressing, the pattern fetch, both scrolls, the wrap, the border and
the sprite pass in one assertion, against a reference frozen against hardware
long before the tier existed.

The low nibble is the picture. The high one carries the sub-palette the tier also
brings, so it is checked separately: with no selector set it must be zero
everywhere, which is what says the tier did not silently colour something.

    python test_text80_8bpp.py                  every eligible T80 scene
    python test_text80_8bpp.py t80-hscroll      just these
"""

import argparse
import os
import sys
import zlib

import suite.outcome as outcome
import suite.scenes as scenes
from suite.access.backend import backend_args, open_backend
from suite.oracle import REFERENCE_DIR
from suite.access.vdp import PIXELS_X

# what the tier adds, and therefore what a scene may not use to be comparable
EXCLUDED = ("ecm", "bml", "palette")


def eligible():
    for name in scenes.SCENES:
        if not name.startswith("t80"):
            continue
        if any(word in name for word in EXCLUDED):
            continue
        yield name


def run(t, names):
    """A build without the tier has nothing to assert, so this reports itself as
    not applicable rather than failing - `PICO9918_TEXT80_8BPP` is off on every
    RP2040 build, and a runner that treats that as a failure would be crying wolf
    on the binding board."""
    fails, notes, checks = [], [], 0
    for name in names:
        scenes.apply(t, name)
        rows, pixels = t.capture()
        if t.width != PIXELS_X * 2:
            result = outcome.property_result([], ["not applicable: this build renders %d bytes a "
                                                  "line, so it has no 8bpp 80-column tier"
                                                  % t.width], 0)
            result["skipped"] = True
            return result
        if t.dropped:
            fails.append("%s: OVER BUDGET, %d rows dropped: %s"
                         % (name, len(t.dropped), t.dropped[:6]))
            continue

        path = os.path.join(REFERENCE_DIR, name + ".bin.z")
        with open(path, "rb") as f:
            want4 = zlib.decompress(f.read())
        if len(want4) != rows * PIXELS_X:
            fails.append("%s: 4bpp reference is %d bytes, expected %d"
                         % (name, len(want4), rows * PIXELS_X))
            continue

        bad = sel = 0
        first = None
        for i, packed in enumerate(want4):
            left, right = packed >> 4, packed & 0x0f
            for j, want in ((i * 2, left), (i * 2 + 1, right)):
                got = pixels[j]
                if (got & 0x0f) != want:
                    bad += 1
                    if first is None:
                        first = (divmod(j, t.width), want, got)
                sel |= got & 0xf0

        checks += rows * t.width
        if bad:
            (y, x), want, got = first
            fails.append("%s: %d of %d pixels differ, first at row %d column %d: %x -> %02x"
                         % (name, bad, rows * t.width, y, x, want, got))
        else:
            notes.append("%-20s %d pixels are the 4bpp picture, sub-palette bits %02x"
                         % (name, rows * t.width, sel))
            if sel:
                fails.append("%s: a selector is set where the 4bpp path had none (%02x) - check "
                             "it is VR24's" % (name, sel))
    return outcome.property_result(fails, notes, checks)


def select(filters):
    names = [n for n in eligible() if not filters or any(f in n for f in filters)]
    if not names:
        raise SystemExit("no eligible T80 scene matches %s" % " ".join(filters))
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("filter", nargs="*")
    backend_args(ap)
    args = ap.parse_args()
    with open_backend(args) as t:
        return outcome.finish("8bpp 80-column text", run(t, select(args.filter)))


if __name__ == "__main__":
    sys.exit(main())
