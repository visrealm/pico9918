#!/usr/bin/env python3
"""How a property shows what it is doing, for the ones that are not a picture.

Most stages here are their own display: they set a scene up and the assertion is
whether that scene came out right, so a person watching the board can see what is
happening. A property that asserts VRAM cannot - test_gpu_dma runs twenty transfers
against memory nothing displays, which leaves whatever the last stage drew on
screen and makes a board mid-run indistinguishable from a board that has hung.

So it draws a scoreboard instead, which is the vdptest cartridge's answer borrowed
whole: a column of names, RUNNING against the one in flight, PASS or FAILED behind
it, and a count at the bottom.

Graphics I colours eight characters at a time rather than per cell, so the font is
loaded three times over and each copy coloured - white, green, red. Printing in
colour is then printing the same string shifted by CH_GREEN or CH_RED.

Nothing here is ever compared. The tables sit clear of LOW_VRAM, which is what a
caller may use for buffers of its own, and `start` checks that claim rather than
trusting it - the failure of getting it wrong is a check quietly asserting against
its own display.
"""

import suite.scenes as scenes
from suite.access.vdp import VRAM_REGISTERS

COLS, ROWS = 32, 24

PATT, NAME, SPRITE_ATTR, COLOUR = 0x0000, 0x1C00, 0x1F00, 0x2000

# Everything between the font and the name table. Graphics I wants the name table
# on a 1KB boundary, which is what fixes 0x1C00 and so where this ends.
LOW_VRAM = (PATT + 3 * scenes.NUM_TILES * 8, NAME)

# add to a tile index to reach the green or red copy of the font
CH_GREEN, CH_RED = scenes.NUM_TILES, scenes.NUM_TILES * 2

TITLE_ROW, FIRST_ROW, SUMMARY_ROW = 0, 2, ROWS - 1
STATUS_COL = 21
STATUS_WIDTH = COLS - STATUS_COL

# Graphics I. R1 is written last, not in this order: it carries the display enable,
# so setting it early shows a frame of whatever the tables held before.
REGISTERS = ((0x00, 0x00), (0x02, NAME // 0x400), (0x03, COLOUR // 0x40),
             (0x04, PATT // 0x800), (0x05, SPRITE_ATTR // 0x80), (0x06, 0x07),
             (0x07, 0x01), (0x01, 0xE0))

# one byte per group of eight tiles, foreground in the high nibble. A transparent
# background shows the backdrop, which VR7 above makes black.
COLOURS = bytes([0xF0] * 8 + [0x30] * 8 + [0x90] * 8 + [0xF0] * 8)


def cells(text, colour=0):
    """Text as tile indices, in one of the three copies of the font."""
    return bytearray(c + colour for c in scenes.chars(text, len(text)))


class Scoreboard:
    """One row per check: `running` puts the name up, `verdict` settles it."""

    def __init__(self, t, title):
        self.t = t
        self.row = FIRST_ROW
        self.passed = self.failed = 0
        self._screen(title)

    def _screen(self, title):
        self.t.reg(0x01, 0x80)  # 16K, display off, while the tables go in
        self.t.vram(PATT, scenes.glyphs() * 3)
        self.t.vram(NAME, bytes(COLS * ROWS))
        self.t.vram(COLOUR, COLOURS)
        self.t.vram(SPRITE_ATTR, b"\xD0")
        self._put(TITLE_ROW, 0, cells(title))  # white: green and red mean a verdict
        for reg, value in REGISTERS:
            self.t.reg(reg, value)

    def _put(self, row, col, tiles):
        if 0 <= row < ROWS:
            self.t.vram(NAME + row * COLS + col, bytes(tiles[:COLS - col]))

    def running(self, name):
        self._put(self.row, 0, cells(name.ljust(STATUS_COL)))
        self._put(self.row, STATUS_COL, cells("RUNNING".ljust(STATUS_WIDTH)))

    def verdict(self, ok):
        word, colour = ("PASS", CH_GREEN) if ok else ("FAILED", CH_RED)
        self._put(self.row, STATUS_COL, cells(word.ljust(STATUS_WIDTH), colour))
        self.passed += bool(ok)
        self.failed += not ok
        self.row += 1

    def summary(self):
        self._put(SUMMARY_ROW, 0, cells("PASSED"))
        self._put(SUMMARY_ROW, 7, cells("%-3d" % self.passed, CH_GREEN))
        self._put(SUMMARY_ROW, 12, cells("FAILED"))
        self._put(SUMMARY_ROW, 19, cells("%-3d" % self.failed, CH_RED))


def start(t, title, low_vram=None):
    """A scoreboard on screen, ready for its first row. `low_vram` is the span the
    caller means to use underneath it, as (start, end)."""
    if low_vram is not None:
        lo, hi = low_vram
        if lo < LOW_VRAM[0] or hi > LOW_VRAM[1]:
            raise ValueError("%04x-%04x is not inside the %04x-%04x this leaves free"
                             % (lo, hi, LOW_VRAM[0], LOW_VRAM[1]))
    t.vram(VRAM_REGISTERS, bytes(64))
    scenes.quiet(t)
    return Scoreboard(t, title)
