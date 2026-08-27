#!/usr/bin/env python3
"""What colour a 40-column text cell draws, checked against properties.

Both halves come from the same fact: an ECM0 pixel reaches the line buffer as PIX
and a six-bit palette address, so a cell either claims a pixel in some
sub-palette or claims nothing at all.

An ECM0 pixel reaches the line buffer as PIX, PRI and a six-bit palette address
whose top two bits are the owning layer's palette select and whose bottom four
are the colour it computed. Nothing there is mode-specific -
`tileps_s` is the layer mux's output (:475, :492) and the text branch changes
only where the colour came from (:1069-1078). The backdrop is assembled the same
way, from tile layer 1's selector.

So for a scene with no sprites and no bitmap layer, choosing sub-palettes must
shift every pixel of the picture into them and change nothing else: a pixel that
read `c` with both selectors zero must read `(ps << 4) | c`, where `ps` is layer
1's selector if layer 1 or the backdrop owns the pixel and layer 2's if layer 2
does. That is what this asserts, per pixel, over both layers' selectors.

Ownership is measured rather than assumed, the way test_text_scroll.py does it:
layer 2 owns a pixel wherever changing the backdrop leaves it alone.

The other half is the cell that claims nothing: a zero colour byte leaves
the pixel not a pixel at all for all six, and the colour stage then shows
whatever is beneath - which under a non-priority bitmap layer is the layer
rather than the backdrop.

    python test_text_colour.py
    python test_text_colour.py --board 2040
"""

import argparse
import os
import sys

import suite.outcome as outcome
import suite.scenes as scenes
from suite.access.backend import backend_args, open_backend
from suite.access.vdp import VRAM_REGISTERS

BACKDROP = scenes.BACKDROP
ALT_BACKDROP = 1         # any other index: it only has to move the pixels it owns
R7 = 0xF0 | BACKDROP


def apply_raw(t, regs, vram, unlocked):
    """scenes.apply for a scene that is not in the library - the selectors here
    are swept, so they are arguments rather than registered scenes."""
    t.unlock() if unlocked else t.lock()
    blanked = bytearray(regs)
    blanked[0x01] &= ~0x40
    t.vram(VRAM_REGISTERS, blanked)
    t.vram(0, bytes(vram))
    t.palette_all(list(t.default_palette()))
    scenes.quiet(t)
    t.reg(0x01, regs[0x01])


def shot(t, **kw):
    regs, vram, unlocked = scenes.text(40, **kw)
    apply_raw(t, regs, vram, unlocked)
    return t.capture()


def check(t, fails, tag, ps1, ps2, **base):
    """`base` is a two-layer configuration. Three extra captures say what each
    pixel is worth at sub-palette zero and which layer owns it."""
    rows, control = shot(t, r18=0, **base)
    if ps2 is None:                       # one layer: layer 1 and the backdrop own everything
        owner2 = [False] * (rows * 256)
    else:
        alone = dict(base, r32=base.get("r32", 0) | 0x10)
        _, only2 = shot(t, r18=0, **alone)
        _, moved = shot(t, r18=0, **dict(alone, r07=0xF0 | ALT_BACKDROP))
        owner2 = [a == b for a, b in zip(only2, moved)]

    _, got = shot(t, r18=(ps1 & 0x03) | ((ps2 or 0) & 0x03) << 2, **base)
    for i in range(rows * 256):
        ps = ps2 if owner2[i] else ps1
        want = (ps << 4) | control[i]
        if got[i] != want:
            row, col = divmod(i, 256)
            fails.append("%s ps1=%d ps2=%s row %d column %d: got %02x, want %02x - "
                         "%s at sub-palette 0 was %02x"
                         % (tag, ps1, ps2, row, col, got[i], want,
                            "layer 2" if owner2[i] else "layer 1 or the backdrop", control[i]))
            return


PAIRS = ((1, 1), (3, 3), (3, 2), (0, 3), (2, 0))

BLANK_CELLS = 3          # what t40-bml-under zeroes at the start of every row
PAD = 8                  # TEXT_PADDING_PX


def nothing_drawn(t, fails):
    """The scene's first three cells have a zero colour byte, so they draw no
    pixel at all - which has to look exactly like having no tile layer there. It
    is the leading run that is the test: the colour memo is primed for it rather
    than reaching a comparison (D8), and the bitmap layer beneath is what makes
    the difference visible, the backdrop being what a wrong answer paints."""
    regs, vram, unlocked = scenes.build("t40-bml-under")
    apply_raw(t, regs, vram, unlocked)
    rows, drawn = t.capture()

    off = bytearray(regs)
    off[0x32] |= 0x10                    # no tile layer 1, so nothing can be written
    apply_raw(t, off, vram, unlocked)
    _, bare = t.capture()

    columns = range(PAD, PAD + BLANK_CELLS * 6)

    # the layer has to reach under those cells, or there is nothing to hide
    covered = sum(bare[y * 256 + x] != BACKDROP for y in range(rows) for x in columns)
    if not covered:
        fails.append("t40-bml-under: the bitmap layer no longer reaches the blank cells, so this "
                     "proves nothing")

    for y in range(rows):
        for x in columns:
            i = y * 256 + x
            if drawn[i] != bare[i]:
                fails.append("t40-bml-under row %d column %d: a cell that draws nothing wrote "
                             "%02x where the layer beneath had %02x" % (y, x, drawn[i], bare[i]))
                return covered
    return covered


def run(t):
    fails, notes = [], []

    # one layer, the R7 colour pair: both nibbles are non-zero, so every pixel
    # of the row is a tile pixel and only the side borders are the backdrop
    for ps in (1, 2, 3):
        check(t, fails, "t40", ps, None, r07=R7)
    notes.append("one layer, the R7 pair: 3 selectors")

    for ps1, ps2 in PAIRS:
        check(t, fails, "t40-t2", ps1, ps2, r07=R7, r31=0x80, r32=0x02)
    notes.append("both layers, a colour byte per cell: %d selector pairs" % len(PAIRS))

    # scrolled, where the emitter takes its cells from a different place and
    # the composite shifts the layer buffers under the mask
    for ps1, ps2 in PAIRS[:2]:
        check(t, fails, "t40-hscroll-split", ps1, ps2,
              r07=R7, r31=0x80, r32=0x02, r1b=163, r19=5)
    notes.append("both layers scrolled apart: 2 selector pairs")

    covered = nothing_drawn(t, fails)
    notes.append("a leading run of zero-colour cells: the layer beneath shows through %d of them"
                 % covered)

    return outcome.property_result(fails, notes, 3 + len(PAIRS) + 2 + covered)


def main():
    ap = argparse.ArgumentParser()
    backend_args(ap)
    args = ap.parse_args()
    with open_backend(args) as t:
        return outcome.finish("text colour", run(t))


if __name__ == "__main__":
    sys.exit(main())
