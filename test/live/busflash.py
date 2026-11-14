#!/usr/bin/env python3
"""Firmware update over the host bus, the way the configurator does it.

    python busflash.py --port COM10                         what is in the socket
    python busflash.py --port COM10 --uf2 FILE              ...and whether FILE suits it
    python busflash.py --port COM10 --uf2 FILE --write      flash, power-cycle, verify
    python busflash.py --port COM10 --uf2 FILE --verify     compare flash against FILE

No SWD involved: a board in the probe's socket can be brought up to date with
nothing but the bus, so a unit without a debug header is as reachable as one with.

**The protocol is the configurator's, and so is frozen.** Each UF2 block goes into
VRAM as its 32 byte header, 256 byte payload and magicEnd, VR63 is written with
bit 6 (firmware, not program data), bit 7 (write, not read) and the VRAM page, and
SR2 bit 7 stays set until the firmware is done. Bits 2-4 are its error code. A
board still running last year's firmware speaks it too, which is the whole point.

**Nothing is erased until the image is known to suit the board.** The board's
model comes back through config register 0 and the UF2's family ID must match it
here, before the first write; the firmware checks it again and refuses block 0
otherwise, and block 0 is the one that erases. Past that point a failure leaves
only BOOTSEL, so the image is validated block by block before anything is sent.

**Verify reads back after a power cycle, not before.** A write overwrites the
flash the old firmware is executing from, so it is the new image that answers the
read-back. With `switch` power the probe cycles the rail itself; with `manual`
power, cycle it by hand and run `--verify`.
"""

import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe as probe_module
from probe import spec

PAGE = 0x1D
FAMILY = {1: 0xE48BFF56, 2: 0xE48BFF59}
MODEL = {1: "RP2040", 2: "RP2350"}
FLASH_CONTROL = 0x3F
FLASH_FIRMWARE = 0x40
FLASH_WRITE = 0x80
TIMEOUT_S = 5


def status(p, index):
    p.register(15, index)
    value = p.status(1)[0]
    p.register(15, 0)
    return value


def config(p, index):
    p.register(15, 12)
    p.register(58, index)
    value = p.status(1)[0]
    p.register(15, 0)
    return value


def identify(p, wait_s=0):
    """Unlock, confirm a PICO9918 is answering, and return its model."""
    deadline = time.time() + wait_s
    while True:
        p.register(57, 0x1C)
        p.register(57, 0x1C)
        sr1 = status(p, 1)
        if (sr1 & 0xE8) == 0xE8:
            break
        if time.time() > deadline:
            raise SystemExit("SR1 reads %02x: not a PICO9918, or nothing in the socket" % sr1)
    model, hw, sw, patch = (config(p, i) for i in range(4))
    if model not in FAMILY:
        raise SystemExit("unknown Pico model %d" % model)
    print("PICO9918 %s  hw v%d.%d  firmware v%d.%d.%d"
          % (MODEL[model], hw >> 4, hw & 15, sw >> 4, sw & 15, patch))
    return model


def blocks(path, model):
    """The image as the firmware takes it, refusing anything it would refuse."""
    data = open(path, "rb").read()
    if not data or len(data) % 512:
        raise SystemExit("%s is not a whole number of UF2 blocks" % path)
    out = []
    for offset in range(0, len(data), 512):
        block = data[offset:offset + 512]
        magic0, magic1, flags, addr, size, number, count, family = struct.unpack("<8I", block[:32])
        (magic_end,) = struct.unpack("<I", block[508:])
        if (magic0, magic1, magic_end) != (0x0A324655, 0x9E5D5157, 0x0AB16F30):
            raise SystemExit("block %d: bad UF2 magic" % len(out))
        if family != FAMILY[model]:
            raise SystemExit("block %d: family %08x, but the board is an %s (%08x)"
                             % (len(out), family, MODEL[model], FAMILY[model]))
        if flags != 0x2000 or size != 256 or count >= 0x400 or number != len(out):
            raise SystemExit("block %d: flags %x, payload %d, number %d of %d"
                             % (len(out), flags, size, number, count))
        if not 0x10000000 <= addr < 0x10040000 or addr & 0xFF:
            raise SystemExit("block %d: target %08x is outside the firmware's 256 KiB" % (number, addr))
        out.append(block[:288] + block[508:])
    if len(out) != count:
        raise SystemExit("%d blocks, but the header says %d" % (len(out), count))
    return out


def transfer(p, block, control):
    """Stage one block, run one flash operation on it, and return SR2."""
    p.vram_write(PAGE << 8, block)
    p.register(FLASH_CONTROL, control | PAGE)
    deadline = time.time() + TIMEOUT_S
    while True:
        sr2 = status(p, 2)
        if not sr2 & 0x80:
            return sr2
        if time.time() > deadline:
            raise SystemExit("flash operation never finished: SR2 %02x" % sr2)


def write(p, image):
    start = time.time()
    for i, block in enumerate(image):
        sr2 = transfer(p, block, FLASH_FIRMWARE | FLASH_WRITE)
        if sr2 & 0x1C:
            raise SystemExit("block %d of %d refused: SR2 %02x" % (i, len(image), sr2))
    print("written %d blocks in %.1f s" % (len(image), time.time() - start))


def verify(p, image):
    differ = [i for i, block in enumerate(image)
              if transfer(p, block, FLASH_FIRMWARE) & 0x1C
              or p.vram_read(PAGE << 8, 256) != block[32:288]]
    if differ:
        print("verify: %d of %d blocks differ, first at %d" % (len(differ), len(image), differ[0]))
        return 1
    print("verify: all %d blocks match" % len(image))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", default=None, metavar="PORT[:MODE]",
                    help="the probe's CDC port; otherwise LIVE9918_PROBE_CDC")
    ap.add_argument("--uf2", default=None, help="the firmware image")
    action = ap.add_mutually_exclusive_group()
    action.add_argument("--write", action="store_true", help="flash, then verify")
    action.add_argument("--verify", action="store_true", help="compare flash with the image")
    args = ap.parse_args()
    where = spec(args.port)
    if not where:
        raise SystemExit("no probe port: pass --port COM10 or set LIVE9918_PROBE_CDC")
    if (args.write or args.verify) and not args.uf2:
        raise SystemExit("--write and --verify need --uf2")
    with probe_module.opened(where) as p:
        model = identify(p)
        if not args.uf2:
            return 0
        image = blocks(args.uf2, model)
        print("%s: %d blocks, %d KiB, %s" % (os.path.basename(args.uf2), len(image),
                                             len(image) // 4, MODEL[model]))
        if args.verify:
            return verify(p, image)
        if not args.write:
            return 0
        write(p, image)
        if not p.state().get("switched"):
            print("power is manual: cycle the board, then run --verify")
            return 0
        p.off()
        p.arm(where[1])
        identify(p, wait_s=TIMEOUT_S)
        return verify(p, image)


if __name__ == "__main__":
    sys.exit(main())
