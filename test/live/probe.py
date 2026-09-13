#!/usr/bin/env python3
"""The host-bus probe's CDC channel, spoken directly: one ASCII line out, one JSON
line back.

    python probe.py                 what is on the port, and what it is holding
    python probe.py --arm switch    arm it, then report
    python probe.py --off           release the bus and the rail

This is the harness's own transport rather than an import of the probe repo's
`host/pico9918_probe.py`, so a checkout of that repo is not a prerequisite for
running the suite. The wire format is small enough that speaking it directly costs
less than the dependency would.

**pyserial is imported lazily.** Nothing here touches `serial` until a port is
actually opened, so the stdlib-only property the rest of the harness has holds
everywhere it held before: a run without `--probe-cdc` never reaches the import.

**The protocol version is asserted, not assumed.** Probe firmware and harness
change together - the 5 ns tick, the one-word event packing and `PULSE` all
arrived in protocol 2 - and a mismatched pair produces plausible-looking numbers
rather than an error. That is the failure this check exists to prevent.
"""

import argparse
import json
import os
import sys

# The command set this module speaks. Bump both sides together; see the note above.
PROTOCOL = 2

# HB_MAX_BYTES in the probe firmware: one READ or WRITE line carries at most this
# many bytes, hex-encoded. Longer transfers are chunked and ride the VDP's own
# address autoincrement, which is what a real host does too.
PORT_CHUNK = 256

# Ports are (MODE, MODE1). 0 and 1 are the TMS9918A's own two; 2 and 3 are the same
# pair with MODE1 low, which no TMS9918A decodes and the PICO9918 currently aliases.
PORT_DATA = 0
PORT_CONTROL = 1


class ProbeError(RuntimeError):
    pass


def spec(override=None):
    """The probe's CDC port as `(port, mode)`, or None when no bus work is wanted.

    Accepts `COM10` or `COM10:manual`; `LIVE9918_PROBE_CDC` sets it globally. Mode
    is how the target is powered: `switch` drives the fitted power transistor,
    `manual` leaves the rail to the bench supply.
    """
    value = override or os.environ.get("LIVE9918_PROBE_CDC") or None
    if not value:
        return None
    port, _, mode = value.partition(":")
    return port, (mode or "switch").lower()


class Probe:
    """One CDC session. Commands are serialized by the caller, as the firmware is."""

    def __init__(self, port, timeout=4):
        try:
            import serial
        except ImportError:
            raise SystemExit(
                "the host-bus probe needs pyserial (%s names %s). Either\n"
                "  python -m pip install \"pyserial>=3.5,<4\"\n"
                "or drop --probe-cdc and leave the bus tests out of the run." %
                ("LIVE9918_PROBE_CDC" if not port else "the port", port))
        self.port = port
        self.link = serial.Serial(port, 115200, timeout=timeout)
        # The probe's CDC task discards everything until DTR is asserted, silently.
        # An un-asserted line looks exactly like a probe running the wrong firmware.
        self.link.dtr = True
        self.link.reset_input_buffer()
        self.sequence = 0
        self.info = self.command("INFO")
        if self.info.get("protocol") != PROTOCOL:
            raise ProbeError(
                "probe on %s speaks protocol %s, this harness speaks %d - flash "
                "tools/debugprobe/build/debugprobe_pico9918.uf2" %
                (port, self.info.get("protocol"), PROTOCOL))

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        if self.link:
            self.link.close()
            self.link = None

    def send(self, *args):
        """One command, one reply, no interpretation of `ok`."""
        self.sequence += 1
        line = "%d %s\n" % (self.sequence, " ".join(str(a) for a in args))
        if len(line) >= 640:
            raise ValueError("probe command too long: %d bytes" % len(line))
        self.link.write(line.encode("ascii"))
        # A freshly opened port can still hold a partial line from whatever spoke to
        # the probe last, and asserting DTR can itself shake one loose. Skip anything
        # that is not our reply rather than failing on it, but bound the skipping so
        # a probe that only ever talks nonsense is still an error.
        for _ in range(8):
            reply = self.link.readline().decode("ascii", errors="replace").strip()
            if not reply:
                raise ProbeError("probe on %s did not answer %r - wrong firmware, or "
                                 "DTR not asserted" % (self.port, line.strip()))
            try:
                answer = json.loads(reply)
            except ValueError:
                continue
            if answer.get("id") == self.sequence:
                return answer
        raise ProbeError("probe on %s never answered request %d; last line was %r"
                         % (self.port, self.sequence, reply))

    def command(self, *args):
        answer = self.send(*args)
        if not answer.get("ok"):
            raise ProbeError("%s: %s" % (" ".join(str(a) for a in args),
                                         answer.get("error", "refused")))
        return answer

    # -- session ------------------------------------------------------------

    def state(self):
        return self.command("STATE")

    def arm(self, mode="switch"):
        """Arm if it is not already, and return what it is holding.

        Idempotent by asking first: a second ARM is refused rather than ignored,
        so sending one blind cannot tell a live fixture from a broken one.
        """
        state = self.state()
        if not state.get("armed"):
            self.command("ARM", mode.upper())
            state = self.state()
        return state

    def off(self):
        self.command("OFF")

    @property
    def tick_ns(self):
        """One PIO tick at divider 1, as the probe measures its own clock."""
        return self.info["tick_ns"]

    def ticks(self, ns):
        """Ticks for a nanosecond figure, refusing anything the grid cannot express.

        A conformance assertion at an edge the instrument cannot generate is either
        vacuous or rejects correct hardware, so this raises rather than rounding.
        """
        if ns % self.tick_ns:
            raise ValueError("%d ns is not a whole number of %d ns ticks"
                             % (ns, self.tick_ns))
        return ns // self.tick_ns

    # -- the bus, at the level a host sees it --------------------------------

    def write_port(self, port, data):
        for i in range(0, len(data), PORT_CHUNK):
            chunk = data[i:i + PORT_CHUNK]
            self.command("WRITE", port, chunk.hex())

    def read_port(self, port, count):
        out = bytearray()
        while len(out) < count:
            want = min(PORT_CHUNK, count - len(out))
            out += bytes.fromhex(self.command("READ", port, want)["data"])
        return bytes(out)

    def set_address(self, address, write=False):
        """The two-byte address latch: low byte, then high byte with the direction bit."""
        if not 0 <= address < 0x4000:
            raise ValueError("VRAM address out of range: %#x" % address)
        self.write_port(PORT_CONTROL, bytes((address & 0xff,
                                             (address >> 8) | (0x40 if write else 0))))

    def vram_write(self, address, data):
        self.set_address(address, write=True)
        self.write_port(PORT_DATA, data)

    def vram_read(self, address, count):
        self.set_address(address)
        return self.read_port(PORT_DATA, count)

    def register(self, index, value):
        self.write_port(PORT_CONTROL, bytes((value & 0xff, 0x80 | (index & 0x3f))))

    def status(self, count=1):
        """Status reads clear /INT and the sprite flags, so count is deliberate."""
        return self.read_port(PORT_CONTROL, count)


def opened(port_spec, arm=True):
    """Open and arm in one step, the way every caller wants it."""
    port, mode = port_spec
    probe = Probe(port)
    if arm:
        probe.arm(mode)
    return probe


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", default=None, metavar="PORT[:MODE]",
                    help="the probe's CDC port; otherwise LIVE9918_PROBE_CDC")
    ap.add_argument("--arm", nargs="?", const="switch", default=None,
                    choices=["switch", "manual"], help="arm the fixture")
    ap.add_argument("--off", action="store_true", help="release the bus and the rail")
    args = ap.parse_args()
    where = spec(args.port)
    if not where:
        raise SystemExit("no probe port: pass --port COM10 or set LIVE9918_PROBE_CDC")
    with Probe(where[0]) as probe:
        if args.off:
            probe.off()
        elif args.arm:
            probe.arm(args.arm)
        info, state = probe.info, probe.state()
        print("%s  protocol %d  %s" % (where[0], info["protocol"], info["firmware"]))
        print("  clock %.3f MHz, tick %d ns, SWCLK %.3f MHz"
              % (info["clock_hz"] / 1e6, info["tick_ns"], info["swclk_hz"] / 1e6))
        print("  %s, %s power" % ("armed" if state["armed"] else "idle",
                                  "switched" if state["switched"] else "manual"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
