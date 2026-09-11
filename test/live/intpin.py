"""/INT on metal: the two sources, and that the lock latch is not a third.

The bench rig drives no bus, so nothing here reads a status register - what it asserts is
the part a host cannot fake: the scanline source alone pulls the physical pin down, and a
relocked device keeps it down. Both are `f18a_cpu.vhd:617`'s shape, where the pin is
`(intr_ff and reg1ie) or (horz_ff and reg0ie1)` with no lock term.

Every step drives the VDP by writing the instance over SWD and then waits for the
renderer to pass the armed line, so the pin is driven by the firmware's own per-scanline
path rather than by anything this file calls.

    python intpin.py --board 2040
    python intpin.py --board pro

Usage: as its own stage, after a suite run that has already left the board quiet. It
writes R0, R1 and R19 and leaves the display where it found it, so anything that applies
a scene afterwards is unaffected.
"""

import argparse
import sys

from live9918 import Live, board_args, default_elf

# SIO's GPIO output register, which is what gpio_put writes. The pin is active low.
SIO_GPIO_OUT = 0xD0000010

REG_SCANLINE_INT = 19
REG_ENHANCED2 = 0x32
R50_GPU_TRIGGERS = 0x60
R0_INT_SCANLINE = 0x10
R1_RAM_16K = 0x80
R1_DISP_ACTIVE = 0x40
R1_INT_ENABLE = 0x20

ARMED_LINE = 100


def int_asserted(board, gpio):
    """the pin as the host would see it: active low, so asserted is a LOW output"""
    word = int.from_bytes(board.read(SIO_GPIO_OUT, 4), "little")
    return (word & (1 << gpio)) == 0


def settle(board):
    """three frames, so the renderer has passed the armed line and reconciled"""
    board.wait_frames(3)


def main():
    ap = argparse.ArgumentParser()
    board_args(ap)
    ap.add_argument("--gpio", type=int, default=22, metavar="N",
                    help="GPIO driving /INT (PICO9918_GPIO_INT)")
    args = ap.parse_args()
    if args.desktop:
        raise SystemExit("intpin.py reads a physical pin - it needs a board")

    elf = args.elf or default_elf(args.board)
    failures = []

    with Live(elf, probe=args.probe) as board:

        def check(label, want):
            got = int_asserted(board, args.gpio)
            ok = got == want
            print("[%s] %-22s /INT %s" % ("PASS" if ok else "FAIL", label,
                                          "asserted" if got else "released"))
            if not ok:
                failures.append("%s: /INT %s, expected %s"
                                % (label, "asserted" if got else "released",
                                   "asserted" if want else "released"))

        # the GPU's run state is board state no register describes, so a program an
        # earlier stage armed would write registers underneath this one
        board.reg(REG_ENHANCED2, board.regs()[REG_ENHANCED2] & ~R50_GPU_TRIGGERS)

        # ---- the scanline source on its own, with the frame source disabled ----
        # R1's interrupt enable stays CLEAR throughout: it gates the frame source only,
        # so a pin that follows here can only be the scanline source's.
        board.unlock()
        board.reg(0, R0_INT_SCANLINE)
        board.reg(1, R1_RAM_16K | R1_DISP_ACTIVE)
        board.reg(REG_SCANLINE_INT, ARMED_LINE)
        settle(board)
        check("armed, unlocked", True)

        # ---- the relock, which is the whole behaviour under test ----
        # R19 and R0 survive it on the part, so the source is still armed and flagged.
        board.lock()
        settle(board)
        check("armed, relocked", True)

        # ---- and the source withdrawn, so the row above is not a stuck pin ----
        board.reg(0, 0x00)
        settle(board)
        check("disarmed, relocked", False)

        # ---- leave nothing armed behind ----
        board.reg(REG_SCANLINE_INT, 0x00)
        settle(board)

    if failures:
        print("\n%d of 3 checks FAILED" % len(failures))
        for f in failures:
            print("  " + f)
        return 1
    print("\nall 3 checks passed on %s" % args.board)
    return 0


if __name__ == "__main__":
    sys.exit(main())
