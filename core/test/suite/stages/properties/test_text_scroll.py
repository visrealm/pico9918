#!/usr/bin/env python3
"""Scroll in the text modes, checked against a property, not a golden.

A text row wraps to its own first cell and takes no page swap with it
either way, so the pixels it displays are one cycle of a ring:
240 of them at 40 columns, 480 at 80. Scrolling must therefore rotate the
unscrolled capture and nothing else - for every scroll value, on every row.

That is worth more than a golden here. A golden records whatever the firmware
did; this says what hardware requires, is derived from the hardware reference
rather than from our source, and needs no expected image to be computed by the
same reasoning the renderer uses. It found the two bugs in this feature that the goldens could only
have frozen: a border fill that ate the last pixels of the row, and - in an
earlier draft of this file - an expectation that ignored a layer 2 pixel whose
colour happened to equal the backdrop.

    python test_text_scroll.py            both modes
    python test_text_scroll.py --board 2040
"""

import argparse
import os
import sys

import suite.outcome as outcome
import suite.scenes as scenes
from suite.access.backend import backend_args, open_backend
from suite.access.vdp import VRAM_REGISTERS

PAD = 8                  # TEXT_PADDING_PX: the side borders are the layer's business
WIDE = 240               # bytes of text either mode writes between them
BACKDROP = scenes.BACKDROP

# every scroll value that stays inside the ring. At 240 and up the row reads on
# past its own end, so it is not a cycle of itself
HSCROLLS = [0, 1, 2, 3, 5, 6, 7, 11, 12, 17, 60, 100, 163, 234, 239]


def apply_raw(t, regs, vram, unlocked):
    """scenes.apply for a scene that is not in the library - the scroll values
    here are swept, so they are arguments rather than registered scenes."""
    t.unlock() if unlocked else t.lock()
    blanked = bytearray(regs)
    blanked[0x01] &= ~0x40
    # and the GPU stays off, as scenes.apply does it: a trigger left enabled resumes
    # a program that writes VRAM underneath, which matters more now that a shot only
    # rewrites the tables when they differ
    blanked[0x32] &= ~0x60
    t.vram(VRAM_REGISTERS, blanked)
    t.vram(0, bytes(vram))
    t.palette_all(list(t.default_palette()))
    scenes.quiet(t)
    t.reg(0x01, regs[0x01])


def row(pixels, y, width):
    return list(pixels[y * width:(y + 1) * width])


class Mode:
    """One text mode, at whatever width the firmware renders it.

    A scroll of h always moves the picture h pixels at 40 columns and 2h at 80,
    hardware doubling the register there. What that is in capture *bytes* depends
    on the depth: at four bits a pixel two 80-column pixels share a byte, so both
    modes come to h, and at eight bits the 80-column line is twice as long and it
    is 2h. `self.scale` is that factor, and the padding, the span and the backdrop
    byte all follow it."""

    def __init__(self, t, cols):
        self.t, self.cols = t, cols
        self.width = 256
        # what is already in the board's VRAM, so a shot that only moves a scroll
        # register does not send 16KB to say so
        self.loaded = None
        # a scanline that overruns is skipped rather than drawn late, so its row holds the
        # previous capture's pixels (vga.c). Comparing one reports a rotation failure
        # where what happened is that the scene does not fit, so they are collected and left
        # out of every comparison below - freeze.py refuses the same rows for the same reason
        self.dropped = set()
        self._geometry()

    def _geometry(self):
        self.scale = self.width // 256          # 1 for a 256-byte line, 2 for a 512-byte one
        self.pad = PAD * self.scale
        self.span = WIDE * self.scale
        # only a 4bpp 80-column line packs two pixels into a byte
        self.packed = self.cols == 80 and self.scale == 1
        self.bg = (BACKDROP | (BACKDROP << 4)) if self.packed else BACKDROP

    def shot(self, expect=None, **kw):
        """A sweep moves one register and nothing else, so when the VRAM is already
        the one on the board only the register file goes over - and the display is
        not blanked to do it, because there is nothing to hide. The 16KB write is
        62 ms and this stage takes 216 shots.

        `loaded` is only trusted inside one `run`, where nothing else writes VRAM.
        The moment the image differs by a byte the whole scene goes again, so a
        configuration change cannot inherit the last one's tables."""
        regs, vram, unlocked = scenes.text(self.cols, **kw)
        if vram == self.loaded and unlocked:
            self.t.vram(VRAM_REGISTERS, regs)
        else:
            apply_raw(self.t, regs, vram, unlocked)
            self.loaded = vram if unlocked else None
        frame = self.t.capture(expect=expect)
        self.dropped.update(self.t.dropped)
        if self.t.width != self.width:
            self.width = self.t.width
            self._geometry()
        return frame

    def ring(self, name, control, got, rows, h, fails):
        rotate = h * self.scale
        for y in range(rows):
            if y in self.dropped:
                continue
            c = row(control, y, self.width)
            want = [c[self.pad + ((i + rotate) % self.span)] for i in range(self.span)]
            have = row(got, y, self.width)[self.pad:self.pad + self.span]
            if have != want:
                bad = [i for i in range(self.span) if have[i] != want[i]]
                fails.append("%s h=%d row %d: %d bytes differ, first at byte %d "
                             "(got %02x want %02x)" % (name, h, y, len(bad), bad[0] + self.pad,
                                                       have[bad[0]], want[bad[0]]))
                return
        for y in range(rows):
            if y in self.dropped:
                continue
            r = row(got, y, self.width)
            if any(p != self.bg for p in r[:self.pad] + r[self.pad + self.span:]):
                fails.append("%s h=%d row %d: the border is not the backdrop" % (name, h, y))
                return

    def held(self):
        """Whether the last shot was answered by the hardware CRC alone, which means
        the frame is what was handed to it byte for byte - the property, asserted
        without the pixels ever crossing the wire."""
        return self.t.crc_hits and not self.t.crc_misses

    def rotated(self, control, rows, h):
        """The whole frame this scroll should produce: each row's span rotated left
        by the scroll and the rest of the line backdrop. This is what `ring` checks a
        row at a time, built once so the capture can put it to the CRC instead."""
        rotate = (h * self.scale) % self.span
        head = bytes([self.bg]) * self.pad
        tail = bytes([self.bg]) * (self.width - self.pad - self.span)
        out = bytearray()
        for y in range(rows):
            seg = control[y * self.width + self.pad:y * self.width + self.pad + self.span]
            out += head + seg[rotate:] + seg[:rotate] + tail
        return bytes(out)

    def sweep(self, name, reg, fails, **base):
        rows, control = self.shot(**base)
        for h in HSCROLLS:
            want = self.rotated(control, rows, h)
            rows, got = self.shot(expect=want, **dict(base, **{reg: h}))
            if self.held():
                continue
            self.ring(name, control, got, rows, h, fails)

    def page_inert(self, fails):
        """And the vertical scroll takes no page swap with it. A text row's name
        address is a row count times a column count
        and the page bit is never part of it, where a graphics row carries it at
        :579 - so VR29's vertical page size bit can do nothing in either text
        mode, at any scroll, on either layer. The name table's other page is laid
        out in the scene, so a swap that happened would be visible."""
        for reg, bit, extra in (("r1c", 0x01, {}), ("r1a", 0x10, dict(r31=0x80))):
            for v in (8, 60, 100, 191):
                base = dict(extra, r32=0x02, **{reg: v})
                _, plain = self.shot(r1d=0x88, **base)
                # the property is that these two frames are the same, so the second
                # is compared against the first by CRC before a pixel is read
                rows, swapped = self.shot(expect=plain, r1d=0x88 | bit, **base)
                if self.held():
                    continue
                bad = [i for i in range(rows * self.width)
                       if plain[i] != swapped[i] and (i // self.width) not in self.dropped]
                if bad:
                    fails.append("t%d %s=%d: the page size bit moved %d pixels, first at row %d "
                                 "column %d (%02x -> %02x)"
                                 % (self.cols, reg, v, len(bad), bad[0] // self.width,
                                    bad[0] % self.width, plain[bad[0]], swapped[bad[0]]))
                    break

    def split(self, fails, pairs):
        """The layers scrolled apart, so neither the whole picture nor either
        layer alone is the ring. Layer 2 owns a pixel wherever changing the
        backdrop leaves it alone - a layer 2 pixel that happens to *be* the
        backdrop colour still covers, which comparing against BACKDROP misses.
        A packed 80-column line covers per nibble, so this compares nibbles
        there and whole bytes everywhere else."""
        nibbles = (0, 4) if self.packed else (0,)
        for h1, h2 in pairs:
            _, only1 = self.shot(r1b=h1, r31=0x00, r32=0x02)          # layer 2 off
            _, only2 = self.shot(r19=h2, r31=0x80, r32=0x12)          # layer 1 off
            _, other = self.shot(r19=h2, r31=0x80, r32=0x12, r07=0x01)
            rows, got = self.shot(r1b=h1, r19=h2, r31=0x80, r32=0x02)
            for y in range(rows):
                if y in self.dropped:
                    continue
                a, b, g, o = (row(only1, y, self.width), row(only2, y, self.width),
                              row(got, y, self.width), row(other, y, self.width))
                want = []
                for x in range(self.width):
                    v = 0
                    for s in nibbles:
                        m = 0xf if self.packed else 0xff
                        n2, no, n1 = (b[x] >> s) & m, (o[x] >> s) & m, (a[x] >> s) & m
                        v |= (n2 if n2 == no else n1) << s
                    want.append(v)
                if want != g:
                    bad = [x for x in range(self.width) if want[x] != g[x]]
                    fails.append("t%d split h1=%d h2=%d row %d: %d differ, first byte %d "
                                 "(got %02x want %02x)" % (self.cols, h1, h2, y, len(bad),
                                                           bad[0], g[bad[0]], want[bad[0]]))
                    break


SPLITS = ((163, 5), (5, 163), (1, 2), (239, 0), (0, 239))


def run(t):
    fails, notes = [], []

    m = Mode(t, 40)
    # r1c scrolls vertically at the same time, so a horizontal change that
    # disturbed the row address would show up here rather than nowhere
    m.sweep("t40", "r1b", fails, r1c=100)
    m.sweep("t40-posattr", "r1b", fails, r32=0x02)
    m.sweep("t40-t2-only", "r19", fails, r31=0x80, r32=0x12)
    # both layers by the same amount: only then is the composite itself a ring.
    # One layer alone moving is the split case below, not a rotation of the picture
    _, control = m.shot(r31=0x80, r32=0x02)
    for h in HSCROLLS:
        rows, got = m.shot(r1b=h, r19=h, r31=0x80, r32=0x02)
        m.ring("t40-together", control, got, rows, h, fails)
    m.split(fails, SPLITS)
    m.page_inert(fails)
    notes.append("40 columns: %d scroll values, four configurations plus the split" % len(HSCROLLS))
    checks = 4 * len(HSCROLLS) + len(SPLITS)
    if m.dropped:
        notes.append("  %d rows were over budget and left out: %s"
                     % (len(m.dropped), sorted(m.dropped)[:8]))

    m = Mode(t, 80)
    m.sweep("t80", "r1b", fails)                            # two-tone, no colour table at all
    m.sweep("t80-posattr", "r1b", fails, r32=0x02)
    m.sweep("t80-t2-only", "r19", fails, r31=0x80, r32=0x12)
    for extra, tag in ((dict(), "t80-together"), (dict(sparse_t2=True), "t80-sparse")):
        base = dict(r31=0x80, r32=0x02, **extra)
        _, control = m.shot(**base)
        for h in HSCROLLS:
            rows, got = m.shot(r1b=h, r19=h, **base)
            m.ring(tag, control, got, rows, h, fails)
    m.split(fails, SPLITS)
    m.page_inert(fails)
    notes.append("80 columns: %d scroll values, five configurations plus the split" % len(HSCROLLS))
    checks += 5 * len(HSCROLLS) + len(SPLITS)
    if m.dropped:
        notes.append("  %d rows were over budget and left out: %s"
                     % (len(m.dropped), sorted(m.dropped)[:8]))

    return outcome.property_result(fails, notes, checks)


def main():
    ap = argparse.ArgumentParser()
    backend_args(ap)
    args = ap.parse_args()
    with open_backend(args) as t:
        return outcome.finish("text scroll", run(t))


if __name__ == "__main__":
    sys.exit(main())
