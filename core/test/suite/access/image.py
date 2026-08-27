#!/usr/bin/env python3
"""Turning a captured index buffer into something a person can look at.

Nothing here touches a board or a shim. A capture is a byte per pixel and a
palette is 64 entries of 0xFRGB, and these four functions are every conversion
the harness performs on them.
"""

import struct
import zlib


def png_bytes(width, height, indices, palette):
    """An 8-bit paletted PNG, which is what an index buffer already is."""
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    raw = b"".join(b"\x00" + indices[y * width:(y + 1) * width] for y in range(height))
    plte = b"".join(palette) + b"\x00" * (3 * (256 - len(palette)))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0))
            + chunk(b"PLTE", plte)
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def write_png(path, width, height, indices, palette):
    with open(path, "wb") as f:
        f.write(png_bytes(width, height, indices, palette))


def unpack_nibbles(indices, rows, width):
    """80 columns at four bits a pixel pack two into each byte, so a 256-byte line
    is 512 pixels. Returns the widened pixels and their new width."""
    wide = bytearray(rows * width * 2)
    for i in range(rows * width):
        wide[i * 2] = indices[i] >> 4
        wide[i * 2 + 1] = indices[i] & 0x0F
    return bytes(wide), width * 2


def rgb_palette(entries):
    """0xFRGB, as PRAM and a dump both hold it, to the PNG's byte a channel."""
    return [bytes([((v >> 8) & 0xF) * 17, ((v >> 4) & 0xF) * 17, (v & 0xF) * 17])
            for v in entries]
