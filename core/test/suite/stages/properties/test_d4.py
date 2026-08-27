#!/usr/bin/env python3
"""D4: with tile layer 1 disabled, the selection mask must still reach screen
space and nothing may be written where the absent layer would have drawn.

The scene is bench.bas scenes 8-10, set up over SWD instead of from a cartridge.
Both layers tile each cell exactly - layer 1 owns the left four pixels and is
opaque, layer 2 owns the right four and is transparent elsewhere - so with equal
fine scrolls the expected image is exact:

    both layers   x mod 8 in 0..3 -> black (layer 2), 4..7 -> white (layer 1)
    layer 1 off   x mod 8 in 0..3 -> black (layer 2), 4..7 -> backdrop
    both off      backdrop everywhere

Layer 1's background is dark blue and layer 2 should cover every pixel of it, so
a single dark blue pixel means the mask disagrees with the pixels it selects
between. That is the assertion the photographs were trying to make.
"""

import argparse
import collections
import os
import sys

import suite.outcome as outcome
import suite.scenes as scenes
from suite.access.backend import backend_args, open_backend
from suite.access.vdp import VRAM_REGISTERS

VRAM_SIZE = 0x4000

WHITE, BLACK, BLUE, BACKDROP = 15, 1, 4, 9
SCROLL = 4

PATT, NAME1, COLOR1, NAME2, COLOR2, SATTR = 0x0000, 0x0800, 0x1200, 0x1C00, 0x2600, 0x3800


def setup(t, tile1=True, tile2=True):
    t.unlock()
    t.reg(0x01, 0x00)                       # display off while VRAM is rewritten

    # Every register and all of VRAM first, the way scenes.apply does it. This
    # test only writes what it names, so without this it inherits whatever ran
    # before - and a bitmap layer left enabled by an earlier scene covers the
    # screen and fails it on rows it never set up. That made it pass alone and
    # fail in a suite, which is the trap DEBUGGING.md records.
    t.vram(VRAM_REGISTERS, bytes(64))
    t.vram(0, bytes(VRAM_SIZE))

    t.vram(PATT, b"\xF0" * 512)             # names 0-63:  left half solid
    t.vram(PATT + 512, b"\x0F" * 512)       # names 64-127: right half solid
    t.vram(NAME1, bytes(range(32)) * 24)    # layer 1 uses the left-half shapes
    t.vram(NAME2, bytes(64 + (i % 32) for i in range(32)) * 24)
    t.vram(COLOR1, bytes([(WHITE << 4) | BLUE]) * 32)
    t.vram(COLOR2, bytes([(BLACK << 4) | 0]) * 32)
    t.vram(SATTR, b"\xD0")                  # no sprites

    t.reg(0x02, 0x02)                       # name table 1   0x0800
    t.reg(0x03, 0x48)                       # colour table 1 0x1200
    t.reg(0x04, 0x00)                       # patterns       0x0000
    t.reg(0x05, 0x70)                       # sprite attrs   0x3800
    t.reg(0x0a, 0x07)                       # name table 2   0x1C00
    t.reg(0x0b, 0x98)                       # colour table 2 0x2600
    scenes.quiet(t)
    t.reg(0x07, BACKDROP)
    t.reg(0x19, SCROLL)                     # layer 2 fine scroll
    t.reg(0x1b, SCROLL)                     # layer 1 fine scroll - equal, see the docstring
    t.reg(0x31, 0x80 if tile2 else 0x00)
    t.reg(0x32, 0x00 if tile1 else 0x10)
    t.reg(0x01, 0xE0)                       # display on


def expect(tile1, tile2):
    row = []
    for x in range(256):
        left = (x % 8) < 4
        if left and tile2:
            row.append(BLACK)
        elif not left and tile1:
            row.append(WHITE)
        else:
            row.append(BACKDROP)
    return bytes(row)


def check(name, t, tile1, tile2, fails, notes):
    setup(t, tile1, tile2)
    rows, pixels = t.capture()
    want = expect(tile1, tile2)
    bad = [y for y in range(rows) if pixels[y * 256:(y + 1) * 256] != want]
    hist = collections.Counter(pixels)
    notes.append("%-22s rows=%3d  indices=%-28s %s"
                 % (name, rows, dict(hist), "OK" if not bad else "MISMATCH on %d rows" % len(bad)))
    if bad:
        y = bad[0]
        fails.append("%s row %d: wanted %s, got %s"
                     % (name, y, list(want[:32]), list(pixels[y * 256:y * 256 + 32])))
    if BLUE in hist and tile2:
        fails.append("%s: dark blue on screen - the mask disagrees with the pixels it selects "
                     "between" % name)
    return rows


def run(t):
    fails, notes, checks = [], [], 0
    for name, tile1, tile2 in (("both layers", True, True),
                               ("layer 1 disabled", False, True),
                               ("both disabled", False, False)):
        checks += check(name, t, tile1, tile2, fails, notes) * 256
    return outcome.property_result(fails, notes, checks)


def main():
    ap = argparse.ArgumentParser()
    backend_args(ap)
    args = ap.parse_args()
    with open_backend(args) as t:
        return outcome.finish("D4", run(t))


if __name__ == "__main__":
    sys.exit(main())
