#!/usr/bin/env python3
"""The GPU's DMA engine, triggered the way a program triggers one.

This is the only thing in the suite that makes the board's DMA trigger fire. The
engine itself is one C function shared by every build, but reaching it is two
unrelated mechanisms, and only one of them is a device:

    board    MPU region 0 guards 32 bytes at >8000 (pico9918_gpu_init). The Thumb
             core has no idea DMA exists - it stores, hard-faults, and the handler
             turns the MPU off and flags it. pico9918_gpu_loop then runs the
             transfer and resumes the program where it stopped.
    desktop  TMS9900_WATCH_WRITES, compiled only off a Pico: the interpreter
             reports each write and the address is compared in software.

So running this on both backends is a differential test of those two, the same way
the gpu stage is one of the two instruction cores. Nothing else here reaches the
fault path at all: the gpu stage's programs never touch >8000, and the DMA cases in
core/test/gpu are a host binary with no board build.

**The expectation is computed, not frozen.** `transfer()` below is the VHDL, and
what a job should leave is derived from it per case rather than copied out of a
table - so this shares no literal with the C tests and disagrees with them
independently. The rule that matters is the row pitch, which is NOT the stride
register (f18a_gpu.vhd:661-686):

    dma_step_s      +1 incrementing, -1 decrementing
    dma_diff_s      stride - (w-1) incrementing, (w-1) - stride decrementing,
                    eight bits, then sign extended
    pitch           the diff replaces the last step of a row, so (w-1)*step + diff

Which gives four corners rather than two, because the difference changes sides with
the direction: incrementing, an ordinary stride is a positive difference and only
one that overflows walks the rows backwards; decrementing, an ordinary stride is a
NEGATIVE difference and only one that underflows walks them forwards. The two turn
over one apart - 135 and 136 for a width of 8 - because two's complement holds one
more negative than positive.
"""

import argparse
import sys

import suite.outcome as outcome
import suite.scoreboard as scoreboard
import suite.stages.gpu as gpu
from suite.access.backend import backend_args, open_backend

# Base VRAM, where the F18A's windowed map and the PICO9918's flat one agree, so a
# case means the same thing whichever personality is running it. WORK is written
# whole before every job and read back whole after, so a transfer that strayed
# outside it would be caught by `reach` below rather than silently ignored.
#
# All of it inside scoreboard.LOW_VRAM, so the board can show what it is doing
# while it does it - `scoreboard.start` is given the span and checks.
PROG, PARAMS = 0x0E00, 0x0E20
SRC, SRC_MID, DST = 0x1000, 0x1100, 0x1800
WORK, WORK_LEN = 0x1000, 0x0A00
SRC_LEN = 0x0400

# LI R0,PARAMS / LI R1,>8000 / LI R2,8 / MOVB *R0+,*R1+ / DEC R2 / JNE -3 /
# LI R1,>8008 / LI R2,>0100 / MOVB R2,*R1 / IDLE. The last MOVB is the trigger: it
# is the store the MPU guards, and everything above it only stages the arguments.
PROGRAM = bytes((0x02, 0x00, PARAMS >> 8, PARAMS & 0xff,
                 0x02, 0x01, 0x80, 0x00,
                 0x02, 0x02, 0x00, 0x08,
                 0xdc, 0x70,
                 0x06, 0x02,
                 0x16, 0xfd,
                 0x02, 0x01, 0x80, 0x08,
                 0x02, 0x02, 0x01, 0x00,
                 0xd4, 0x42,
                 0x03, 0x40,
                 0x03, 0x40))

JOB = gpu.Program(file=None, entry=PROG, credit=None, note=None, timeout=5.0)

# src, dst, width, height, stride, params - params bit 0 fills, bit 1 decrements
CASES = (
    ("copy",                  SRC,       DST,        4, 3,   4, 0x00),
    ("stride, gaps",          SRC,       DST,        4, 3,  16, 0x00),
    ("stride zero",           SRC,       DST,        4, 3,   0, 0x00),
    ("stride under width",    SRC,       DST,        8, 2,   4, 0x00),
    ("inc, last forwards",    SRC_MID,   DST,        8, 2, 134, 0x00),
    ("inc, first backwards",  SRC_MID,   DST,        8, 2, 135, 0x00),
    ("inc, far backwards",    SRC_MID,   DST,        8, 2, 200, 0x00),
    ("width 256",             SRC,       DST,        0, 1,   0, 0x00),
    ("height 256",            SRC,       DST,        1, 0,   1, 0x00),
    ("fill",                  SRC,       DST,        4, 3,  16, 0x01),
    ("dec",                   SRC_MID,   DST,        4, 2,   4, 0x02),
    ("dec, stride zero",      SRC_MID,   DST,        4, 3,   0, 0x02),
    ("dec, gaps",             SRC_MID,   DST,        4, 3,  16, 0x02),
    ("dec, last backwards",   SRC_MID,   DST,        8, 2, 135, 0x02),
    ("dec, first forwards",   SRC_MID,   DST,        8, 2, 136, 0x02),
    ("dec, far forwards",     SRC_MID,   DST,        8, 2, 200, 0x02),
    ("fill, dec",             SRC_MID,   DST,        4, 3,  16, 0x03),
    ("overlap forwards",      SRC,       SRC + 2,    8, 1,   8, 0x00),
    ("overlap backwards",     SRC_MID,   SRC_MID - 2, 8, 1,  8, 0x02),
    ("undecoded param bits",  SRC,       DST,        4, 3,   4, 0xfc),
)


def geometry(width, height, stride, params):
    """step, the eight-bit signed difference, and the row pitch it makes."""
    w = width or 256
    h = height or 256
    wm1 = (w - 1) & 0xff
    step = -1 if params & 0x02 else 1
    diff = ((wm1 - stride) if step < 0 else (stride - wm1)) & 0xff
    if diff & 0x80:
        diff -= 256
    return w, h, step, diff, wm1 * step + diff


def reach(dst, width, height, stride, params):
    """The lowest and highest address a job touches, each axis counting whichever
    way it runs. A case whose reach leaves WORK is a bug in the case, not a
    finding, so run() refuses it rather than comparing a window it fell out of."""
    _, h, step, _, pitch = geometry(width, height, stride, params)
    row = ((width or 256) - 1) * step
    col = (h - 1) * pitch
    return (dst + min(row, 0) + min(col, 0), dst + max(row, 0) + max(col, 0))


def transfer(image, src, dst, width, height, stride, params):
    """Run one job over `image`, in place and one byte at a time - which is what
    the engine does, so an overlapping copy propagates here exactly as it does
    there. A fill reads its byte from the source address and never advances it."""
    w, h, step, _, pitch = geometry(width, height, stride, params)
    fill = params & 0x01
    for _ in range(h):
        rs, rd = src, dst
        for _ in range(w):
            image[rd] = image[src] if fill else image[rs]
            rs = (rs + step) & 0xffff
            rd = (rd + step) & 0xffff
        if not fill:
            src = (src + pitch) & 0xffff
        dst = (dst + pitch) & 0xffff


def workspace():
    """What WORK holds before a job: the source pattern, 0x40 counting up, then
    clearance. Where a byte landed says which one it was, and therefore which row
    and column the engine thought it was on."""
    return bytearray(bytes((0x40 + i) & 0xff for i in range(SRC_LEN))
                     + bytes(WORK_LEN - SRC_LEN))


def check(t, case, board, fails, notes):
    name, src, dst, width, height, stride, params = case
    board.running(name)
    lo, hi = reach(dst, width, height, stride, params)
    if lo < WORK or hi >= WORK + WORK_LEN:
        fails.append("%s: reaches %04x-%04x, outside the %04x-%04x window this "
                     "compares" % (name, lo, hi, WORK, WORK + WORK_LEN - 1))
        board.verdict(False)
        return 0

    before = workspace()
    t.vram(WORK, bytes(before))
    t.vram(PARAMS, bytes((src >> 8, src & 0xff, dst >> 8, dst & 0xff,
                          width, height, stride, params)))
    t.vram(PROG, PROGRAM)

    gpu.spin(t, JOB)

    want = bytearray(0x10000)
    want[WORK:WORK + WORK_LEN] = before
    transfer(want, src, dst, width, height, stride, params)
    want = want[WORK:WORK + WORK_LEN]
    got = t.read(t.vdp + WORK, WORK_LEN)

    _, h, _, diff, pitch = geometry(width, height, stride, params)
    bad = [i for i in range(WORK_LEN) if want[i] != got[i]]
    notes.append("%-22s w=%-3d h=%-3d stride=%-3d %s  diff=%+4d pitch=%+4d  %s"
                 % (name, width, height, stride, "dec" if params & 0x02 else "inc",
                    diff, pitch,
                    "OK" if not bad else "%d byte(s) wrong" % len(bad)))
    if bad:
        i = bad[0]
        fails.append("%s: [%04x] wanted %02x, got %02x (%d byte(s) differ)"
                     % (name, WORK + i, want[i], got[i], len(bad)))
    board.verdict(not bad)
    return WORK_LEN


def run(t):
    fails, notes, checks = [], [], 0
    t.unlock()
    board = scoreboard.start(t, "GPU DMA", (PROG, WORK + WORK_LEN))
    for case in CASES:
        checks += check(t, case, board, fails, notes)
    board.summary()
    return outcome.property_result(fails, notes, checks)


def main():
    ap = argparse.ArgumentParser()
    backend_args(ap)
    args = ap.parse_args()
    with open_backend(args) as t:
        return outcome.finish("GPU DMA", run(t))


if __name__ == "__main__":
    sys.exit(main())
