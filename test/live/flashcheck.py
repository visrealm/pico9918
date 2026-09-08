#!/usr/bin/env python3
"""Drive one R63 program-data READ on the board and check what it reports.

The scene suite never writes R63, so nothing else here exercises the flash path or
the completion that ends it. A read is the non-destructive half: it searches the
program-data region and copies a block into VRAM, and erases nothing.

Usage: python flashcheck.py --board pro|2040
"""

import argparse
import sys
import time

import live9918
from suite.access.vdp import VRAM_REGISTERS, VRAM_STATUS

SR_GPU = 2
REG_FLASH_CONTROL = 63

RESULT = {
    0: "OK",
    1: "ERR_HEADER",
    2: "ERR_SEQUENCE",
    3: "ERR_SIZE",
    4: "ERR_VERIFY",
    5: "ERR_UNSUPPORTED",
    6: "ERR_FULL",
    7: "(7, unassigned)",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", choices=("pro", "2040"), default="pro")
    args = ap.parse_args()

    with live9918.Live(live9918.default_elf(args.board)) as t:
        t.unlock()

        # This runs against whatever scene the board is displaying, and page 0 is
        # where a bitmap mode keeps its first 32 patterns. Borrow it, then give it back.
        held = t.read(t.vdp + 0, 0x100)

        # A block the firmware cannot match: id hint 0xffffffff, then a GUID of our
        # own. Unmatched and unallocated, so the read either allocates a free block
        # and reports OK, or reports FULL. Both end the operation, which is the point.
        block = bytearray(256)
        block[0:4] = (0xFFFFFFFF).to_bytes(4, "little")
        block[4:20] = bytes(range(0x40, 0x50))
        t.vram(0x0000, bytes(block))

        before = t.read(t.vdp + VRAM_STATUS + SR_GPU, 1)[0]

        # b7=0 read, b6=0 program data, b5-0 = the VRAM page holding the block.
        t.reg(REG_FLASH_CONTROL, 0x00)

        # This rig writes the register FILE, so pico9918_write_reg never runs and
        # nothing arms the request. Arm it exactly as that path does - the pending
        # flag and the busy bit - which is the only way to reach the flash code here.
        t.write(t.vdp + VRAM_STATUS + SR_GPU, bytes([0x80]))
        t.write(t.inst + t.off["flash"], bytes([1]))

        # core 0's GPU loop picks it up; a program-data read is a flash search, not an erase
        deadline = time.time() + 5.0
        while time.time() < deadline:
            if not t.read(t.inst + t.off["flash"], 1)[0]:
                break
            time.sleep(0.05)

        after = t.read(t.vdp + VRAM_STATUS + SR_GPU, 1)[0]
        pending = t.read(t.inst + t.off["flash"], 1)[0]
        gpuctl = t.read(t.vdp + VRAM_REGISTERS + 56, 1)[0]
        blockid = int.from_bytes(t.read(t.vdp + 0, 4), "little")

        print("SR2 before        0x%02x" % before)
        print("SR2 after         0x%02x  busy=%d result=%s progress=%d"
              % (after, (after >> 7) & 1, RESULT[(after >> 2) & 7], after & 3))
        print("R56 (GPU control) 0x%02x" % gpuctl)
        print("pending flash     %s" % ("n/a" if pending is None else pending))
        print("block id in VRAM  0x%08x" % blockid)

        bad = []
        if after & 0x80:
            bad.append("SR2 bit 7 still set - the operation never ended")
        if pending not in (None, 0):
            bad.append("pending flash flag still set")
        if gpuctl != 0:
            bad.append("R56 not cleared")
        if ((after >> 2) & 7) not in (0, 6):
            bad.append("unexpected result %s" % RESULT[(after >> 2) & 7])

        # The other half: with no callback registered the engine must end the request
        # itself rather than sit busy forever. The board always registers one, so the
        # only way to reach that arm here is to take the registration away.
        cb = t.sym.get("gpuFlash")
        if cb is None:
            bad.append("gpuFlash not in the ELF - cannot reach the no-callback arm")
        else:
            saved = t.read(cb, 8)
            t.write(cb, bytes(8))
            t.reg(REG_FLASH_CONTROL, 0x00)
            t.write(t.vdp + VRAM_STATUS + SR_GPU, bytes([0x80]))
            t.write(t.inst + t.off["flash"], bytes([1]))

            deadline = time.time() + 5.0
            while time.time() < deadline:
                if not t.read(t.inst + t.off["flash"], 1)[0]:
                    break
                time.sleep(0.05)

            nocb = t.read(t.vdp + VRAM_STATUS + SR_GPU, 1)[0]
            nopend = t.read(t.inst + t.off["flash"], 1)[0]
            t.write(cb, saved)

            print("\nno callback registered:")
            print("SR2               0x%02x  busy=%d result=%s"
                  % (nocb, (nocb >> 7) & 1, RESULT[(nocb >> 2) & 7]))
            print("pending flash     %d" % nopend)

            if nocb & 0x80:
                bad.append("no-callback: SR2 bit 7 still set - the engine sat busy")
            if nopend:
                bad.append("no-callback: pending flash flag still set")
            if ((nocb >> 2) & 7) != 5:
                bad.append("no-callback: result is %s, expected ERR_UNSUPPORTED"
                           % RESULT[(nocb >> 2) & 7])

        t.vram(0x0000, held)
        t.write(t.vdp + VRAM_STATUS + SR_GPU, bytes([before]))

        if bad:
            for b in bad:
                print("FAIL: " + b)
            return 1
        print("\nPASS: both the completion and the no-callback arm end the request")
        return 0


if __name__ == "__main__":
    sys.exit(main())
