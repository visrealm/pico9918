#!/usr/bin/env python3
"""What a 40-column text cell draws above ECM0, checked against properties.

Both come from the same fact: ECM composes with mode through the plane 1 address
alone. Above ECM0 a text cell stops being a
colour pair and becomes an ordinary ECM tile - the attribute byte carries
priority, both flips, transparency and the sub-palette, the fg/bg pair goes
inert, and the only thing text keeps of its own is that a cell is six pixels
wide rather than eight.

So, given the same names, the same attribute table indexed by name and the same
planes:

  * **composition** - a text cell must draw what a Graphics I tile draws, in its
    first six pixels. One scene of each geometry, differing only in the name
    table's stride, and every cell compared against its tile. This is the
    stronger half: it asserts the whole ECM cell - plane addressing, the Y flip,
    transparency against the backdrop, the sub-palette and the priority bit -
    against a path that was frozen against hardware long before T40 had one.

  * **flip width** - an X flip mirrors six bits, not eight. Two captures
    differing only in the attribute's flip bit, and cell pixel `i` of one must be
    pixel `5 - i` of the other. Mirroring eight bits instead - which is what the
    graphics path's own table does - lands the glyph two pixels off, so this is
    the one place text may not reuse it, and a golden would only have frozen
    whichever answer the firmware gave.

    python test_text_ecm.py
    python test_text_ecm.py --board 2040
"""

import argparse
import os
import sys

import suite.outcome as outcome
import suite.scenes as scenes
from suite.access.backend import backend_args, open_backend
from suite.access.vdp import VRAM_REGISTERS

PAD = 8                  # TEXT_PADDING_PX - the border a text row leaves either side
CELL = 6                 # pixels a text cell shows
TILE = 8                 # pixels a graphics tile shows
COLS = 32                # cells compared: a graphics row is 32 wide, a text row 40
ROWS = 24
ECM_LEVELS = (1, 2, 3)


def attrs(flip_x):
    """One byte per name, indexed by name because position attributes are off. The sub-palette
    steps every eight names and the flag bits go to the low sixteen, so one table carries priority,
    the Y flip and transparency across the picture. `flip_x` forces the X flip bit either way: the
    composition property only holds where it is clear, and the flip property needs both."""
    a = bytearray(64)
    for i in range(64):
        a[i] = (((i & 0x0f) << 4) if i < 16 else 0) | ((i >> 3) & 0x0f)
        a[i] = (a[i] | 0x40) if flip_x else (a[i] & ~0x40)
    return a


def name_rows(cols):
    """The same 32 names on every row of either geometry, so text cell c and tile c hold the same
    name. Only the stride differs, which is the whole point: everything else must agree."""
    out = bytearray()
    for row in range(ROWS):
        shared = bytearray((row * COLS + c) & 0x3f for c in range(COLS))
        out += shared + bytearray(cols - COLS)
    return out


def vram(cols, flip_x):
    v = scenes.blank_vram()
    plane2, plane3 = scenes.ecm_planes()
    scenes.place(v, scenes.PATT, scenes.glyphs())
    scenes.place(v, scenes.PATT + scenes.ECM_K, plane2)
    scenes.place(v, scenes.PATT + 2 * scenes.ECM_K, plane3)
    scenes.place(v, scenes.NAME1, name_rows(cols))
    scenes.place(v, scenes.COLOR1, attrs(flip_x))
    return scenes.sprites_off(v)


def regs(cols, ecm):
    r = scenes.tile_regs()
    r[0x00] = 0x00
    r[0x01] = 0xF2 if cols == 40 else 0xE2      # text, or Graphics I
    r[0x31] = ecm << 4                          # one tile layer, no bitmap layer
    r[0x32] = 0x00                              # attributes by name, not by position
    return r


def shot(t, cols, ecm, flip_x):
    r = regs(cols, ecm)
    t.unlock()
    blanked = bytearray(r)
    blanked[0x01] &= ~0x40                      # display off while the tables are written
    t.vram(VRAM_REGISTERS, blanked)
    t.vram(0, bytes(vram(cols, flip_x)))
    t.palette_all(list(t.default_palette()))
    scenes.quiet(t)
    t.reg(0x01, r[0x01])
    return t.capture()


def composition(t, fails, ecm):
    """Text's six pixels against the tile's first six, cell for cell."""
    rows, text = shot(t, 40, ecm, flip_x=False)
    _, tiles = shot(t, 32, ecm, flip_x=False)
    drawn = 0
    for y in range(rows):
        for c in range(COLS):
            for i in range(CELL):
                got = text[y * 256 + PAD + c * CELL + i]
                want = tiles[y * 256 + c * TILE + i]
                drawn += got != scenes.BACKDROP
                if got != want:
                    fails.append("ecm%d composition row %d cell %d pixel %d: text drew %02x where "
                                 "the same name and attribute drew %02x as a tile"
                                 % (ecm, y, c, i, got, want))
                    return drawn
    return drawn


def flip_width(t, fails, ecm):
    """Cell pixel i flipped is pixel 5 - i unflipped. Eight-bit mirroring would put it two out."""
    rows, plain = shot(t, 40, ecm, flip_x=False)
    _, flipped = shot(t, 40, ecm, flip_x=True)
    moved = 0
    for y in range(rows):
        for c in range(COLS):
            base = y * 256 + PAD + c * CELL
            moved += any(plain[base + i] != plain[base + CELL - 1 - i] for i in range(CELL))
            for i in range(CELL):
                got = flipped[base + i]
                want = plain[base + CELL - 1 - i]
                if got != want:
                    fails.append("ecm%d flip row %d cell %d pixel %d: flipped drew %02x where "
                                 "unflipped pixel %d is %02x"
                                 % (ecm, y, c, i, got, CELL - 1 - i, want))
                    return moved
    return moved


def run(t):
    fails, notes, checks = [], [], 0
    for ecm in ECM_LEVELS:
        drawn = composition(t, fails, ecm)
        checks += drawn
        notes.append("ECM%d: %d of %d text pixels agree with the tile that shares their name and "
                     "attribute, and are not the backdrop" % (ecm, drawn, 192 * COLS * CELL))

    for ecm in ECM_LEVELS:
        moved = flip_width(t, fails, ecm)
        checks += moved
        notes.append("ECM%d: the X flip mirrors six pixels, over %d cells that are not already "
                     "symmetrical" % (ecm, moved))

    return outcome.property_result(fails, notes, checks)


def main():
    ap = argparse.ArgumentParser()
    backend_args(ap)
    args = ap.parse_args()
    with open_backend(args) as t:
        return outcome.finish("text ECM", run(t))


if __name__ == "__main__":
    sys.exit(main())
