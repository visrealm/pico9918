#!/usr/bin/env python3
"""What a scene does to a VDP, and the addresses it does it at.

The whole suite is written against `VdpAccess` and nothing below it. Two backends
implement it - a board over SWD and an in-process library behind a pipe - and a
scene, a golden and a property cannot tell which one they are driving. That is
what makes the same 111 scenes a gate for the library on a desktop and a gate for
the firmware on hardware.
"""

import struct

from suite.access.image import png_bytes, rgb_palette, unpack_nibbles

PIXELS_X = 256
CAPTURE_ROWS = 240

# what liveTestCapture.request means, per livetest.h. Nothing here sends one: the board
# backend in the firmware repository does, and it subclasses this.
REQUEST_WINDOW = 1
REQUEST_CRC = 2

# Configuration bytes a run may want to turn, per pico9918_config.h. These two are
# the ones the renderer never re-reads: everything else in the block is picked up at
# end of frame from `tms9918->config`, which is where scenes.quiet writes.
PICO9918_CONF_CLOCK_TESTED = 4
PICO9918_CONF_CLOCK_PRESET_ID = 10
PICO9918_CONF_SAVE_FORCED = 252
PICO9918_CONF_SAVE_TO_FLASH = 255
CONFIG_BYTES = 256

# offsets into the VDP's own address space, per impl/pico9918_priv.h
VRAM_REGISTERS = 0x6000
VRAM_PRAM = 0x5000
VRAM_STATUS = 0xB000


class VdpAccess:
    """Everything a scene does to a VDP, in terms of two operations: read a range of
    the instance and write one.

    Separated from the transport because that is all a scene ever needed. The device
    reaches the instance over SWD and the desktop backend reaches an in-process one
    through a pipe, and neither difference reaches this far - so a scene, a golden
    and a property assert the same thing against either. A backend supplies `read`,
    `write`, `off`, `inst`, `vdp` and `_default_palette_bytes`; everything below is
    written once.

    `inst` and `vdp` are NOT interchangeable. Field offsets from `off` are
    instance-relative; the VDP's own address space starts at the instance's `vram`
    union and goes through `vdp`. Writing a scene to `inst` instead lands it on the
    scalars ahead of that union, which renders a blank instance while every read
    agrees with itself."""

    def vram(self, addr, data):
        """write VDP address space - VRAM below 0x4000, registers at 0x6000"""
        self.write(self.vdp + addr, bytes(data))

    def reg(self, index, value):
        self.vram(VRAM_REGISTERS + index, bytes([value]))

    def regs(self):
        return self.read(self.vdp + VRAM_REGISTERS, 64)

    def palette(self, index, rgb):
        """rgb is 12-bit 0xRGB. PRAM holds 0xFRGB big-endian - pico9918.c
        byte-swaps defaultPalette on the way in."""
        self.vram(VRAM_PRAM + index * 2, struct.pack(">H", 0xF000 | (rgb & 0x0FFF)))
        self.write(self.inst + self.off["palDirty"], b"\x01")

    def palette_all(self, entries):
        """All 64 at once, which is what a dump brings. One transfer, not 64."""
        data = b"".join(struct.pack(">H", 0xF000 | (v & 0x0FFF)) for v in entries)
        self.vram(VRAM_PRAM, data)
        self.write(self.inst + self.off["palDirty"], b"\x01")

    def default_palette(self):
        """The library's own boot palette, read out of the build under test rather
        than copied into this file where it could drift. It holds 0xFRGB and the
        library byte-swaps it on the way into PRAM."""
        if self._defaultPalette is None:
            raw = self._default_palette_bytes()
            self._defaultPalette = [struct.unpack_from("<H", raw, i * 2)[0] & 0x0FFF
                                    for i in range(64)]
        return self._defaultPalette

    def unlock(self):
        self.write(self.inst + self.off["isUnlocked"], b"\x01")
        self.write(self.inst + self.off["lockedMask"], b"\x3f")

    def lock(self):
        """Unlocking is sticky until the board resets, so a locked scene has to
        say so rather than assume it."""
        self.write(self.inst + self.off["isUnlocked"], b"\x00")
        self.write(self.inst + self.off["lockedMask"], b"\x07")

    def conf(self, index, value=None):
        """One byte of the board's running configuration."""
        addr = self.inst + self.off["config"] + index
        if value is None:
            return self.read(addr, 1)[0]
        self.write(addr, bytes([value]))

    def frame_png(self, frame=None):
        """The capture as PNG bytes, coloured by the palette the board is holding
        now. Read per capture, not once: a scene that sets its own palette - every
        dump that ships one - is a different picture under the firmware's default,
        and the picture is the whole point."""
        rows, pixels = frame or self.capture()
        width = self.width
        pram = self.read(self.vdp + VRAM_PRAM, 64 * 2)   # 0xFRGB, big-endian
        pal = rgb_palette(struct.unpack_from(">H", pram, i * 2)[0] for i in range(64))
        if width == PIXELS_X and self.regs()[0] & 0x04:
            pixels, width = unpack_nibbles(pixels, rows, width)
        return png_bytes(width, rows, pixels[:rows * width], pal), rows

    def png(self, path, frame=None):
        data, rows = self.frame_png(frame)
        with open(path, "wb") as f:
            f.write(data)
        return rows
