#!/usr/bin/env python3
"""The live harness against an in-process library instead of a board.

    python freeze.py --desktop            every scene, no board
    python runner.py --desktop            the stages that do not measure time

`Live` reaches a running board's instance over SWD. This reaches one inside
`shim/live_shim` over a pipe. Both are the same two operations - read a range
of the instance, write one - plus "render a frame and give me the rows", so a scene,
a golden and a property assert the same thing against either.

**The rows are the same artifact.** The shim captures through PICO9918_LINE_CAPTURE,
the hook the firmware's own harness uses, at the same call site inside
pico9918_frame_scanline and from the same pointer. Byte-identity with a device capture
is structural, not a coincidence to be re-checked per scene.

**What this cannot do, and must not appear to.** The device measures microseconds a
line and which lines did not fit; neither exists here, so `dropped` is always empty
and there is no per-line timing hook in the shim's build at all. `perf.py` and the
diag stages stay on hardware. A desktop pass says the renderer computes the right
picture; only the board says it computes it in time.

The one number that does come back is a GPU program's runtime, and it is the
library's own accumulator rather than a hook added here - the same one the device
reports from. It measures this machine, so it is recorded and never compared against
a board's.

The tier is the build's, not a flag: the shim renders 256 bytes a line by default
and 512 with LIVE_DESKTOP_TEXT80_8BPP=ON, which is the PRO's 8bpp 80-column tier.
The reference set follows from the width the capture reports, exactly as it does on
the device.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
from suite.access.vdp import PIXELS_X, VdpAccess

# Where the shim gets built. Overridable, because a second tier is a second build
# directory and CI will not put it where a workstation does.
LIBRARY_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
SHIM = os.environ.get("LIVE9918_SHIM") or os.path.join(
    LIBRARY_ROOT, "build-live-desktop",
    "live_shim.exe" if os.name == "nt" else "live_shim")


class Desktop(VdpAccess):
    """A board-shaped object backed by the shim. The method surface is `Live`'s,
    minus what only a board can answer."""

    def __init__(self, shim=None):
        self.shim = shim or SHIM
        if not os.path.exists(self.shim):
            raise SystemExit(
                "no desktop shim at %s - build it with:\n"
                "  cmake -S test/suite/shim -B build-live-desktop -G Ninja -DCMAKE_C_FLAGS=-O2\n"
                "  cmake --build build-live-desktop" % self.shim)
        self.proc = None
        # a desktop run has no ELF, no probe and no clock; the record's fields say so
        self.elf = None
        self.target = "desktop"
        self.label = os.path.basename(self.shim)
        self.sym = {}
        self.dropped = []
        self.width = PIXELS_X       # until a capture says otherwise
        self.crc_hits = self.crc_misses = 0
        self._defaultPalette = None

    # ---- lifecycle --------------------------------------------------------
    def __enter__(self):
        # unbuffered on our side; the shim flushes every reply itself
        self.proc = subprocess.Popen([self.shim], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, bufsize=0)
        self.off = self._offsets()
        # addresses are instance-relative on the wire, so the instance base is 0 and
        # `vdp` is the same arithmetic the device backend does - see VdpAccess
        self.inst = 0
        self.vdp = self.inst + self.off["vram"]
        return self

    def __exit__(self, *_):
        if self.proc:
            try:
                self._send("quit")
                self.proc.stdin.close()
                self.proc.wait(timeout=5)
            except (OSError, ValueError, subprocess.TimeoutExpired):
                self.proc.kill()
            self.proc = None

    # ---- the pipe ---------------------------------------------------------
    def _send(self, command, payload=b""):
        self.proc.stdin.write(command.encode() + b"\n" + payload)
        self.proc.stdin.flush()

    def _line(self):
        line = self.proc.stdout.readline()
        if not line:
            raise SystemExit("the desktop shim exited (%s)" % self.shim)
        return line.decode().strip()

    def _expect(self, first):
        """Read one reply and refuse anything but the word expected. The shim answers
        an out-of-range access with `error ...` rather than silently clamping, so a
        harness bug shows up here instead of as a scene that renders oddly."""
        reply = self._line()
        if not reply.startswith(first):
            raise SystemExit("desktop shim: %s" % reply)
        return reply.split()

    def _payload(self, size):
        chunks, got = [], 0
        while got < size:
            block = self.proc.stdout.read(size - got)
            if not block:
                raise SystemExit("the desktop shim closed mid-payload")
            chunks.append(block)
            got += len(block)
        return b"".join(chunks)

    def _offsets(self):
        self._send("off")
        found = {}
        while True:
            parts = self._line().split()
            if parts and parts[0] == "end":
                return found
            if len(parts) == 2:
                found[parts[0]] = int(parts[1])

    # ---- raw memory -------------------------------------------------------
    def read(self, addr, size):
        self._send("r %x %d" % (addr, size))
        self._expect("data")
        return self._payload(size)

    def write(self, addr, data):
        data = bytes(data)
        self._send("w %x %d" % (addr, len(data)), data)
        self._expect("ok")

    def _default_palette_bytes(self):
        self._send("palette")
        size = int(self._expect("data")[1])
        return self._payload(size)

    # ---- capture ----------------------------------------------------------
    def capture(self, timeout=2.0, expect=None):
        """Render one frame and return (rows, bytes), the same pair `Live.capture`
        returns. No window and no CRC: there is no link to save, so the whole frame
        comes back every time and `expect` is accepted and ignored."""
        self._send("capture")
        _, rows, width = self._expect("capture")
        rows, width = int(rows), int(width)
        self.width = width
        self.dropped = []
        return rows, self._payload(rows * width)

    def wait_frames(self, n, timeout=3.0):
        self._send("frames %d" % n)
        self._expect("ok")

    def gpu_start(self, entry):
        """Start a GPU program. The shim runs it on a thread of its own, so frames
        can be rendered while it draws - the shape the device has, where the program
        is on core 0 and the renderer on core 1."""
        self._send("gpu %x" % entry)
        self._expect("ok")

    def gpu_poll(self):
        """None while it is still running, else the microseconds it ran for.

        The number is the library's own accumulator, the same one the device reports
        from. What differs is the machine underneath it, so the two are the same
        measurement of two different processors and not comparable to each other."""
        self._send("gpupoll")
        parts = self._expect("gpu")
        return None if parts[1] == "running" else int(parts[2])

    def gpu_frames(self):
        """None: there is no display here. The shim renders when something asks it
        to, so a frame count would be a count of this harness's own requests rather
        than of anything that reached a screen."""
        return None

    def gpu_stop(self):
        self._send("gpustop")
        self._expect("ok")

    def view(self):
        """One frame as a P6 PPM, expanded through the palette the instance is
        holding. The expansion is the shim's because it is free there and would cost
        a viewer its whole frame budget here - 200,000 indices through a table, sixty
        times a second. Tk loads these bytes as an image directly."""
        self._send("view")
        size = int(self._expect("view")[1])
        return self._payload(size)

    # ---- what only a board can answer -------------------------------------
    def clock_hz(self):
        return None

    def flash(self):
        raise SystemExit("--desktop has nothing to flash")
