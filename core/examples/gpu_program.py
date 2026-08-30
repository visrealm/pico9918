#!/usr/bin/env python3
"""pico9918-core - run a program on the F18A's GPU, from one thread.

The F18A has a TMS9900 on it. A program sitting in VRAM runs on that core, reaches
the VDP register file through the GPU's >6000 window, and draws by writing VRAM - so
the host's whole job is to load it, point the GPU at it, and let it run.

Except that "let it run" is where a host has a real decision to make, and it is what
this example is about.

A GPU program is not a subroutine. It runs beside the display, and it can WAIT on the
display: the scanline being scanned out is readable at >7000, and a program that pages
a bitmap wants to do it in the vertical blank, so it polls that address until the
raster is somewhere safe. If nothing advances the raster while the program runs, that
poll never ends.

There are two honest shapes for a host, and the library supports both:

  ONE THREAD, INTERLEAVED. What this file does. gpu_step_n() runs a bounded number of
  instructions and returns with the PC kept, so the loop below alternates slices of
  program with lines of raster and needs no thread at all:

      while vdp.gpu_step_n(SLICE):
          vdp.raster()

  A THREAD FOR THE GPU. What the firmware does - core 0 runs the GPU while core 1
  renders. gpu_program.c is written that way, against the same two programs, and is
  worth reading beside this one. It is not what a Python host should reach for: the
  GIL is held across every call into this module, deliberately, so a second thread
  would buy nothing. See BINDINGS-PLAN.md for what releasing it would cost.

gpu_step() - unbounded, on the calling thread - is the third option and is the one
that cannot service a wait: the caller that would advance the raster is the one
blocked inside it.

The default program is **Tursi's** F18A GPU Mandelbrot, from test/suite/data/gpu-programs/
where it is credited in full. 548 bytes: it sets VR0-VR7 itself, builds its own name
table, and draws 49,152 pixels in Graphics II over x -2.0..+0.5, y +1.25..-1.25 with
14 iterations of Q13 fixed point before halting on IDLE. Twenty-three million TMS9900
instructions. It is somebody else's program, which is the point - nothing in it was
written with this library in mind. It never looks at the raster, so it runs the same
under either shape.

The other thing a host has to get right: the GPU is an F18A feature, so the module
must be built PICO9918_MODE=1 and the chip must be unlocked. A locked TMS9918A has no
GPU to run anything on. `pico9918.MODE` reports which build is on the path.

Build the module, then run this against it:

    cmake -S . -B build -DPICO9918_MODE=1 -DPICO9918_PYTHON_BINDING=ON
    cmake --build build
    PYTHONPATH=build/bindings/python python examples/gpu_program.py mandel.ppm

A program and the address it was assembled for can be given instead. cube.bin, beside
it, is a solid cube turning on the F18A's bitmap layer, and it is the one that waits
on the raster:

    python examples/gpu_program.py cube.ppm .../gpu-programs/cube.bin 0x3200

Its colours come out wrong here, and that is the interesting part: the cube shades
itself by rewriting palette RAM, and rgb() below colours the frame from the boot
palette instead. `test/suite/view.py --gpu` reads the live one and shows it turning.
"""

import os
import sys

try:
    import pico9918
except ImportError:
    sys.exit("no pico9918 module on the path - build it and set PYTHONPATH to "
             "<build>/bindings/python")

HERE = os.path.dirname(os.path.abspath(__file__))
MANDEL = os.path.join(HERE, "..", "test", "suite", "data", "gpu-programs", "mandel.bin")

ENTRY = 0x1B02  # where Tursi's program starts. Even, because the GPU refuses an odd one

# The GPU may occupy base VRAM below the F18A's GRAM window. Blanked before the
# program is loaded, so nothing already in VRAM can end up as part of the picture.
PROGRAM_SPACE = 0x4000

ROWS = 192

# Instructions between raster lines. Only the size of the grain: bigger spends less
# time in the loop, smaller answers a program's wait sooner. The library's own bound
# is per call, so nothing here has to know how long an instruction takes.
SLICE = 20000

# Long enough that a program waiting on something this host does not provide is
# reported rather than waited on forever.
GIVE_UP_LINES = 3_000_000

# The public header names only R0-R7, the ones a TMS9918A has, so the enhanced
# registers are written by number.
VR_GPU_HI = 0x36  # GPU start address, high byte
VR_GPU_LO = 0x37  # low byte - writing it arms the GPU at that address


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "gpu.ppm"
    path = sys.argv[2] if len(sys.argv) > 2 else MANDEL
    entry = int(sys.argv[3], 0) if len(sys.argv) > 3 else ENTRY

    if entry & 1 or entry >= PROGRAM_SPACE:
        sys.exit("%#06x cannot be a start address: the GPU refuses an odd one, and a "
                 "program lives below %#06x" % (entry, PROGRAM_SPACE))

    if not pico9918.MODE:
        sys.exit("this module was built PICO9918_MODE=0, and a TMS9918A has no GPU")

    with open(path, "rb") as f:
        program = f.read(PROGRAM_SPACE - entry)
    if not program:
        sys.exit("%s is empty" % path)

    vdp = pico9918.Vdp()
    vdp.reset()
    vdp.gpu_init()
    vdp.unlock()  # 0x1c twice to VR57, which the binding wraps

    # reset() leaves VRAM alone, the way a real TMS9918A's is undefined at power-on, so
    # the program goes in as a blanked image rather than on its own.
    image = bytearray(PROGRAM_SPACE)
    image[entry:entry + len(program)] = program

    # The register file is left as reset() made it. Do not blank it from here: VR48 is
    # the address auto-increment the data port uses, and zeroing it would land every
    # byte of the load below on the same address.
    vdp.write_vram(0, bytes(image))

    # One frame before the program starts, so the raster it reads has a value in it
    # rather than whatever a reset left.
    vdp.raster(481)

    # Arm and run. Writing the low byte is what latches the address and starts it, so
    # the high byte goes first.
    vdp.write_reg(VR_GPU_HI, entry >> 8)
    vdp.write_reg(VR_GPU_LO, entry & 0xFF)

    vdp.gpu_reset_time()

    # The interleave. Neither call blocks on the other, so a program that waits for the
    # vertical blank gets a blank to wait for, from the one thread that is running.
    lines = frames = 0
    while vdp.gpu_step_n(SLICE):
        frames += vdp.raster()
        lines += 1
        if lines >= GIVE_UP_LINES:
            sys.exit("%s did not finish in %d raster lines" % (path, lines))
    us = vdp.gpu_time(0)

    # Wall clock on whatever is emulating it, not the TMS9900's own time - the same
    # accumulator the firmware reports the GPU's share of a frame from. The frames are
    # this host's, and it renders them as fast as it can rather than at 60Hz: a display
    # is what paces a board, and there is no display here.
    print("%s: %d bytes at 0x%04X, %d us of host time over %d frames"
          % (path, len(program), entry, us, frames))

    # What it drew. The GPU wrote VRAM and the registers; rendering is unchanged from
    # any other frame, so the frame comes back the way every other example reads it.
    with open(out, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (pico9918.PIXELS_X, ROWS))
        f.write(vdp.rgb(ROWS))
    print("wrote %s (%dx%d)" % (out, pico9918.PIXELS_X, ROWS))


main()
