#!/usr/bin/env python3
"""What the module has to do to be worth importing.

Run it with the built module on the path:

    PYTHONPATH=<build>/bindings/python python bindings/python/test.py
    python bindings/python/test.py --png frame.png     # needs pillow

The renderer's own correctness is test/golden and test/suite - byte-exact frames
against committed ones. Nothing here re-freezes a picture; these are the things
that break when the binding is wrong rather than when the renderer changes.
"""

import argparse
import gc
import os
import sys
import zlib

try:
    import pico9918
except ImportError:
    sys.exit("no pico9918 module on the path - build it and set PYTHONPATH to "
             "<build>/bindings/python")

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGE = os.path.join(HERE, "image.bin")
SUITE = os.path.join(HERE, "..", "..", "test", "suite")
MANDEL = os.path.join(SUITE, "data", "gpu-programs", "mandel.bin")
REFERENCE = os.path.join(SUITE, "oracle", "reference", "gpu-mandel.bin.z")
CUBE = os.path.join(SUITE, "data", "gpu-programs", "cube.bin")
CUBE_REFERENCE = os.path.join(SUITE, "oracle", "reference", "gpu-cube.bin.z")
CUBE_FRAMES = 256  # what cube.a99's FRAMES says, and it flips once a frame

ROWS = 192
GRAPHICS_I = 0


def brief(value):
    text = repr(value)
    return text if len(text) <= 80 else "%s... (%d)" % (text[:80], len(value))


def check(name, got, want):
    if got != want:
        raise AssertionError("%s: %s, expected %s" % (name, brief(got), brief(want)))
    print("  ok  %s = %s" % (name, brief(got)))


def load_image(vdp):
    """A screen dumped off a real machine: 16KB of VRAM then the eight registers."""
    data = open(IMAGE, "rb").read()
    vdp.write_regs(list(data[16 * 1024:]))
    vdp.write_vram(0, data[:16 * 1024])
    return data


def test_render():
    print("a dumped screen renders")
    vdp = pico9918.Vdp()
    load_image(vdp)
    check("display_mode", vdp.display_mode(), GRAPHICS_I)
    check("display_enabled", vdp.display_enabled(), True)
    check("line_bytes", vdp.line_bytes(), pico9918.PIXELS_X)

    indices = vdp.indices(ROWS)
    check("indices", len(indices), ROWS * pico9918.PIXELS_X)
    check("colours used", sorted(set(indices)), [4, 13, 15])
    check("rgb", len(vdp.rgb(ROWS)), ROWS * pico9918.PIXELS_X * 3)
    return vdp


def test_bus():
    """The two ports, and the read-ahead that makes a seek fetch a byte."""
    print("the host bus")
    vdp = pico9918.Vdp()
    payload = bytes(range(16))

    vdp.write_addr(0x00)          # low address byte
    vdp.write_addr(0x40 | 0x01)   # 0x40: the pair was a write address, so 0x0100
    for b in payload:
        vdp.write_data(b)
    check("write_vram round trip", vdp.read_vram(0x0100, len(payload)), payload)

    vdp.write_addr(0x00)
    vdp.write_addr(0x01)          # 0x00: a read address, which also prefetches
    check("read_data after a seek", bytes(vdp.read_data() for _ in payload), payload)


def test_interrupt():
    print("the frame interrupt")
    vdp = pico9918.Vdp()
    vdp.write_reg(1, 0xE0)        # 16K, display on, interrupt enable
    check("idle", vdp.interrupt_status(), False)
    vdp.interrupt_set()
    check("asserted", vdp.interrupt_status(), True)
    vdp.peek_status()
    check("after peek_status", vdp.interrupt_status(), True)
    check("read_status has SR0's F", vdp.read_status() & 0x80, 0x80)
    check("after read_status", vdp.interrupt_status(), False)


def test_unlock():
    """R49 is an F18A register, so a locked device drops the write."""
    print("the F18A unlock")
    vdp = pico9918.Vdp()
    vdp.unlock()
    vdp.write_reg(0x31, 0xA0)
    check("R49 once unlocked", vdp.reg(0x31), 0xA0)


def test_gpu():
    """Tursi's Mandelbrot, against the frame the renderer suite froze for it. The GPU
    sets its own registers and builds its own name table, so loading it and starting it
    is the whole of the host's part - and the picture is 49,152 pixels of proof that
    every call between here and the TMS9900 core did what it said."""
    print("a GPU program")
    entry = 0x1B02
    vdp = pico9918.Vdp()
    vdp.gpu_init()
    vdp.unlock()
    vdp.write_vram(entry, open(MANDEL, "rb").read())
    vdp.write_reg(0x36, entry >> 8)
    vdp.write_reg(0x37, entry & 0xFF)   # the low byte is what starts it

    vdp.gpu_reset_time()
    vdp.gpu_step()
    elapsed = vdp.gpu_time(0)
    if elapsed == 0:
        raise AssertionError("the GPU reported no time at all")
    print("  ok  ran for %u us" % elapsed)

    want = zlib.decompress(open(REFERENCE, "rb").read())
    rows = len(want) // pico9918.PIXELS_X
    check("rows", rows, ROWS)
    check("the frame the suite froze", vdp.indices(rows), want)


def test_gpu_interleaved():
    """The cube, on one thread, against the same frozen frame the suite compares it to.

    It is here rather than beside the Mandelbrot because it is the program that WAITS:
    it pages its bitmap in the vertical blank, so it only finishes if raster() moves the
    scanline register while gpu_step_n() is between slices.

    The picture is not what makes this a test. The cube draws its 192 frames whatever
    the pacing, so a broken interleave still produces the right image - what it cannot
    produce is the right NUMBER of frames. One flip a frame is the most a program
    waiting on the blank can manage, so anything under 192 says the wait fell through:
    a budget expires between a compare and the jump that reads it, and a resume that
    rebuilt the CPU with a zeroed status register took the branch on flags nothing set.
    """
    print("a GPU program that waits on the raster")
    entry = 0x3200
    program = open(CUBE, "rb").read()
    image = bytearray(0x4000)
    image[entry:entry + len(program)] = program

    vdp = pico9918.Vdp()
    vdp.gpu_init()
    vdp.unlock()
    vdp.write_vram(0, bytes(image))
    vdp.raster(481)                     # a frame, so the raster it reads holds a value
    vdp.write_reg(0x36, entry >> 8)
    vdp.write_reg(0x37, entry & 0xFF)   # the low byte is what starts it

    frames = lines = 0
    while vdp.gpu_step_n(20000):
        frames += vdp.raster()
        lines += 1
        if lines > 1_000_000:
            raise AssertionError("the cube never finished - is raster() advancing?")
    print("  ok  %d frames over %d raster lines" % (frames, lines))
    if frames < CUBE_FRAMES:
        raise AssertionError("%d frames for %d flips - the wait is falling through"
                             % (frames, CUBE_FRAMES))

    want = zlib.decompress(open(CUBE_REFERENCE, "rb").read())
    check("the frame the suite froze", vdp.indices(ROWS), want)


def test_lifetime():
    """What a functional test cannot see. Both assertions bind in a release interpreter:
    the type's refcount tracks live instances exactly, so a dealloc that never runs shows
    up in the first; and the allocated-block count is flat across the calls that build
    bytes and export buffers, including the parse that fails after the buffer is taken."""
    print("lifetime")
    base = sys.getrefcount(pico9918.Vdp)
    held = [pico9918.Vdp() for _ in range(200)]
    check("type refs while 200 are held", sys.getrefcount(pico9918.Vdp) - base, 200)
    del held
    check("type refs once released", sys.getrefcount(pico9918.Vdp) - base, 0)

    vdp = pico9918.Vdp()
    payload = bytearray(16)
    gc.collect()
    before = sys.getallocatedblocks()
    for _ in range(2000):
        vdp.write_vram(0, payload)
        vdp.indices(2)
        vdp.rgb(2)
        vdp.read_vram(0, 16)
        vdp.line()
        try:
            vdp.write_vram(-1, payload)      # rejected after the buffer would be taken
        except TypeError:
            pass
    gc.collect()
    leaked = sys.getallocatedblocks() - before
    if leaked > 16:
        raise AssertionError("leaked %d allocated blocks over 2000 iterations" % leaked)
    print("  ok  allocated blocks stable = %+d" % leaked)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--png", help="write the dumped screen here (needs pillow)")
    args = ap.parse_args()

    print("pico9918 %s, %d pixels a line, %d bytes a pixel"
          % ("F18A" if pico9918.MODE else "TMS9918A", pico9918.PIXELS_X, pico9918.PIXEL_SIZE))

    vdp = test_render()
    test_bus()
    test_interrupt()
    test_lifetime()
    if pico9918.MODE:
        test_unlock()
        test_gpu()
        test_gpu_interleaved()

    if args.png:
        from PIL import Image
        Image.frombytes("RGB", (pico9918.PIXELS_X, ROWS), vdp.rgb(ROWS)).save(args.png)
        print("wrote", args.png)

    print("PASS")


main()
