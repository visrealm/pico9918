#!/usr/bin/env python3
"""Live test harness for the PICO9918, over a Pico debug probe.

Everything runs through SWD, so the firmware needs no command channel and no USB:

  * scene setup writes VDP registers and VRAM straight into tms9918Inst
  * readback reads liveTestCapture, which a PICO9918_LIVE_TEST build fills with
    one copy per scanline
  * flashing is openocd's job too, so a test cycle needs no hands

Usage:
    python live9918.py flash <elf>          program the board and reset it
    python live9918.py shot <out.png>       capture a frame and render it
    python live9918.py regs                 dump the VDP registers

or import it and drive a scene:

    with Live(elf) as t:
        t.unlock()
        t.reg(0x00, 0x04)                   # text 80
        t.vram(0x0800, bytes(range(80)))
        frame = t.capture()                 # (rows, 240x256 indices)
"""

import argparse
import glob
import os
import re
import socket
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

# Where the tools live. On Windows that is the Pico SDK installer's own layout,
# which is fixed and worth naming outright. Everywhere else openocd and the Arm
# toolchain come from a package manager or an unpacked tarball, so the default is
# PATH and the env vars are there for a toolchain that is not on it.
EXE = ".exe" if os.name == "nt" else ""

if os.name == "nt":
    PICO_SDK = os.path.expandvars(r"%USERPROFILE%\.pico-sdk")
    OPENOCD = os.path.join(PICO_SDK, "openocd", "0.12.0+dev", "openocd" + EXE)
    OPENOCD_SCRIPTS = os.path.join(PICO_SDK, "openocd", "0.12.0+dev", "scripts")
    TOOLCHAIN = os.path.join(PICO_SDK, "toolchain", "15_2_Rel1", "bin")
else:
    PICO_SDK = os.environ.get("PICO_SDK_PATH", os.path.expanduser("~/pico/pico-sdk"))
    OPENOCD = os.environ.get("OPENOCD", "openocd")
    OPENOCD_SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", "/usr/share/openocd/scripts")
    TOOLCHAIN = os.environ.get("ARM_TOOLCHAIN_BIN", "")


def tool(name):
    """An arm-none-eabi tool: out of TOOLCHAIN when that names a directory, off
    PATH when it does not. Callers that only want the tool if it is really there
    still check os.path.exists, so a bare name has to survive that - shutil.which
    resolves it to a path when it can and leaves it alone when it cannot."""
    exe = name + EXE
    if TOOLCHAIN:
        return os.path.join(TOOLCHAIN, exe)
    return shutil.which(exe) or exe


NM = tool("arm-none-eabi-nm")
GDB = tool("arm-none-eabi-gdb")
OBJCOPY = tool("arm-none-eabi-objcopy")

XIP_BASE = 0x10000000

# The SWD DP's identity register, which is the only thing on the wire that says
# which chip is on the probe. `--board` picks an ELF and an openocd target, not a
# probe - so nothing else stops a run reaching the other tier, and programming the
# wrong image reports success and leaves the chip unreachable until the rescue DP
# resets its power state machine (`set RESCUE 1` with target/rp2040.cfg).
# Both are DP identities, which name ARM (designer 0x477) rather than the vendor:
# the RP2350's TARGETID names Raspberry Pi (0x927) and is a different register, so
# a value ending 927 here refuses every board it is asked about.
DPIDR = {"rp2040": 0x0BC12477, "rp2350": 0x4C013477}

def tcl_port():
    """A port of this session's own for openocd's TCL RPC. Fixed at one number, two
    boards could not be driven at once: the second openocd would find the port taken
    and exit, or worse, the harness would connect to the first board's session and
    read the wrong chip. The gdb and telnet ports are disabled for the same reason -
    nothing here uses them, and they would collide the same way."""
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]

# 20 MHz takes the golden sweep from 57.5 s to 49.2 s, three runs each, with all 96
# scenes matching at both. Higher does not help, and the reason is worth keeping: a
# 12KB read costs a flat ~45 ms whatever the clock - 5, 10, 20 and 30 MHz all - so
# what is left is USB round trips to a full-speed CMSIS-DAP probe, not the wire. At
# 20 MHz the wire is already down to 7 ms of a 50 ms transfer. LIVE9918_SPEED
# overrides for a board or a probe that will not hold it.
SWD_SPEED_KHZ = int(os.environ.get("LIVE9918_SPEED", "20000"))
TCL_EOF = b"\x1a"


def probe_serial(target, override=None):
    """Which probe to open. With a board on each of two probes, openocd takes the
    first CMSIS-DAP device that answers, so which tier a run reaches is a coin toss
    that the DP check above can only catch after the fact. LIVE9918_PROBE_RP2040 and
    LIVE9918_PROBE_RP2350 name a serial each - `--probe` overrides for a one-off, and
    with neither set openocd picks as it always has."""
    return override or os.environ.get("LIVE9918_PROBE_" + target.upper()) or None

# The stored block is the top 4 KB of a 2 MB flash and the pending display block is
# the sector below it, per CONFIG_FLASH_OFFSET in src/config.c.
CONFIG_FLASH = XIP_BASE + 0x200000 - 0x1000
PENDING_FLASH = CONFIG_FLASH - 0x1000

# What each preset means, per vgaClockPresets in src/clocks.c. `clock_hz` reads
# the real number back off the board; this is here to bound the knob, since an
# index past the table's end is a system clock read out of whatever follows it.
CLOCK_PRESET_MHZ = (252, 302, 352)

# The library's half of the harness needs no board, so it is the `suite` package
# under core/test. Re-exported because every caller imports them from here.
# The path is set here rather than in the runner alone, so this module works when
# it is the one being run.
_TEST = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), "core", "test")
if _TEST not in sys.path:
    sys.path.insert(0, _TEST)

from suite.oracle import REFERENCE_DIR, golden, reference, reference_dir
from suite.access.image import png_bytes, rgb_palette, unpack_nibbles, write_png
from suite.access.vdp import (CAPTURE_ROWS, CONFIG_BYTES, PICO9918_CONF_CLOCK_PRESET_ID,
                 PICO9918_CONF_CLOCK_TESTED, PICO9918_CONF_SAVE_FORCED,
                 PICO9918_CONF_SAVE_TO_FLASH, PIXELS_X, REQUEST_CRC,
                 REQUEST_WINDOW, VRAM_PRAM, VRAM_REGISTERS, VRAM_STATUS,
                 VdpAccess)


class OpenOcd:
    """openocd's TCL RPC. Bulk transfers go through dump_image/load_image,
    which move a file rather than a list of decimal strings."""

    def __init__(self, target, speed=SWD_SPEED_KHZ, serial=None):
        self.proc = None
        self.sock = None
        self.target = target
        self.speed = speed
        self.serial = serial
        self.port = tcl_port()
        self.log = None

    def start(self):
        # openocd logs to stderr, and an unread pipe stops it dead as soon as the
        # OS buffer fills - so it goes to a file we can read back after a failure
        self.log = open(os.path.join(tempfile.gettempdir(), "openocd-live9918.log"), "wb")
        # the serial goes between the two configs: the interface one selects the
        # driver it applies to, and the target one is what opens the device
        args = [OPENOCD, "-s", OPENOCD_SCRIPTS, "-f", "interface/cmsis-dap.cfg"]
        if self.serial:
            args += ["-c", "adapter serial %s" % self.serial]
        args += ["-c", "tcl_port %d" % self.port,
                 "-c", "gdb_port disabled", "-c", "telnet_port disabled",
                 "-f", "target/%s.cfg" % self.target,
                 "-c", "adapter speed %d" % self.speed]
        self.proc = subprocess.Popen(args, stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.time() + 10
        while time.time() < deadline:
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), 1)
                self.sock.settimeout(180)   # programming takes seconds, not milliseconds
                return
            except OSError:
                if self.proc.poll() is not None:
                    raise RuntimeError("openocd exited:\n" + self.tail())
                time.sleep(0.2)
        raise RuntimeError("openocd did not open its TCL port - is the probe connected?")

    def tail(self, lines=25):
        try:
            self.log.flush()
            with open(self.log.name, "rb") as f:
                return b"".join(f.readlines()[-lines:]).decode(errors="replace")
        except OSError:
            return "(no log)"

    def dpidr(self):
        """What answered on SWD, read back out of openocd's own connect log. Free:
        it prints this before the TCL port opens, so nothing extra goes on the wire."""
        found = re.findall(r"SWD DPIDR (0x[0-9a-fA-F]+)", self.tail(400))
        return int(found[0], 16) if found else None

    def cmd(self, command):
        self.sock.sendall(command.encode() + TCL_EOF)
        out = b""
        while not out.endswith(TCL_EOF):
            try:
                chunk = self.sock.recv(65536)
            except ConnectionError:
                raise RuntimeError("openocd died on %r:" % command + self.tail())
            if not chunk:
                raise RuntimeError("openocd closed the connection on %r:" % command + self.tail())
            out += chunk
        return out[:-1].decode(errors="replace")

    def close(self):
        try:
            if self.sock:
                self.cmd("shutdown")
                self.sock.close()
        except OSError:
            pass
        if self.proc:
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if self.log:
            self.log.close()


class Live(VdpAccess):
    def __init__(self, elf, target=None, verify=True, speed=SWD_SPEED_KHZ, boot=None, probe=None):
        self.elf = elf
        # what this run was against, for anything that prints rather than asserts -
        # the desktop backend has no ELF, so nothing may reach for one
        self.label = os.path.basename(elf)
        self.verify = verify
        self.boot = boot or {}
        self.target = target or ("rp2350" if "pro" in os.path.basename(elf).lower() else "rp2040")
        self.sym = elf_symbols(elf)
        self.inst = self.sym["tms9918Inst"]
        self.capture_addr = self.sym.get("liveTestCapture")
        self.off = struct_offsets(elf)
        # The VDP address space starts at the instance's `vram` union, NOT at the
        # instance. Scalar fields sit ahead of it, so a scene written to `inst`
        # lands on lockedMask, restart, flash and vdpBase instead of on registers,
        # and the renderer draws a blank instance while the reads agree with
        # themselves. Field offsets from self.off are instance-relative; only the
        # VDP address space goes through self.vdp.
        self.vdp = self.inst + self.off["vram"]
        self.dropped = []
        self.width = PIXELS_X       # until a capture says otherwise
        self._defaultPalette = None
        self.ocd = OpenOcd(self.target, speed, probe_serial(self.target, probe))

    def __enter__(self):
        self.ocd.start()
        # a `with` never runs __exit__ for a body that failed to start, so openocd
        # outlives the run that launched it and holds the probe against the next one
        # - which then cannot identify the chip and refuses for the wrong reason
        try:
            self.check_chip()
            self.run()
            # before check_image, which reads flash: a board stuck mid-save answers
            # zeros for every flash address, and this is what gets it out
            if self.boot_config(**self.boot):
                print("rebooted at %d MHz" % (self.clock_hz() // 1000000))
            if self.verify:
                self.check_image()
        except BaseException:
            self.ocd.close()
            raise
        return self

    def __exit__(self, *_):
        self.ocd.close()

    # ---- raw memory -------------------------------------------------------
    def read(self, addr, size):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "r.bin").replace("\\", "/")
            self.ocd.cmd("dump_image %s 0x%08x %d" % (path, addr, size))
            with open(path, "rb") as f:
                return f.read()

    def write(self, addr, data):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "w.bin").replace("\\", "/")
            with open(path, "wb") as f:
                f.write(data)
            self.ocd.cmd("load_image %s 0x%08x bin" % (path.replace("\\", "/"), addr))

    def _default_palette_bytes(self):
        """Off the running image, so it cannot drift from what the board booted."""
        return self.read(self.sym["defaultPalette"], 64 * 2)

    # ---- capture ----------------------------------------------------------
    def capture(self, timeout=2.0, expect=None):
        """Arm a capture and return (rows, bytes) once the firmware has a whole
        frame. A 48KB SWD read is slower than a frame, so the firmware holds the
        buffer still rather than the reader trying to catch it between frames.

        A build that cannot spare a whole frame captures a window at a time; this
        walks the windows and stitches them. The scene is static and the GPU is
        off, so the passes reassemble the image one pass would have given - and
        each row's `seen` bit is taken from the pass that captured that row, not
        from whichever pass happened to run last.

        `self.dropped` is left holding the rows the renderer never got to, which
        is not the same as rows it drew badly: a line that overruns is skipped
        outright (vga.c:600) and its pixels here are the previous capture's. Read
        it before trusting any row.

        `expect` is what this capture is about to be compared against, which for
        every caller that has one is a golden. The DMA sniffer CRCs each window in
        hardware as the copy runs, so a window whose CRC matches is taken from
        `expect` rather than read: four bytes off the board instead of twelve
        kilobytes, over a link that moves 258 KB/s and is the sweep's whole cost.
        A window that differs is read in full, so a regression is still reported
        byte for byte. `self.crc_hits` counts what was taken on the CRC's word."""
        if self.capture_addr is None:
            raise RuntimeError("this firmware was not built with PICO9918_LIVE_TEST=ON")
        self.crc_hits = self.crc_misses = 0

        # One armed frame for the whole picture, before spending one per window: the
        # firmware runs the copy on every row in this mode so the sniffer sees them
        # all, and a scene that matches is finished here.
        if expect is not None:
            self._arm(REQUEST_CRC, 0)
            rows, width, _, crc = self._await(timeout)
            if len(expect) == rows * width and zlib.crc32(expect) == crc:
                self.width = width
                self.crc_hits = 1
                self.dropped = [y for y in range(rows) if y not in self._seen]
                return rows, expect
            self.crc_misses = 1

        pixels, seen_rows, start, rows = bytearray(), set(), 0, None
        while rows is None or start < rows:
            got, rows, window = self._one(timeout, start, expect)
            pixels += got
            seen_rows |= {y for y in range(start, min(start + window, rows)) if y in self._seen}
            start += window
        self.dropped = [y for y in range(rows) if y not in seen_rows]
        return rows, bytes(pixels[:rows * self.width])

    def frame_count(self):
        """Frames the firmware has rendered since boot. It ticks at the top of every
        frame whether or not a capture is armed, so it is the display's own clock."""
        return struct.unpack("<I", self.read(
            self.capture_addr + self.off["capture.frame"], 4))[0]

    def wait_frames(self, n, timeout=3.0):
        """Wait for n rendered frames. The diagnostic accumulators reset every four
        and the strings are rewritten from them, so a reading taken sooner is partly
        the scene before it: measured, the value is the new scene's by the eighth
        frame. Sleeping most of the way and then checking costs one or two reads
        rather than a poll a frame."""
        start = self.frame_count()
        time.sleep(n / 60.0)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.frame_count() - start >= n:
                return
            time.sleep(0.005)
        raise RuntimeError("only %d frames passed in %.1f s - is the display running?"
                           % (self.frame_count() - start, timeout))

    def gpu_start(self, entry):
        """Point the GPU at `entry` and start it.

        Core 0 is already sitting in pico9918_gpu_loop, so this is a store to `restart`
        and nothing else has to be arranged - the program then runs alongside the
        renderer, exactly as one loaded by a host would, and the drawing appears a
        frame at a time while it works."""
        self._gpuQuiet = 0
        self.write(self.sym["gpuTimeUs"], b"\x00\x00\x00\x00")
        self._gpuFrame = self.frame_count()
        self.write(self.inst + self.off["gpuAddress"], struct.pack("<H", entry))
        self.write(self.inst + self.off["restart"], b"\x01")

    def gpu_poll(self):
        """None while it is still running, else the microseconds it ran for.

        The microseconds are the FIRMWARE's, read out of the accumulator the
        diagnostics overlay reports from. A stopwatch on this side would measure the
        probe and the polling instead, and on a four-second program that is the wrong
        two decimal places.

        Done is `restart` clear and the Running status bit down, seen twice running:
        the loop adds to the accumulator *after* the program stops, so one quiet poll
        can arrive between the two and read the time before it is there."""
        busy = (self.read(self.inst + self.off["restart"], 1)[0]
                or self.read(self.vdp + VRAM_STATUS + 2, 1)[0] & 0x80)
        self._gpuQuiet = 0 if busy else self._gpuQuiet + 1
        if self._gpuQuiet < 2:
            return None
        return struct.unpack("<I", self.read(self.sym["gpuTimeUs"], 4))[0]

    def gpu_frames(self):
        """Frames the display has drawn since the program started.

        A second instrument on the same run, and the only one that compares with a
        capture card: it counts what reached the glass. It does not agree with the
        microseconds and should not - the accumulator measures trigger to IDLE, while
        frames are quantised to 1/60 s and a capture card measures FIRST PIXEL to
        last, which starts after the program has set its registers and cleared its
        tables."""
        return self.frame_count() - self._gpuFrame

    def gpu_stop(self):
        """Stop a program where it stands. run9900 tests this byte every
        instruction, so it is the same switch the program itself uses to finish."""
        self.reg(0x38, 0)

    def _arm(self, request, start):
        self.write(self.capture_addr + self.off["capture.start"], struct.pack("<I", start))
        self.write(self.capture_addr, struct.pack("<I", request))

    def _await(self, timeout):
        """Wait for the firmware to clear the flag, then take everything the header
        holds. `seen` comes with it, so a whole-frame CRC still knows which rows the
        renderer reached."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if struct.unpack("<I", self.read(self.capture_addr, 4))[0] == 0:
                head = self.read(self.capture_addr + self.off["capture.rows"],
                                 self.off["capture.seen"] - self.off["capture.rows"])
                # the CRC rides in the header this already reads, so comparing costs
                # nothing even when it does not save anything
                rows, width, window, _, crc = struct.unpack_from("<IIIII", head)
                raw = self.read(self.capture_addr + self.off["capture.seen"],
                                self.off["capture.pixels"] - self.off["capture.seen"])
                words = struct.unpack("<%dI" % (len(raw) // 4), raw)
                self._seen = {y for y in range(rows) if (words[y >> 5] >> (y & 31)) & 1}
                return rows, width, window, crc
            time.sleep(0.01)
        raise RuntimeError("capture never completed - is the display running?")

    def _one(self, timeout, start, expect=None):
        """One armed pass: returns this window's pixels, the frame's height and
        the window size the firmware reports rather than one the harness assumed."""
        self._arm(REQUEST_WINDOW, start)
        rows, width, window, crc = self._await(timeout)
        # the mode decides how wide a line is, so the firmware says rather than the
        # harness assuming. It is also what a reference is keyed on: 8bpp 80-column
        # text renders 512 bytes a line where every other mode renders 256
        self.width = width
        # the last window is short: the firmware only filled rows - start of it,
        # and reading the whole window would append the previous pass's leftovers
        take = min(window, max(0, rows - start))
        if expect is not None and len(expect) == rows * width:
            want = expect[start * width:(start + take) * width]
            if zlib.crc32(want) == crc:
                self.crc_hits += 1
                return want, rows, window
            self.crc_misses += 1
        return (self.read(self.capture_addr + self.off["capture.pixels"], take * width),
                rows, window)

    def clock_hz(self):
        """The system clock every timing here is measured against, resolved the
        way clocks.c does it: a preset index into whichever table the board picked
        at boot. 252 MHz is the floor the firmware offers and the budget assumes,
        but a user can clock up - so a run has to record which, or two sets of
        numbers can differ by a third and look like a regression.

        `ClockSettings` is five ints and `clockHz` is the last of them; the result
        is range-checked rather than trusted, because both symbols are file-static
        and a build that inlined them away would otherwise yield a plausible-looking
        number from whatever is at that address."""
        try:
            table = struct.unpack("<I", self.read(self.sym["clockPresets"], 4))[0]
            index = struct.unpack("<i", self.read(self.sym["clockPresetIndex"], 4))[0]
            hz = struct.unpack("<I", self.read(table + index * 20 + 16, 4))[0]
        except (KeyError, struct.error, RuntimeError):
            return None
        return hz if 100_000_000 <= hz <= 600_000_000 else None

    def flash(self):
        # openocd answers a failed `program` with the error text rather than by
        # closing the connection, so ignoring the reply reports a flash that never
        # happened - and the next stage then measures whatever is still on the board
        failed = self.ocd.cmd("program %s verify reset" % self.elf.replace("\\", "/"))
        if failed:
            raise SystemExit("programming %s failed:\n%s" % (self.elf, failed))
        self.run()

    def check_chip(self):
        """Refuse to touch a board of the other tier, before anything is written.

        Changing which board is on the probe takes a person, so a run aimed at the
        tier that is not plugged in is always a mistake - and left to itself a
        silent one: the ELF's name picks the openocd target, `program` answers
        that it worked, and the board is then unreachable. Two lines of the connect
        log say which chip replied, because the two target configs fail
        differently: the matching one prints a DP identity, and the other reads a
        CPUID of zero off a core that is not there. Neither means yes, so neither
        may pass."""
        log = self.ocd.tail(400)
        seen = self.ocd.dpidr()
        if seen == DPIDR[self.target] and "is unrecognized" not in log:
            return
        if seen is None and "is unrecognized" not in log:
            raise SystemExit("nothing identified itself on SWD - the probe reported no DP. Check "
                             "it is plugged in and that no other openocd is holding it")
        here = next((t for t, v in DPIDR.items() if v == seen), None)
        advice = ("Probe %s is the other tier's" % self.ocd.serial if self.ocd.serial else
                  "Name the probe for each tier (--probe, or LIVE9918_PROBE_RP2040 / "
                  "LIVE9918_PROBE_RP2350) - openocd otherwise opens whichever answers first")
        raise SystemExit("%s is a %s image and the board on the probe is %s. %s"
                         % (os.path.basename(self.elf), self.target,
                            here or "not one (DP identity 0x%08x)" % seen if seen
                            else "not answering as one", advice))

    def check_image(self):
        """Refuse to read a board running something other than this ELF.

        Everything here is addressed by symbol, and the symbols come from the ELF
        rather than from the board - so a stale image does not fail, it reads the
        wrong addresses and answers. A build that only resized `.bss` cost a full
        suite this way: the capture struct happened to keep its address, so all 90
        goldens passed, and the diagnostic strings behind it came back as pixels,
        which `float("")` turns into a clean 0.00 us for every scene in the run."""
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "image.bin")
            subprocess.run([OBJCOPY, "-O", "binary", self.elf, path], check=True)
            with open(path, "rb") as f:
                image = f.read()
        # boot2 at the front is the same in every build; a block past it is not,
        # because a RAM section that changes size moves every literal after it
        for at in (0x1000, len(image) // 2 & ~0xFFF):
            want = image[at:at + 0x400]
            if want and self.read(XIP_BASE + at, len(want)) != want:
                raise SystemExit(
                    "the board is not running %s - flash it (`--flash`, or "
                    "`live9918.py flash <elf>`) before measuring anything"
                    % os.path.basename(self.elf))

    def run(self):
        """A halted core stays halted across an openocd session and across a
        flash, so anything that reads a live buffer has to say so rather than
        assume - one interrupted session otherwise looks exactly like firmware
        that no longer renders."""
        if "halted" in self.ocd.cmd("targets"):
            self.ocd.cmd("reset run")

    def boot_config(self, clock=None, clock_tested=None):
        """Turn configuration the renderer never re-reads, and reboot into it.

        The system clock is the one knob here that is neither a scene nor a
        register: clocks.c applies it once, out of flash, before the renderer
        starts. Writing the running copy changes nothing, so the stored block has
        to change - and since the clock is the denominator of every timing the rig
        records, a run at another preset is a different measurement rather than a
        comparable one. `clock_hz` reads back which.

        The sector is written from here with both cores halted, rather than by
        asking the firmware to save its own config. That route exists -
        `PICO9918_CONF_SAVE_FORCED`, which the GPU core polls for - but it erases and
        programs flash while core 0 renders out of it, and with a debugger reading
        XIP at the same time the board stops after one frame and answers zeros for
        every flash address until the sector is rewritten from outside. Halted,
        there is nothing to race.

        The pending block goes too. It is the configurator's "try this display mode
        and let a person confirm it", it outranks the main block at boot, and
        erasing it is the state that means nothing is pending - so a clock set here
        cannot be overridden by a half-finished change from a previous run."""
        want = {PICO9918_CONF_CLOCK_PRESET_ID: clock, PICO9918_CONF_CLOCK_TESTED: clock_tested}
        want = {i: v for i, v in want.items() if v is not None}
        if not want:
            return False

        # read it running, like check_image does: only the erase needs the cores
        # stopped, and `resume` would bring back the one openocd has selected
        # rather than the two `halt` took - which stops the display
        block = bytearray(self.read(CONFIG_FLASH, CONFIG_BYTES))
        # a blank or unreadable sector is the state a board hangs in, and the
        # running copy is the way out of it: readConfig builds a valid block in RAM
        # before handing it to the save that does not finish, so storing that is
        # both the repair and what the firmware was trying to do
        stored = bytes(block[:4]) not in (bytes(4), b"\xff" * 4)
        if not stored:
            block = bytearray(self.read(self.inst + self.off["config"], CONFIG_BYTES))
        if stored and all(block[i] == v for i, v in want.items()):
            return False
        for index, value in want.items():
            block[index] = value
        # the command bytes are actions rather than settings, and one of them stored
        # set is a save on every boot
        block[PICO9918_CONF_SAVE_FORCED] = block[PICO9918_CONF_SAVE_TO_FLASH] = 0

        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "config.bin").replace("\\", "/")
            with open(path, "wb") as f:
                f.write(block)
            self.ocd.cmd("halt")
            for command in ("flash erase_address 0x%08x 0x1000" % PENDING_FLASH,
                            "flash erase_address 0x%08x 0x1000" % CONFIG_FLASH,
                            "flash write_image %s 0x%08x bin" % (path, CONFIG_FLASH)):
                failed = self.ocd.cmd(command)
                if "rror" in failed:
                    raise SystemExit("could not write the stored configuration:\n" + failed)

        self.ocd.cmd("reset run")
        self.wait_frames(2, timeout=10.0)
        return True


def elf_symbols(elf):
    out = subprocess.run([NM, elf], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


TMS_FIELDS = ("isUnlocked", "lockedMask", "palDirty", "config", "configDirty", "vram",
              "gpuAddress", "restart")
CAPTURE_FIELDS = ("request", "frame", "rows", "width", "window", "start", "crc", "seen",
                  "pixels", "skipped", "skippedRows", "lineTimes")

# Fields whose SIZE is wanted too, because nothing else in the struct gives it away:
# lineTimes is the last member, so there is no successor offset to subtract from. The
# answer comes back as "capture.<field>.size".
CAPTURE_SIZES = ("lineTimes",)


def struct_offsets(elf):
    """Ask gdb for the field offsets we poke, rather than hardcoding a layout
    that PICO9918_MODE - or a new capture field - can change underneath
    us. Capture fields come back prefixed, so the two structs cannot collide.

    A field the build does not have is left out rather than being fatal, so a
    firmware that predates one can still be driven for everything that does not
    need it - perf reads two formatted strings and no capture field at all.
    Asking for an absent one is then a KeyError naming the field, raised where
    something actually wanted it. Each answer is fenced by its own marker,
    because gdb numbers only the prints that succeed."""
    addr = "&((%s)0)->%s"
    size = "sizeof(((%s)0)->%s)"
    fields = [(f, "struct pico9918_s *", f, addr) for f in TMS_FIELDS]
    fields += [("capture." + f, "LiveTestCapture *", f, addr) for f in CAPTURE_FIELDS]
    fields += [("capture." + f + ".size", "LiveTestCapture *", f, size) for f in CAPTURE_SIZES]
    args = [GDB, "-batch", "-nx", elf]
    for name, cast, field, expr in fields:
        args += ["-ex", "echo @@%s\\n" % name,
                 "-ex", "print/d (int)" + expr % (cast, field)]
    out = subprocess.run(args, capture_output=True, text=True).stdout
    offsets = {}
    for chunk in out.split("@@")[1:]:
        name, _, rest = chunk.partition("\n")
        match = re.search(r"=\s*(\d+)", rest)
        if match:
            offsets[name.strip()] = int(match.group(1))
    if not any(f in offsets for f in TMS_FIELDS):
        raise RuntimeError("could not read any VDP struct offset from %s:\n%s" % (elf, out))
    return offsets


def board_args(ap):
    """Every runner here picks a board the same way, so it is written once. The
    openocd target comes from the ELF's name rather than from --board, which is
    why the two have to move together."""
    ap.add_argument("--board", default="pro", choices=("pro", "2040"))
    ap.add_argument("--desktop", action="store_true",
                    help="run against the in-process library instead of a board - the "
                         "stages that compare pixels, not the ones that measure time")
    ap.add_argument("--elf", default=None)
    ap.add_argument("--probe", default=None, metavar="SERIAL",
                    help="CMSIS-DAP serial to open, for a desk with a probe per board; "
                         "otherwise LIVE9918_PROBE_RP2040 / LIVE9918_PROBE_RP2350")
    ap.add_argument("--clock", type=int, default=None, metavar="N",
                    choices=range(len(CLOCK_PRESET_MHZ)),
                    help="system clock preset, %s - saved and the board rebooted into it, because "
                         "clocks.c reads it once at boot"
                         % ", ".join("%d = %d MHz" % (i, m)
                                     for i, m in enumerate(CLOCK_PRESET_MHZ)))
    ap.add_argument("--clock-tested", type=int, default=None, metavar="N",
                    help="PICO9918_CONF_CLOCK_TESTED, saved the same way")
    return ap


def open_board(args):
    if getattr(args, "desktop", False):
        # imported here rather than at the top: the desktop backend imports this
        # module for the shared VDP access, so a top-level import would be a cycle
        from suite.access.desktop import Desktop
        return Desktop()
    # a caller about to flash is not reading a stale board, it is replacing one
    boot = {k: getattr(args, k, None) for k in ("clock", "clock_tested")}
    return Live(args.elf or default_elf(args.board),
                verify=not getattr(args, "flash", False),
                boot={k: v for k, v in boot.items() if v is not None},
                probe=getattr(args, "probe", None))


def default_elf(board="pro"):
    """newest live-test build for the given board, so tests need no path

    The directory is what says this is a live build, not the filename: the binary
    name carries the BRANCH, so `*live.elf` only ever matched on a branch called
    `live` and every other branch got "no live-test build found" beside a build that
    was sitting right there."""
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    pattern = os.path.join(root, "build-live", "pico9918" + ("pro" if board == "pro" else ""),
                           "src", "*.elf")
    found = sorted(glob.glob(pattern), key=os.path.getmtime)
    if not found:
        raise SystemExit("no live-test build found (configure with -DPICO9918_LIVE_TEST=ON): "
                         + pattern)
    return found[-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=("flash", "shot", "regs"))
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--elf", default=None)
    ap.add_argument("--board", default="pro", choices=("pro", "2040"))
    ap.add_argument("--target", default=None, choices=("rp2040", "rp2350"))
    args = ap.parse_args()

    elf = args.elf or (args.arg if args.action == "flash" else None) or default_elf(args.board)

    with Live(elf, args.target, verify=args.action != "flash") as t:
        if args.action == "flash":
            t.flash()
            print("programmed", elf)
        elif args.action == "shot":
            out = args.arg or "shot.png"
            print("captured %d rows -> %s" % (t.png(out), out))
        elif args.action == "regs":
            for i, v in enumerate(t.regs()):
                if v:
                    print("R%-3d 0x%02x" % (i, v))


if __name__ == "__main__":
    main()
