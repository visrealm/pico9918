#!/usr/bin/env python3
"""What the host bus does, asserted through the bus rather than over SWD.

    python hostbus.py --port COM10              every group
    python hostbus.py --port COM10 --only glitch
    python hostbus.py --port COM10 --list

Everything here is one of two things, and they want opposite treatment.

**Conformance** asks whether the firmware honours the TMS9918A envelope from
`HARDWARE.md`. Every parameter is a fixed number from the datasheet, the answers
are pass/fail, and they must never regress.

**Capability** asks how far past the envelope it goes. Those are numbers that
move; they belong in the record beside the render timings and they do not fail a
run. The datasheet's minimum inter-access time is 8 us and this board sustains
under one, so a capability number regressing is a metric moving, not a defect.

This module is the first half. It is destructive to VRAM and to the register file,
which is why the runner puts it first: `d4` blanks both itself rather than
inheriting them.

**Why a phantom-access check is the cheapest test here.** The defect that produced
this fixture was a read that fired twice, and it was invisible to every test the
suite had: each individual byte was a legal byte, just the wrong one. Reading a
16 KiB non-repeating pattern back through the bus turns that into a byte index.
"""

import argparse
import json
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe as probe_module
from probe import PORT_CONTROL, PORT_DATA, Probe, ProbeError, spec

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))), "core", "test"))
import suite.outcome as outcome

VRAM_SIZE = 0x4000

# Where the scratch patterns go. Low VRAM, so a board showing its diagnostic
# overlay still shows something recognisable while these run.
SCRATCH = 0x1000


def pattern(length, seed=9918):
    """A locally non-repeating pattern: a single inserted or dropped byte shifts
    everything after it, which is what makes a phantom access a byte index rather
    than a plausible-looking value."""
    rng = random.Random(seed)
    return bytes(rng.randrange(256) for _ in range(length))


# -- Group A: signals and wiring -------------------------------------------


def walking(p, failures, notes):
    """Walking ones and zeros on CD0-7: no stuck-at, no bridged data line."""
    values = bytes([1 << i for i in range(8)] + [0xff ^ (1 << i) for i in range(8)])
    p.vram_write(SCRATCH, values)
    got = p.vram_read(SCRATCH, len(values))
    for i, (want, have) in enumerate(zip(values, got)):
        if want != have:
            bad = want ^ have
            failures.append("walking bit %d: wrote %#04x, read %#04x, lines %s differ"
                            % (i, want, have, [b for b in range(8) if bad >> b & 1]))
    notes.append("walking ones and zeros: %d patterns" % len(values))
    return len(values)


def address_lines(p, failures, notes):
    """Each of the 14 address bits is independent.

    One distinct byte per power of two plus address zero. A bridged pair writes
    both members of the pair, so the earlier of the two reads back as the later.
    """
    places = [0] + [1 << b for b in range(14)]
    for i, address in enumerate(places):
        p.vram_write(address, bytes([0x40 + i]))
    for i, address in enumerate(places):
        got = p.vram_read(address, 1)[0]
        if got != 0x40 + i:
            failures.append("address %#06x read %#04x, expected %#04x - address lines "
                            "bridged or aliased" % (address, got, 0x40 + i))
    notes.append("address-line separation: %d addresses" % len(places))
    return len(places)


def wrap(p, failures, notes):
    """The pointer wraps at 0x3FFF rather than running off the end."""
    p.vram_write(0x3FFE, bytes((0x11, 0x22, 0x33)))
    edge = p.vram_read(0x3FFE, 2)
    wrapped = p.vram_read(0x0000, 1)
    if edge != bytes((0x11, 0x22)):
        failures.append("0x3FFE..0x3FFF read %s, expected 1122" % edge.hex())
    if wrapped != bytes((0x33,)):
        failures.append("write past 0x3FFF landed at %s, not 0x0000 (read %s)"
                        % ("nowhere", wrapped.hex()))
    notes.append("16 KiB wrap at 0x3FFF")
    return 2


# -- Group B: protocol correctness -----------------------------------------


def phantom(p, failures, notes):
    """N writes advance the address by exactly N, and N reads by exactly N.

    The whole 16 KiB in one pass each way. This is the regression test for the
    defect that produced the fixture, and it is the cheapest one here.
    """
    want = pattern(VRAM_SIZE)
    p.vram_write(0, want)
    got = p.vram_read(0, VRAM_SIZE)
    if got != want:
        first = next(i for i in range(VRAM_SIZE) if got[i] != want[i])
        # A phantom access shifts the rest rather than corrupting one byte, so say
        # which it was: a shift is a counted-accesses defect, a single wrong byte
        # is a data-path one.
        shifted = got[first:first + 32] == want[first + 1:first + 33]
        failures.append("16 KiB round trip differs at %#06x: wrote %#04x, read %#04x%s"
                        % (first, want[first], got[first],
                           "; the rest is shifted by one, so an access was counted "
                           "twice" if shifted else ""))
    notes.append("16 KiB write and read back, byte for byte")
    return VRAM_SIZE


def readahead(p, failures, notes):
    """The first read after an address write returns the prefetched byte, and a
    rewritten address discards the prefetch rather than returning it."""
    checks = 0
    p.vram_write(SCRATCH, bytes((0xc1, 0xc2, 0xc3, 0xc4)))
    p.vram_write(SCRATCH + 0x100, bytes((0xd1, 0xd2)))

    got = p.vram_read(SCRATCH, 2)
    if got != bytes((0xc1, 0xc2)):
        failures.append("read-ahead: first two bytes at SCRATCH read %s, expected c1c2"
                        % got.hex())
    checks += 1

    # Prefetch from one address, then rewrite the address before reading. The
    # prefetched byte must be discarded.
    p.set_address(SCRATCH)
    p.set_address(SCRATCH + 0x100)
    got = p.read_port(PORT_DATA, 1)
    if got != bytes((0xd1,)):
        failures.append("read-ahead: address rewritten between prefetch and read "
                        "returned %s, expected d1 - the stale prefetch was served"
                        % got.hex())
    checks += 1
    notes.append("read-ahead: prefetch served, stale prefetch discarded")
    return checks


def register_vs_address(p, failures, notes):
    """Bit 7 of the second control byte picks register or address, and a register
    write leaves the VRAM pointer alone."""
    p.vram_write(SCRATCH, bytes((0xe1, 0xe2, 0xe3)))
    p.set_address(SCRATCH)
    first = p.read_port(PORT_DATA, 1)
    p.register(7, 0x01)                      # backdrop colour, harmless
    second = p.read_port(PORT_DATA, 1)
    if first != bytes((0xe1,)) or second != bytes((0xe2,)):
        failures.append("register write disturbed the VRAM pointer: read %s then %s, "
                        "expected e1 then e2" % (first.hex(), second.hex()))
    notes.append("register write does not move the VRAM pointer")
    return 2


def latch_matrix(p, failures, notes):
    """Interrupting the two-byte control latch, against the F18A's own rule.

    The address latch is two bytes and a host can put almost anything between them.
    The rule is not "a status read resets it": real hardware clears the hi/lo
    flip-flop on **every** access that is not the latch itself - a data read, a data
    write, a status read and reset - and leaves it alone otherwise. The F18A's VHDL
    clears `addr_ff` at exactly those four points, and this asserts all four rather
    than the one that is easy to remember.

    It matters because the shapes are real host patterns. A host that polls status
    mid-latch, or that is interrupted into a data access mid-latch, gets a *different
    address* from one that is not, and a VDP that only reset on status reads would
    quietly diverge for the other two.
    """
    checks = 0
    p.vram_write(SCRATCH, bytes((0xf1, 0xf2)))
    p.vram_write(SCRATCH + 0x80, bytes((0x91, 0x92)))
    target = SCRATCH + 0x80

    def half(byte):
        p.write_port(PORT_CONTROL, bytes((byte,)))

    def case(name, interrupt, expected, why):
        # Every case is the same shape: point at SCRATCH, write the low half of an
        # address pointing at `target`, do something, write the high half, read. The
        # byte that comes back says whether the latch survived.
        p.set_address(SCRATCH, write=(interrupt == "write"))
        half(target & 0xff)
        if interrupt == "status":
            p.status()
        elif interrupt == "read":
            p.read_port(PORT_DATA, 1)
        elif interrupt == "write":
            p.write_port(PORT_DATA, bytes((0x5a,)))
        half(target >> 8)
        got = p.read_port(PORT_DATA, 1)
        if got != bytes((expected,)):
            failures.append("latch interrupted by %s: read %s, expected %02x - %s"
                            % (name, got.hex(), expected, why))
        return 1

    # Uninterrupted, the latch completes and the read comes from `target`.
    checks += case("nothing", None, 0x91, "the latch did not complete")
    # A status read clears the flip-flop, so the high half becomes a fresh low half
    # and the pointer stays where the prefetch left it.
    checks += case("a status read", "status", 0xf1, "the flip-flop survived a status read")
    # A data read clears it too, and leaves its own prefetch behind: SCRATCH+1.
    checks += case("a data read", "read", 0xf2, "the flip-flop survived a data read")
    # So does a data write, which additionally loads the written byte into the
    # read-ahead buffer - so that byte, not VRAM, is what the next read returns.
    checks += case("a data write", "write", 0x5a, "the flip-flop survived a data write")

    notes.append("control-latch interruption: 4 shapes, all matching the F18A's flip-flop")
    return checks


def status_pointer(p, failures, notes):
    """A status read does not disturb the VRAM pointer."""
    p.vram_write(SCRATCH, bytes((0xa1, 0xa2, 0xa3)))
    p.set_address(SCRATCH)
    first = p.read_port(PORT_DATA, 1)
    p.status()
    second = p.read_port(PORT_DATA, 1)
    if first != bytes((0xa1,)) or second != bytes((0xa2,)):
        failures.append("status read moved the VRAM pointer: read %s then %s, expected "
                        "a1 then a2" % (first.hex(), second.hex()))
    notes.append("status read leaves the VRAM pointer alone")
    return 2


# -- Group E: what the firmware must reject -------------------------------

# Where the glitch sweeps work, clear of the patterns above.
GLITCH = 0x2000
COUNTER = 0x2800

# The event sequencer's own program is five ticks, so it cannot describe an event
# shorter than that. `PULSE` can, down to one tick, but only for an excursion
# against an idle bus.
EVENT_FLOOR_TICKS = 5

# Each confirm is `nop [7]` plus a re-read, which is 9 of the target's own 252 MHz
# cycles. Two of the three sit behind a 3-cycle polling loop, so where the edge
# lands inside that loop adds 0-2 cycles and the threshold is a range; the read's
# release path is entered by a `wait`, which has no phase, so it is a single number.
CONFIRM_NS = 35.7
CONFIRM_SPREAD_NS = 43.7
RELEASE_NS = 39.7


def measure(rows):
    """`strict` and `p50` from a pass-rate curve.

    A boundary is a distribution, not a number: the probe's tick and the target's
    are incommensurate, so near the edge a given width lands sometimes. `strict` is
    the coarse, stable answer - the widest fully rejected and the narrowest always
    taken - and `p50` is the interpolated half-way point, which is the only one of
    the three fine enough to show a small change moving.
    """
    rejected = [r["ns"] for r in rows if r["hits"] == 0]
    always = [r["ns"] for r in rows if r["hits"] == r["trials"]]
    out = {"rejected_below": max(rejected) if rejected else None,
           "taken_from": min(always) if always else None, "p50": None,
           "clean_from": None, "degrades_at": None}
    lossy = [r["ns"] for r in rows if r["hits"] != r["trials"]]
    if lossy:
        out["degrades_at"] = max(lossy)
    # The smallest point from which this and every point above it is unanimous.
    # `taken_from` is the lowest unanimous point anywhere, which on a curve that
    # dips back down again - and a sustained-rate curve does - says less than it
    # appears to.
    for row in reversed(rows):
        if row["hits"] != row["trials"]:
            break
        out["clean_from"] = row["ns"]
    for a, b in zip(rows, rows[1:]):
        ra, rb = a["hits"] / a["trials"], b["hits"] / b["trials"]
        if ra < 0.5 <= rb:
            out["p50"] = round(a["ns"] + (b["ns"] - a["ns"]) * (0.5 - ra) / (rb - ra), 1)
            break
    return out


def curve(rows):
    return ", ".join("%gns:%d/%d" % (r["ns"], r["hits"], r["trials"]) for r in rows)


def events(p, steps):
    """One event sequence, as `(pins, ticks)` pairs. Durations are absolute ticks."""
    words = []
    for pins, ticks in steps:
        if ticks < EVENT_FLOOR_TICKS:
            raise ValueError("event of %d ticks; the sequencer floor is %d"
                             % (ticks, EVENT_FLOOR_TICKS))
        words.append("%08x" % (pins | ((ticks - EVENT_FLOOR_TICKS) << 20)))
    return p.command("EVENTS", 1, "".join(words))


# Pin words. Bit 8 is /CSR, 9 is /CSW, 10 is MODE, 11 is MODE1; MODE1 stays high
# because that is what a TMS9918A socket presents, and MODE stays low so every
# sequence below is a data-port access.
IDLE = 0x300 | (1 << 11)
CSR_LOW = 0x200 | (1 << 11)
CSW_LOW = 0x100 | (1 << 11)
MODE_IDLE = IDLE | (1 << 10)
MODE_CSW_LOW = CSW_LOW | (1 << 10)
DRIVEN = 0xFF << 12


def glitch_write_start(p, failures, notes, trials=48, per_run=24):
    """A LOW excursion on /CSW that is too short to be a cycle must not become one.

    This is the write-side twin of the defect that produced the fixture, and it is
    a *rejection* test: the whole class was untested, because every test the suite
    had asks what the firmware accepts.

    The count is the measurement. A write advances the address, so N excursions all
    rejected leave VRAM untouched and N all taken leave exactly N bytes written -
    which turns a rare event into an offset rather than a coin toss. Two dozen
    excursions fit in one event sequence, so a width costs two commands rather than
    fifty.

    TRAP: PULSE reaches below the sequencer's 25 ns floor, but handing the pins
    between two PIOs glitches the bus and writes whatever byte the previous command
    left behind. Only worth it if the threshold ever drops under 25 ns again.
    """
    rows, data = [], 0xA5
    for ticks in range(EVENT_FLOOR_TICKS, 17):
        p.vram_write(GLITCH, bytes(trials + 16))
        p.set_address(GLITCH, write=True)
        for _ in range(trials // per_run):
            seq = [(IDLE, 32)]
            for _ in range(per_run):
                # 500 ns between excursions. Packing them tighter than the write
                # path's own sustained floor makes the target drop some of the ones
                # it accepted, which reads exactly like a rejection and is not one.
                seq += [(IDLE | DRIVEN | data, 100), (CSW_LOW | DRIVEN | data, ticks)]
            events(p, seq + [(IDLE | DRIVEN | data, 260), (IDLE, 260)])
        landed = p.vram_read(GLITCH, trials + 16)
        stray = sum(1 for b in landed if b not in (0, data))
        rows.append({"ns": ticks * p.tick_ns, "trials": trials,
                     "hits": sum(1 for b in landed if b == data)})
        if stray:
            failures.append("/CSW LOW excursion at %g ns wrote %d bytes that were "
                            "neither 0 nor %#04x" % (rows[-1]["ns"], stray, data))
    notes.append("/CSW LOW excursion, must not start a write: " + curve(rows))
    return rows


def glitch_write_end(p, failures, notes, trials=24):
    """A HIGH excursion inside an asserted /CSW must not commit the write early.

    The mirror of the read case, on the edge that *is* defended. Below the confirm
    window the release reads as a bounce and the whole thing is one write; above it
    the write commits and the second assertion starts a second one. So the byte
    count is the answer: one or two.
    """
    rows, data = [], 0x5A
    for ticks in range(EVENT_FLOOR_TICKS, 17):
        p.vram_write(GLITCH, bytes(8))
        p.set_address(GLITCH, write=True)
        twice = 0
        for _ in range(trials):
            p.vram_write(GLITCH, bytes(8))
            p.set_address(GLITCH, write=True)
            events(p, [(IDLE, 32), (IDLE | DRIVEN | data, 32),
                       (CSW_LOW | DRIVEN | data, 40), (IDLE | DRIVEN | data, ticks),
                       (CSW_LOW | DRIVEN | data, 40), (IDLE | DRIVEN | data, 260),
                       (IDLE | data, 260)])
            landed = p.vram_read(GLITCH, 8)
            written = sum(1 for b in landed if b == data)
            if written not in (1, 2):
                failures.append("/CSW HIGH excursion of %g ns produced %d writes, and a "
                                "single interrupted strobe can only make one or two"
                                % (ticks * p.tick_ns, written))
            twice += written > 1
        rows.append({"ns": ticks * p.tick_ns, "trials": trials, "hits": twice})
    notes.append("/CSW HIGH excursion, must not end a write: " + curve(rows))
    return rows


def glitch_read_start(p, failures, notes, trials=24):
    """A LOW excursion on /CSR that is too short to be a cycle must not become one.

    The read path confirms before it samples MODE and before it turns the data bus
    around, so a rejected strobe should leave no trace at all: no read, no pointer
    movement, and nothing driven.
    """
    rows = []
    p.vram_write(COUNTER, bytes(i & 0xff for i in range(512)))
    for ticks in range(EVENT_FLOOR_TICKS, 17):
        took = 0
        for _ in range(trials):
            p.set_address(COUNTER)
            events(p, [(IDLE, 32), (CSR_LOW, ticks), (IDLE, 260), (IDLE, 260)])
            reads = p.read_port(PORT_DATA, 1)[0]
            if reads > 1:
                failures.append("a single /CSR LOW excursion of %g ns produced %d reads"
                                % (ticks * p.tick_ns, reads))
            took += reads > 0
        rows.append({"ns": ticks * p.tick_ns, "trials": trials, "hits": took})
    notes.append("/CSR LOW excursion, must not start a read: " + curve(rows))
    return rows


def read_end_rows(p, failures, widths, trials):
    rows = []
    for ticks in widths:
        twice = 0
        for _ in range(trials):
            p.set_address(COUNTER)
            events(p, [(IDLE, 32), (CSR_LOW, 40), (IDLE, ticks), (CSR_LOW, 40),
                       (IDLE, 260), (IDLE, 260)])
            reads = p.read_port(PORT_DATA, 1)[0]
            if reads > 2:
                failures.append("/CSR HIGH excursion of %g ns produced %d reads, and one "
                                "interrupted strobe can only make one or two"
                                % (ticks * p.tick_ns, reads))
            twice += reads > 1
        rows.append({"ns": ticks * p.tick_ns, "trials": trials, "hits": twice})
    return rows


def glitch_read_end(p, failures, notes, trials=24):
    """A HIGH excursion inside an asserted /CSR must not re-arm the read.

    This is the defect itself, and the one edge whose threshold has no spread: the
    release path waits rather than polls, so its ten cycles are exact.

    Counting is free here. After a read the read-ahead buffer holds VRAM at the
    address the reads reached, so filling the region with its own low address byte
    makes the next read return how many reads happened.
    """
    p.vram_write(COUNTER, bytes(i & 0xff for i in range(512)))
    rows = read_end_rows(p, failures, range(EVENT_FLOOR_TICKS, 17), trials)
    notes.append("/CSR HIGH excursion, must not re-arm the read: " + curve(rows))
    return rows


def drive_control(p, failures, notes, trials=32):
    """The same boundary at two drive strengths, to say whether it is the firmware.

    Every threshold here sits a couple of nanoseconds above what the instruction
    count predicts, and there are two candidate explanations: the firmware, or the
    probe's own edges. 3.3 V into a 5 V HC input crosses that input's threshold late
    on the way up and early on the way down, which *narrows* a HIGH excursion by an
    amount that depends on slew rate - so a limit that moves with drive strength is
    the fixture's edges, not the target's logic.

    Only the transition region is swept, because that is where the information is;
    the flat ends cost time and say nothing. It repeats the read release edge, which
    is the one with no polling-loop phase of its own to confuse the comparison.
    """
    settings = ((2, 0), (12, 1))
    widths = range(7, 11)                 # 35-50 ns, either side of every threshold
    p.vram_write(COUNTER, bytes(i & 0xff for i in range(512)))
    out, seen = [], {}
    for ma, fast in settings:
        p.command("DRIVE", ma, fast)
        rows = read_end_rows(p, failures, widths, trials)
        limits = measure(rows)
        seen["%dmA %s" % (ma, "fast" if fast else "slow")] = limits
        out.append({"ma": ma, "fast": bool(fast), "rows": rows, "limits": limits})
        notes.append("  drive %2d mA %-4s: %s" % (ma, "fast" if fast else "slow", curve(rows)))
    # The SDK's own default, which is what every other sweep in this file ran at.
    p.command("DRIVE", 4, 0)
    p50s = [s["p50"] for s in seen.values() if s["p50"] is not None]
    if len(p50s) == 2:
        notes.append("  p50 moves %+.1f ns between 2 mA slow and 12 mA fast - %s"
                     % (p50s[1] - p50s[0],
                        "the probe's edges, not the target's logic"
                        if abs(p50s[1] - p50s[0]) >= 1 else
                        "within a tick, so the offset is not edge rate"))
    return out


# -- Group C: conformance, pinned to the datasheet -------------------------

# TMS9918A Figures 5-3 and 5-4, via HARDWARE.md. These are not thresholds to find;
# they are fixed numbers a host is entitled to use, and every one of them is a whole
# number of 5 ns ticks - which is the entire reason the probe runs at 200 MHz.
DATASHEET_NS = {
    "tsu(A-WL)": 30,     # MODE valid before /CSW falls
    "th(WL-A)": 30,      # MODE held after /CSW falls
    "tsu(D-WH)": 100,    # data valid before /CSW rises
    "th(WH-D)": 30,      # data held after /CSW rises
    "tw(WL)": 200,       # /CSW low
    "tw(CS-H1)": 8000,   # between accesses that request memory
}


def conformance(p, failures, notes):
    """The worst *legal* cycle for each datasheet parameter, which must work.

    These are the opposite of the sweeps above. A sweep asks how far past the
    envelope the board goes and the answer is allowed to move; this asks whether it
    honours the envelope at all, and the answer may not. A capability number
    regressing from 840 ns to 900 is a metric moving. A failure here reaches a
    TI-99.

    Two parameters are missing and their absence is deliberate rather than an
    oversight: `th(WL-A)` and `tsu(A-RL)` both need MODE to change while a strobe is
    asserted, and the probe's event validator refuses that. See the note this
    emits - it is a fixture limitation, not a passing test.
    """
    checks = 0
    tick = p.tick_ns
    def t(ns):
        return p.ticks(ns)

    # tsu(A-WL): MODE becomes valid exactly 30 ns before /CSW falls, and a control
    # write at that setup must still reach the control port. Observable because a
    # control write moves the address and a data write does not. Both halves of the
    # latch are driven at the same setup, since either landing on the wrong port
    # breaks it.
    p.vram_write(GLITCH, bytes((0x77, 0x88)))
    low = t(DATASHEET_NS["tw(WL)"])
    for name, setup in (("tsu(A-WL) at minimum", t(DATASHEET_NS["tsu(A-WL)"])),
                        ("tsu(A-WL) at 10x", t(300))):
        p.set_address(GLITCH + 1)                      # somewhere else entirely
        target, seq = GLITCH, [(IDLE, 32)]
        for byte in (target & 0xff, target >> 8):
            seq += [(IDLE | DRIVEN | byte, 32),        # OE and data, MODE still low
                    (MODE_IDLE | DRIVEN | byte, setup),
                    (MODE_CSW_LOW | DRIVEN | byte, low),
                    (MODE_IDLE | DRIVEN | byte, low),
                    (IDLE | DRIVEN | byte, 32)]        # MODE back low between halves
        seq.append((IDLE, 260))
        events(p, seq)
        got = p.read_port(PORT_DATA, 1)
        if got != bytes((0x77,)):
            failures.append("%s: the control write did not set the address (read %s, "
                            "expected 77)" % (name, got.hex()))
        checks += 1

    # tsu(D-WH) and th(WH-D): the data byte is legal only for the last 100 ns before
    # the strobe rises and the first 30 ns after it. Poison either side, so a
    # firmware sampling outside the window latches the poison rather than the byte.
    for name, strobe_ns, valid_ns, hold_ns in (
            ("tsu(D-WH) and th(WH-D) at minimum", DATASHEET_NS["tw(WL)"],
             DATASHEET_NS["tsu(D-WH)"], DATASHEET_NS["th(WH-D)"]),
            ("tsu(D-WH) and th(WH-D) at 2x", 400, 200, 100)):
        datum, poison = 0x3C, 0xC3
        p.vram_write(GLITCH, bytes((0x00,)))
        p.set_address(GLITCH, write=True)
        low = t(strobe_ns)
        events(p, [(IDLE, 32), (IDLE | DRIVEN | poison, 32),
                   # /CSW low with poison on the bus, then the real byte for exactly
                   # the guaranteed setup, then poison again once the hold expires.
                   (CSW_LOW | DRIVEN | poison, low - t(valid_ns)),
                   (CSW_LOW | DRIVEN | datum, t(valid_ns)),
                   (IDLE | DRIVEN | datum, t(hold_ns)),
                   (IDLE | DRIVEN | poison, 260),
                   (IDLE | poison, 260)])
        got = p.vram_read(GLITCH, 1)
        if got != bytes((datum,)):
            failures.append("%s: wrote %#04x with %#04x either side, VRAM holds %s - the "
                            "data was sampled outside the guaranteed window"
                            % (name, datum, poison, got.hex()))
        checks += 1

    # tw(WL) at the datasheet minimum, and tw(CS-H1) as the gap between two of them.
    # 8 us is more than one event can hold, so the gap is several with identical pins.
    gap = t(DATASHEET_NS["tw(CS-H1)"])
    chunk, rest = 255, []
    while gap > 0:
        step = min(chunk, gap)
        rest.append((IDLE, max(step, EVENT_FLOOR_TICKS)))
        gap -= step
    p.vram_write(GLITCH, bytes(4))
    p.set_address(GLITCH, write=True)
    events(p, [(IDLE, 32), (IDLE | DRIVEN | 0x11, 32),
               (CSW_LOW | DRIVEN | 0x11, t(DATASHEET_NS["tw(WL)"])),
               (IDLE | DRIVEN | 0x11, 32)] + [(w | DRIVEN | 0x11, d) for w, d in rest] +
           [(IDLE | DRIVEN | 0x22, 32),
            (CSW_LOW | DRIVEN | 0x22, t(DATASHEET_NS["tw(WL)"])),
            (IDLE | DRIVEN | 0x22, 260), (IDLE | 0x22, 260)])
    got = p.vram_read(GLITCH, 2)
    if got != bytes((0x11, 0x22)):
        failures.append("tw(WL) at 200 ns with a tw(CS-H1) gap: VRAM holds %s, expected "
                        "1122" % got.hex())
    checks += 1

    checks += mode_hold(p, failures, notes)
    notes.append("conformance: %d worst-legal cycles at %d ns per tick, all exact"
                 % (checks, tick))
    return checks


def mode_hold(p, failures, notes):
    """`th(WL-A)` and `tsu(A-RL)`: MODE moving inside an asserted strobe.

    These are the two parameters that say how *briefly* a host may hold MODE, and
    they are the two this firmware is most likely to fail, because it samples MODE
    a fixed number of PIO cycles after the strobe rather than at the edge.
    `HARDWARE.md` puts that sample at 31.7 ns against a 30 ns guarantee, so a host
    that releases MODE at exactly the datasheet minimum should see the write land on
    the wrong port.

    Needs a probe that will move MODE inside a strobe. Earlier firmware refused, so
    this reports itself as unreachable rather than passing vacuously - a conformance
    test that cannot run must not look like one that ran.
    """
    t = p.ticks
    p.vram_write(GLITCH, bytes((0x77, 0x88)))
    checks = 0

    # th(WL-A): MODE is guaranteed for 30 ns after /CSW falls and no longer. Drop it
    # at exactly 30 ns and the control write must still reach the control port.
    for name, hold_ns in (("th(WL-A) at minimum", DATASHEET_NS["th(WL-A)"]),
                          ("th(WL-A) at 2x", 60)):
        p.set_address(GLITCH + 1)
        target, seq, low = GLITCH, [(IDLE, 32)], t(DATASHEET_NS["tw(WL)"])
        for byte in (target & 0xff, target >> 8):
            seq += [(IDLE | DRIVEN | byte, 32),
                    (MODE_IDLE | DRIVEN | byte, t(DATASHEET_NS["tsu(A-WL)"])),
                    (MODE_CSW_LOW | DRIVEN | byte, t(hold_ns)),
                    (CSW_LOW | DRIVEN | byte, low - t(hold_ns)),   # MODE goes invalid
                    (IDLE | DRIVEN | byte, low)]
        seq.append((IDLE, 260))
        try:
            events(p, seq)
        except ProbeError as why:
            notes.append("  th(WL-A) and tsu(A-RL) UNREACHABLE: %s" % why)
            notes.append("  the probe refuses MODE moving inside a strobe; flash a "
                         "protocol 2 build with the relaxed validator")
            return checks
        got = p.read_port(PORT_DATA, 1)
        if got != bytes((0x77,)):
            failures.append("%s: MODE released at the datasheet minimum and the control "
                            "write did not reach the control port (read %s, expected 77)"
                            % (name, got.hex()))
        checks += 1

    # tsu(A-RL) is 0 ns: MODE is guaranteed only *at* the falling edge of /CSR, with
    # no setup at all. The /CSR delay chain through U6 exists to manufacture that
    # margin in hardware, so this is a test of the chain as much as the firmware -
    # which looks like an error unless it says so.
    p.vram_write(COUNTER, bytes(i & 0xff for i in range(512)))
    p.set_address(COUNTER)
    events(p, [(MODE_IDLE, 32),                        # MODE high, both strobes idle
               (CSR_LOW, t(DATASHEET_NS["tw(WL)"])),   # MODE drops as /CSR falls
               (IDLE, 260), (IDLE, 260)])
    got = p.read_port(PORT_DATA, 1)[0]
    if got != 1:
        failures.append("tsu(A-RL) at 0 ns: MODE fell with /CSR and the access counted "
                        "as %d data reads rather than 1" % got)
    checks += 1
    notes.append("  MODE moved inside the strobe for %d cycles: th(WL-A) and tsu(A-RL) "
                 "both reachable" % checks)
    return checks


# -- Group D: capability limits, the numbers that move ---------------------

# The probe's own defaults in ticks - 255 ns setup, 1 us pulse, 1 us hold. Every
# sweep below restores them on the way out, because the probe keeps whatever
# timing it was last given and a stale candidate would quietly poison every group
# that ran after it.
SAFE_TIMING = (51, 200, 200, 1)

# hb_set_timing's floors. A sweep that asks for less is refused rather than
# clamped, so the sweeps start here - and a limit that lands on one of them is a
# statement about the instrument rather than about the board.
MIN_SETUP, MIN_PULSE, MIN_HOLD = 5, 6, 6

# Bytes per sustained-rate measurement. Long enough that read-ahead cannot hide
# behind a short burst, short enough that a bracketed sweep with repeats is
# seconds rather than minutes. `burst_floor` is the test that says whether the
# choice matters.
BURST = 4096

DATUM, POISON = 0xA5, 0x5A


def split(total):
    """A period as setup/pulse/hold, at roughly a quarter, a half and a quarter.

    The shape has to be stated because the answer depends on it: a 500 ns period
    with a 450 ns strobe and one with a 50 ns strobe are different questions. Half
    the period asserted is close to what a Z80 I/O cycle does, and it keeps both
    the strobe and the recovery gap clear of their own floors so that the period
    is what is being measured.
    """
    spare = total - (MIN_SETUP + MIN_PULSE + MIN_HOLD)
    if spare < 0:
        raise ValueError("%d ticks is below the probe's own floor" % total)
    pulse = MIN_PULSE + spare // 2
    setup = MIN_SETUP + (spare - spare // 2) // 2
    return setup, pulse, total - setup - pulse


def sustained(p, read, count, repeat, setup, pulse, hold, divider=1):
    """Verified bursts at one timing, as (passes, attempts, errors).

    The probe writes the pattern and reads it back at its safe timing either side,
    so only the burst under test runs at the candidate and a failure is the burst
    rather than the setup around it. Each repeat gets its own pattern, and the
    error tally is summed here rather than in the probe: near the limit a burst is
    wrong in tens of bytes out of thousands, and "1 of 4 passed" throws away the
    part of that which says how close it was.
    """
    p.command("TIMING", setup, pulse, hold, divider)
    passes = errors = 0
    for index in range(repeat):
        answer = p.command("STRESS", 1 if read else 0, count, 39192 + index, 1)
        passes += answer["passes"]
        errors += answer["errors"]
    return passes, repeat, errors


def bracket(attempt, floor, ceiling):
    """The smallest value in [floor, ceiling] that passes, by halving.

    This only has to land near the edge; the sweep that follows re-measures the
    neighbourhood, so the assumption that passing is upward-closed is never load
    bearing - if it is violated the curve shows it.
    """
    # The ceiling goes first. Some of these attempts drive the target far past
    # anything it can keep up with, and one that has just been hammered wants a
    # settled reference measured before it rather than after.
    if not attempt(ceiling):
        return None
    if attempt(floor):
        return floor
    lo, hi = floor, ceiling
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if attempt(mid):
            hi = mid
        else:
            lo = mid
    return hi


def limit(p, run_at, to_ns, floor, ceiling, repeat=8, points=24, count=None,
          step=None):
    """Bracket the smallest passing value, then walk down through the transition.

    `run_at(value, repeat)` returns (passes, attempts, errors). The width of the
    transition is not known in advance and it is not small - a sustained rate
    degrades over hundreds of nanoseconds rather than snapping - so the sweep sizes
    its own step from the bracket and stops once two consecutive points fail
    outright. A fixed span either misses the transition or spends the whole budget
    on the flat part.
    """
    edge = bracket(lambda v: run_at(v, 1)[0] == 1, floor, ceiling)
    if edge is None:
        return None, []
    step, rows = step or max(1, edge // 32), {}

    def at(value):
        hits, attempts, errors = run_at(value, repeat)
        rows[value] = {"ns": to_ns(value), "trials": attempts, "hits": hits,
                       "errors": errors}
        if count:
            rows[value]["bytes"] = count * attempts
        return hits, attempts

    # Up from the bracket until three consecutive points are unanimous, because one
    # is not evidence that the curve has settled, and down until two fail outright.
    value, clean = edge, 0
    while clean < 3 and len(rows) < points and value <= ceiling:
        hits, attempts = at(value)
        clean = clean + 1 if hits == attempts else 0
        value += step
    # Downward, the stop has to be the loss rate rather than the pass rate. On a
    # path whose limit is a tail, every burst stops being perfect long before many
    # bytes are wrong - so stopping at two zero-pass points ends the sweep at a
    # different depth every run, and the shape of the tail is then fitted over a
    # range that moves. Two points losing a tenth of the burst is a fixed place.
    value, done = edge - step, 0
    while done < 2 and len(rows) < points and value >= floor:
        hits, attempts = at(value)
        row = rows[value]
        spent = (row["errors"] / float(row["bytes"]) > 0.1 if row.get("bytes")
                 else hits == 0)
        done = done + 1 if spent else 0
        value -= step
    return edge, [rows[value] for value in sorted(rows)]


def rate_note(notes, label, rows, budget_ns=None):
    limits = measure(rows)
    clean = limits["clean_from"]
    line = "%s: clean from %s" % (label, _ns(clean))
    if budget_ns and clean:
        line += ", %.0f%% of the %g ns a host may assume" % (100.0 * clean / budget_ns,
                                                             budget_ns)
    notes.append(line)
    notes.append("  " + curve(rows))
    # The pass rate says where the cliff is. The loss rate says whether it is a
    # cliff at all - and it is not: it falls off smoothly over hundreds of
    # nanoseconds. That shape is the target's own response latency having a tail,
    # and the length of the tail is the number worth recording, because it is what
    # a change to the hot path moves. A fixed periodic interruption would instead
    # hold the loss rate flat as the period shortens.
    lost = [r for r in rows if r.get("bytes")]
    if lost:
        notes.append("  bytes lost: " + ", ".join(
            "%gns:%s" % (r["ns"], "0" if not r["errors"] else
                         "%.2f%%" % (100.0 * r["errors"] / r["bytes"]))
            for r in lost))
        notes.append("  " + decay(lost, clean))
    return rows


def decay(rows, clean_ns):
    """How fast the loss rate falls as the period lengthens, as ns per tenfold.

    Straight least squares on log10(loss) against the period, over the points that
    lost something. It is a one-number summary of the tail: a board whose response
    is occasionally slow has a long one, and shortening the worst case shortens it.
    """
    points = [(r["ns"], r["errors"] / float(r["bytes"])) for r in rows if r["errors"]]
    if len(points) < 8:
        return "too few lossy points to say how the tail falls off"
    xs = [x for x, _ in points]
    ys = [math.log10(y) for _, y in points]
    n = len(points)
    mx, my = sum(xs) / n, sum(ys) / n
    spread = sum((x - mx) ** 2 for x in xs)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / spread if spread else 0
    if slope >= 0:
        return "the loss rate does not fall with the period, which is not a latency tail"
    return ("the loss rate falls tenfold every %.0f ns of extra period, from %.2f%% at "
            "%g ns to nothing by %s - a smooth tail, so it is the worst case of the "
            "target's own response rather than a fixed interruption"
            % (-1.0 / slope, 100 * points[0][1], points[0][0], _ns(clean_ns)))


def period_limit(p, read, failures, notes, count=BURST):
    """The shortest access-to-access period that still moves every byte correctly.

    The headline number, and the one the read-ahead work moves. The datasheet's own
    answer is 8 us between accesses that request memory; anything under that is
    capability, so this cannot fail a run - but it is the number that says whether
    a change to the hot path cost the bus anything.
    """
    what = "read" if read else "write"
    # One phase is 255 ticks at most, so the finest grid reaches about 2.5 us per
    # access. A board slower than that - the plan's real F18A, eventually - needs a
    # coarser tick rather than a longer sweep, so escalate rather than give up.
    edge, rows, tick = None, [], p.tick_ns
    for divider in (1, 2, 4, 8):
        tick = p.tick_ns * divider

        def run_at(total, repeat, divider=divider):
            return sustained(p, read, count, repeat, *split(total), divider=divider)

        try:
            # One tick at a time across the whole degraded band. The loss rate
            # falls off smoothly rather than snapping, so the shape of the fall is
            # the measurement and a coarse step throws it away.
            edge, rows = limit(p, run_at, lambda v, tick=tick: v * tick, 17, 500,
                               count=count, repeat=6, points=48, step=1)
        finally:
            p.command("TIMING", *SAFE_TIMING)
        if edge is not None:
            break
    if edge is None:
        failures.append("%s period: %d bytes failed at every period up to 20 us per "
                        "access, well past the datasheet's own tw(CS-H1) - the bus is "
                        "not working" % (what, count))
        return rows
    setup, pulse, hold = split(edge)
    rate_note(notes, "minimum sustained %s period over %d bytes" % (what, count), rows,
              DATASHEET_NS["tw(CS-H1)"])
    notes.append("  at the edge that is %g ns setup, %g ns strobe, %g ns recovery, "
                 "%.1f MB/s" % (setup * tick, pulse * tick, hold * tick,
                                1000.0 / (edge * tick)))
    return rows


def read_period(p, failures, notes):
    """Minimum sustained read period."""
    return period_limit(p, True, failures, notes)


def write_period(p, failures, notes):
    """Minimum sustained write period.

    The write path has no prefetch to starve, so it moves independently of the
    read one and a divergence between them says which side a change landed on.
    """
    return period_limit(p, False, failures, notes)


def strobe_limit(p, read, failures, notes, count=BURST):
    """The shortest strobe that still transfers, with the gap held wide open.

    This separates "the pulse is too short" from "the cycle is too fast", which
    the period sweep alone cannot. On the read side it is also a cross-check on
    `read_window` by a completely different route: the probe samples 15 ns before
    the rising edge for any strobe at or under 175 ns, so the floor here should
    land about 15 ns above the point where the window sweep says data is valid. Two
    methods agreeing is worth more than either one's absolute number.
    """
    tick = p.tick_ns
    what = "read" if read else "write"

    def run_at(pulse, repeat):
        return sustained(p, read, count, repeat, 200, pulse, 200)

    try:
        edge, rows = limit(p, run_at, lambda v: v * tick, MIN_PULSE, 255, count=count)
    finally:
        p.command("TIMING", *SAFE_TIMING)
    if edge is None:
        failures.append("%s strobe: no width up to 1275 ns transferred correctly"
                        % what)
        return rows
    rate_note(notes, "minimum %s strobe width" % what, rows, DATASHEET_NS["tw(WL)"])
    return rows


def read_strobe(p, failures, notes):
    """Minimum /CSR width returning correct data."""
    return strobe_limit(p, True, failures, notes)


def write_strobe(p, failures, notes):
    """Minimum /CSW width that latches."""
    return strobe_limit(p, False, failures, notes)


def recovery_limit(p, read, failures, notes, count=BURST):
    """The shortest gap between accesses, with the strobe held wide open.

    The other half of the split: this is the target's own turnaround, the part of
    the period that has nothing to do with how long the host asserts anything.
    """
    tick = p.tick_ns
    what = "read" if read else "write"

    def run_at(gap, repeat):
        setup = max(MIN_SETUP, gap // 2)
        return sustained(p, read, count, repeat, setup, 200, gap - setup)

    try:
        edge, rows = limit(p, run_at, lambda v: v * tick, MIN_SETUP + MIN_HOLD, 510,
                           count=count)
    finally:
        p.command("TIMING", *SAFE_TIMING)
    if edge is None:
        failures.append("%s recovery: no gap up to 2550 ns transferred correctly"
                        % what)
        return rows
    rate_note(notes, "minimum inter-access %s recovery" % what, rows)
    return rows


def read_recovery(p, failures, notes):
    """Minimum gap between reads."""
    return recovery_limit(p, True, failures, notes)


def write_recovery(p, failures, notes):
    """Minimum gap between writes."""
    return recovery_limit(p, False, failures, notes)


def mode_window(p, failures, notes, trials=8):
    """How long MODE must actually be held after /CSW falls.

    The conformance case says the datasheet's 30 ns is not enough. This says by how
    much, which is the number a fix has to move and the one `HARDWARE.md` can carry:
    the firmware samples MODE a fixed eight PIO cycles after the strobe, so the
    threshold here *is* that sample instant, read off the board rather than counted
    off the program.

    Observable because a control write moves the address and a data write does not.
    Both halves of the latch are driven at the same hold, since either one landing
    on the wrong port breaks the address.
    """
    tick, rows = p.tick_ns, []
    target, low = GLITCH, p.ticks(DATASHEET_NS["tw(WL)"])
    setup = p.ticks(DATASHEET_NS["tsu(A-WL)"])
    for hold in range(EVENT_FLOOR_TICKS, low - EVENT_FLOOR_TICKS + 1):
        landed = 0
        for _ in range(trials):
            p.vram_write(target, bytes((0x77, 0x88, 0x99, 0xaa)))
            p.set_address(target + 1)
            seq = [(IDLE, 32)]
            for byte in (target & 0xff, target >> 8):
                seq += [(IDLE | DRIVEN | byte, 32),
                        (MODE_IDLE | DRIVEN | byte, setup),
                        (MODE_CSW_LOW | DRIVEN | byte, hold),
                        (CSW_LOW | DRIVEN | byte, low - hold),
                        (IDLE | DRIVEN | byte, low)]
            events(p, seq + [(IDLE, 260)])
            landed += p.read_port(PORT_DATA, 1) == bytes((0x77,))
        rows.append({"ns": hold * tick, "trials": trials, "hits": landed})
        if landed == trials and len(rows) > 3:
            break
    limits = measure(rows)
    notes.append("MODE must be held %s after /CSW falls for a control write to reach "
                 "the control port, against the %g ns a host may assume"
                 % (_ns(limits["clean_from"]), DATASHEET_NS["th(WL-A)"]))
    notes.append("  " + curve(rows))
    if limits["clean_from"] and limits["clean_from"] > DATASHEET_NS["th(WL-A)"]:
        notes.append("  that is %g ns outside the datasheet, so a host releasing MODE "
                     "at the guaranteed minimum puts its control write on the data port"
                     % (limits["clean_from"] - DATASHEET_NS["th(WL-A)"]))
    return rows


def pair_split(half):
    """setup/strobe/hold for one direction of an alternating pair."""
    spare = half - 3 * EVENT_FLOOR_TICKS
    if spare < 0:
        raise ValueError("%d ticks is below the sequencer's three-event floor" % half)
    strobe = EVENT_FLOOR_TICKS + spare // 2
    setup = EVENT_FLOOR_TICKS + (spare - spare // 2) // 2
    return setup, strobe, half - setup - strobe


def alternation(p, failures, notes, pairs=512, datum=0xA5):
    """A write and a read every other access, which turns the bus around each time.

    The two directions have completely different floors - 405 ns and 855 - so the
    question is what a host pays for mixing them. It also flips U5's `DIR` on every
    cycle, which nothing else here does: every other sweep leaves the transceiver
    pointing the same way for thousands of accesses.

    The sequencer is what makes this sustainable at all. A 6-event pair repeated a
    few hundred times is one DMA transfer with no CPU inside it, so the alternation
    runs at exactly the period asked for rather than at whatever a command-per-
    access round trip allows.

    Counting is by phase rather than by value. Every write puts the same byte down,
    so a wrong byte says nothing - but the writes land on even offsets and the reads
    consume the odd ones, so a lost or doubled access swaps the two from that point
    on and the whole tail of the region reads back inverted.
    """
    want = bytes((datum if i % 2 == 0 else 0) for i in range(2 * pairs))

    def run_at(total, repeat):
        half = total // 2
        write, read = pair_split(half), pair_split(total - half)
        seq = [(IDLE | DRIVEN | datum, write[0]), (CSW_LOW | DRIVEN | datum, write[1]),
               (IDLE | DRIVEN | datum, write[2]), (IDLE, read[0]),
               (CSR_LOW, read[1]), (IDLE, read[2])]
        words = "".join("%08x" % (pins | ((ticks - EVENT_FLOOR_TICKS) << 20))
                        for pins, ticks in seq)
        passes = errors = 0
        for _ in range(repeat):
            p.vram_write(0, bytes(2 * pairs))
            p.set_address(0, write=True)
            p.command("EVENTS", 1, words, pairs)
            wrong = sum(1 for a, b in zip(p.vram_read(0, 2 * pairs), want) if a != b)
            errors += wrong
            passes += wrong == 0
        return passes, repeat, errors

    tick = p.tick_ns
    edge, rows = limit(p, run_at, lambda v: v * tick / 2.0, 6 * EVENT_FLOOR_TICKS, 500,
                       count=2 * pairs, repeat=6, points=40, step=2)
    if edge is None:
        failures.append("alternation: a write and a read alternating never ran clean, "
                        "at any period up to 1.25 us per access")
        return rows
    rate_note(notes, "minimum alternating read/write period, per access", rows)
    notes.append("  %d pairs per run, so the transceiver turns around %d times without "
                 "a command between any two of them" % (pairs, 2 * pairs))

    # What the sweep above proves is that every access was counted and every write
    # landed. It says nothing about what the reads returned, because a repeated
    # sequence only hands back the first iteration's samples. So bracket the other
    # floor separately: a short sequence, every read sampled, every sample checked.
    # Ceiling well clear of the answer: every read of every round has to be right,
    # so a ceiling only a little above the threshold fails the bracket outright
    # whenever one read misses.
    verified = bracket(lambda v: alternation_reads(p, v, datum)[0] == 8, edge, 1000)
    if verified is None:
        failures.append("alternation: no period up to 1.25 us per access returned the "
                        "written byte on the read that follows the write")
        return rows
    notes.append("  reads return the right byte only from %g ns per access, %.1fx the "
                 "%g ns at which the accesses merely all count - a host that alternates "
                 "and looks at what comes back is held to the slower of the two"
                 % (verified * tick / 2.0, verified / float(edge), edge * tick / 2.0))
    return rows


def alternation_reads(p, total, datum, rounds=8, pairs=10):
    """Does the read in each pair return the byte the write just put there?

    A data write loads its own byte into the read-ahead buffer, so the read that
    follows must hand that byte straight back. Ten pairs fit inside one sequence,
    which is what makes every read's sample visible rather than only the first.
    """
    half = total // 2
    write, read = pair_split(half), pair_split(total - half)
    pair = [(IDLE | DRIVEN | datum, write[0]), (CSW_LOW | DRIVEN | datum, write[1]),
            (IDLE | DRIVEN | datum, write[2]), (IDLE, read[0]),
            (CSR_LOW, read[1]), (IDLE, read[2])]
    words = "".join("%08x" % (pins | ((ticks - EVENT_FLOOR_TICKS) << 20))
                    for pins, ticks in pair * pairs)
    good = 0
    for _ in range(rounds):
        # A status read first: it clears the control latch, so a previous round
        # that ran too fast to be understood cannot leave this one half-addressed.
        p.status()
        p.set_address(0, write=True)
        data = bytes.fromhex(p.command("EVENTS", 1, words)["data"])
        good += all(data[6 * i + 4] == datum for i in range(pairs))
    return good, rounds


def read_window(p, failures, notes, trials=8):
    """When the read data actually appears, measured rather than derived.

    `HARDWARE.md` puts the byte on the host socket 87-128 ns after /CSR falls, from
    an instruction count plus the two buffer delays. This asks the board instead.

    The sequencer samples the bus once per event, so one long strobe chopped into
    58 short events measures 58 points in a single read cycle; sliding the first
    event's length across a tick fills in the grid between them. The whole curve is
    five commands rather than several hundred, and every point comes from a cycle
    of identical shape - only the sample moves.
    """
    step, points, tick = 5, 58, p.tick_ns
    p.vram_write(0, bytes([DATUM]) * VRAM_SIZE)
    p.set_address(0)
    tally = {}
    for phase in range(step):
        first = EVENT_FLOOR_TICKS + phase
        seq = ([(IDLE | DRIVEN | POISON, 40), (IDLE, 40), (CSR_LOW, first)] +
               [(CSR_LOW, step)] * (points - 1) + [(IDLE, 260), (IDLE, 260)])
        offsets, at = [first - 1], first
        for _ in range(points - 1):
            at += step
            offsets.append(at - 1)
        for _ in range(trials):
            got = bytes.fromhex(events(p, seq)["data"])
            for i, offset in enumerate(offsets):
                slot = tally.setdefault(offset * tick, [0, 0])
                slot[0] += got[2 + i] == DATUM
                slot[1] += 1
    rows = [{"ns": ns, "hits": hits, "trials": count}
            for ns, (hits, count) in sorted(tally.items())]
    limits = measure(rows)
    notes.append("read data valid from %s after /CSR falls, p50 %s"
                 % (_ns(limits["taken_from"]), _ns(limits["p50"])))
    # The curve is 290 points; the record keeps them all and the note keeps the
    # transition, which is the only part anyone reads.
    edge = [r for r in rows if 0 < r["hits"] < r["trials"]]
    if edge:
        notes.append("  " + curve(rows[max(0, rows.index(edge[0]) - 2):
                                       rows.index(edge[-1]) + 3]))
    return rows


# The datum is never absent from a write cycle by accident: a byte still holding
# this after one says the cycle did not write at all, which is a different defect
# from writing the wrong byte and must not be counted as one.
PREFILL = 0x00


def capture_case(start, low=40, gap=100):
    """One write cycle whose data byte is present for exactly one 25 ns event.

    `start` is in ticks relative to /CSW rising, negative before it. The event
    floor is what limits the offsets: a 25 ns window can sit anywhere inside the
    strobe, end exactly on the edge, or start on it, but it cannot be narrower and
    it cannot straddle the edge.
    """
    if start + EVENT_FLOOR_TICKS <= 0:
        before = -start
        seq = [(IDLE | DRIVEN | POISON, gap),
               (CSW_LOW | DRIVEN | POISON, low - before),
               (CSW_LOW | DRIVEN | DATUM, EVENT_FLOOR_TICKS)]
        if before > EVENT_FLOOR_TICKS:
            seq.append((CSW_LOW | DRIVEN | POISON, before - EVENT_FLOOR_TICKS))
    else:
        seq = [(IDLE | DRIVEN | POISON, gap), (CSW_LOW | DRIVEN | POISON, low)]
        if start:
            seq.append((IDLE | DRIVEN | POISON, start))
        seq.append((IDLE | DRIVEN | DATUM, EVENT_FLOOR_TICKS))
    seq.append((IDLE | DRIVEN | POISON, 20))
    return seq


def capture_starts(low=40):
    """Every offset a 25 ns datum window can take, in ticks relative to the rise."""
    inside = [-b for b in range(2 * EVENT_FLOOR_TICKS, low - EVENT_FLOOR_TICKS + 1)]
    return (sorted(inside) + [-EVENT_FLOOR_TICKS, 0] +
            list(range(EVENT_FLOOR_TICKS, 3 * EVENT_FLOOR_TICKS + 1)))


def write_cases(p, cases, failures, trials=8, room=58):
    """Run each case as a write cycle and read back the byte it left.

    A write advances the address, so N cycles in one event sequence leave N bytes
    and each byte says whether its own cycle latched the datum, the poison around
    it, or nothing at all. That is the counting trick the glitch sweeps use,
    applied to a window rather than a threshold. The sequencer holds 64 events, so
    the cases are packed into as few sequences as fit.
    """
    hits, packs, pack, used = [0] * len(cases), [], [], 0
    for index, case in enumerate(cases):
        if used + len(case) > room and pack:
            packs.append(pack)
            pack, used = [], 0
        pack.append((index, case))
        used += len(case)
    if pack:
        packs.append(pack)
    for _ in range(trials):
        for pack in packs:
            p.vram_write(GLITCH, bytes([PREFILL]) * len(pack))
            p.set_address(GLITCH, write=True)
            seq = [(IDLE, 32)]
            for _, case in pack:
                seq += case
            events(p, seq + [(IDLE, 260)])
            landed = p.vram_read(GLITCH, len(pack))
            for (index, _), byte in zip(pack, landed):
                if byte == PREFILL:
                    failures.append("write cycle %d left VRAM untouched: a 200 ns "
                                    "strobe did not write at all" % index)
                hits[index] += byte == DATUM
    return hits


def write_window(p, failures, notes, trials=8):
    """Where the firmware actually samples the write data, relative to the edge.

    The two-ended question - how late the data may appear and how early it may
    vanish - has one answer, because there is one sample. So rather than sweeping
    setup and hold separately, this slides a single 25 ns presence window across
    the rising edge: a window hits only if the sample falls inside it, so the band
    of hitting windows brackets the sample itself.

    `HARDWARE.md` derives that sample at 35 ns before the rise to 1.5 ns after,
    from a polling loop that captures continuously and a synchroniser that delays
    what it captures. The margin against the datasheet is what belongs in the
    record rather than the bare number: a sample moving from -10 ns to -25 ns is
    invisible as a number and is 15 ns off a host's guarantee.
    """
    tick = p.tick_ns
    starts = capture_starts()
    hits = write_cases(p, [capture_case(s) for s in starts], failures, trials)
    rows = [{"ns": s * tick, "hits": h, "trials": trials}
            for s, h in zip(starts, hits)]
    width = EVENT_FLOOR_TICKS * tick
    notes.append("write data sample, by 25 ns presence window (ns from /CSW rising):")
    notes.append("  " + curve([r for r in rows if r["hits"]]) or "  nothing hit")
    always = [r["ns"] for r in rows if r["hits"] == r["trials"]]
    seen = [r["ns"] for r in rows if r["hits"]]
    if always:
        lo, hi = max(always), min(always) + width
        notes.append("  the sample lands between %+g and %+g ns of the rising edge, "
                     "which is one window: the event floor is the resolution"
                     % (lo, hi))
        notes.append("  so the data may appear as late as %+g ns and vanish as early "
                     "as %+g ns: %g ns of margin on tsu(D-WH), %g ns on th(WH-D)"
                     % (lo, hi, DATASHEET_NS["tsu(D-WH)"] + lo,
                        DATASHEET_NS["th(WH-D)"] - hi))
    elif seen:
        notes.append("  no window hit every trial; the sample is spread across %+g to "
                     "%+g ns" % (min(seen), max(seen) + width))
    else:
        failures.append("no 25 ns presence window anywhere from %+g to %+g ns of the "
                        "rising edge latched the data" % (rows[0]["ns"], rows[-1]["ns"]))
    return [{"leg": "capture instant", "rows": rows, "limits": measure(rows)}]


def burst_floor(p, failures, notes, read=True):
    """How fast a host may go, as a function of how much it moves at once.

    The headline period is measured over 4096 bytes, and a host that moves 16 at a
    time is entitled to ask whether that number applies to it. It does not: a short
    burst runs clean well below the sustained floor, because whatever the target
    occasionally takes too long over, a short burst can finish before meeting it.

    So this is the table rather than the single number - and it is the table a host
    author actually needs, since real loops move a row, a sprite attribute or a
    register's worth, not sixteen kilobytes.
    """
    lengths = (16, 64, 256, 1024, 4096, VRAM_SIZE)
    what = "read" if read else "write"
    legs = []
    try:
        for count in lengths:
            # Short bursts are cheap and need more repeats before a clean answer
            # means anything; long ones are their own evidence.
            repeat = min(32, max(3, 4096 // count))
            tried = []

            def attempt(total, count=count, repeat=repeat, tried=tried):
                passes = sustained(p, read, count, repeat, *split(total))[0]
                tried.append(passes == repeat)
                return passes == repeat

            clean = bracket(attempt, 17, 500)
            legs.append({"leg": "%d bytes" % count, "bytes": count, "repeat": repeat,
                         "clean_ns": clean * p.tick_ns if clean else None,
                         "rows": [{"ns": clean * p.tick_ns if clean else 0,
                                   "trials": len(tried), "hits": sum(tried)}]})
    finally:
        p.command("TIMING", *SAFE_TIMING)
    notes.append("sustained %s floor by burst length:" % what)
    for leg in legs:
        notes.append("  %5d bytes: clean from %-8s %s"
                     % (leg["bytes"], _ns(leg["clean_ns"]),
                        "= %.0f us for the whole burst, over %d tries"
                        % (leg["bytes"] * leg["clean_ns"] / 1000.0, leg["repeat"])
                        if leg["clean_ns"] else "never ran clean"))
    floors = [leg["clean_ns"] for leg in legs if leg["clean_ns"]]
    if len(floors) == len(legs) and floors[0]:
        notes.append("  the shortest burst goes %.0f%% faster than the longest, so the "
                     "headline number is the worst case rather than the only one"
                     % (100.0 * (floors[-1] - floors[0]) / floors[0]))
    return legs


def read_burst_floor(p, failures, notes):
    """Read floor against burst length."""
    return burst_floor(p, failures, notes, read=True)


def write_burst_floor(p, failures, notes):
    """Write floor against burst length."""
    return burst_floor(p, failures, notes, read=False)


# Somewhere the sustained-rate bursts never reach, so the sprite list can be set
# once and stay set while VRAM below it is rewritten thousands of times.
SPRITES = 0x3F00

# R0 and R1 for each display state, with 16 KiB selected and the frame interrupt
# off throughout. Blanked first, because it is the control: if the read floor does
# not move between blanked and the heaviest mode, the renderer is not what sets it
# and every line of effort spent there is wasted.
DISPLAY = (
    ("blanked", 0x00, 0x80),
    ("graphics I", 0x00, 0xC0),
    ("graphics II", 0x02, 0xC0),
    ("multicolour", 0x00, 0xC8),
    ("text", 0x00, 0xD0),
)


def display_load(p, failures, notes):
    """Does what the board is drawing set how fast a host may read?

    Below the floor the read path loses about one byte in a hundred, and the first
    guess is always the renderer - the board is drawing a line every 64 us and a
    line is the most expensive thing it does. This is the experiment rather than
    the guess: move the per-line work and see whether the floor moves with it. A
    floor that is the same blanked as it is in the most expensive mode is not the
    renderer's, and knowing that before optimising the renderer is worth more than
    the measurement.

    The sprite list is turned off first, because a random VRAM pattern is also a
    random sprite attribute table and would put a different number of sprites on
    each line of each run - which is a third variable in a two-variable experiment.
    """
    p.register(5, SPRITES >> 7)
    p.vram_write(SPRITES, bytes((0xD0,)))
    legs = []
    try:
        # The floor itself is a one-shot answer and moves by a tick or two between
        # runs, so the loss rate at one fixed period below it carries the
        # comparison: a display state that costs more loses more of the same
        # accesses, and that shows up long before the floor moves.
        p.register(0, 0x00)
        p.register(1, 0x80)
        reference = bracket(
            lambda v: sustained(p, True, BURST, 3, *split(v))[0] == 3, 17, 500)
        if reference is None:
            failures.append("display: no period ran clean with the display blanked")
            return legs
        total = max(17, reference * 3 // 4)
        for name, r0, r1 in DISPLAY:
            p.register(0, r0)
            p.register(1, r1)
            passes, attempts, errors = sustained(p, True, BURST, 8, *split(total))
            clean = bracket(
                lambda v: sustained(p, True, BURST, 3, *split(v))[0] == 3, 17, 500)
            legs.append({"leg": name, "r0": r0, "r1": r1,
                         "clean_ns": clean * p.tick_ns if clean else None,
                         "rows": [{"ns": total * p.tick_ns, "trials": attempts,
                                   "hits": passes, "errors": errors,
                                   "bytes": BURST * attempts}]})
    finally:
        p.command("TIMING", *SAFE_TIMING)
        p.register(0, 0x00)
        p.register(1, 0xC0)
    notes.append("read floor and loss by display state, probed at %g ns per access:"
                 % (total * p.tick_ns))
    for leg in legs:
        row = leg["rows"][0]
        notes.append("  %-12s clean from %-8s  %d/%d bursts perfect, %.2f%% of bytes "
                     "lost" % (leg["leg"], _ns(leg["clean_ns"]), row["hits"],
                               row["trials"], 100.0 * row["errors"] / row["bytes"]))
    loss = [leg["rows"][0]["errors"] / float(leg["rows"][0]["bytes"]) for leg in legs]
    if max(loss) > 0:
        notes.append("  the dearest display state loses %.1fx what the blanked one "
                     "does%s" % (max(loss) / max(min(loss), 1e-9),
                                 " - so what the board is drawing is most of what "
                                 "sets how fast a host may read"
                                 if max(loss) > 2 * max(min(loss), 1e-9) else
                                 " - so the renderer is not what sets it, and the "
                                 "recurring cost is somewhere else entirely"))
    return legs


# -- Group F: real host access patterns ------------------------------------

# One CPU's tightest published VDP loop each, as the access-to-access period and
# how long the strobe is asserted inside it. A Z80 I/O cycle holds /IORQ with /RD
# or /WR for about 2.5 T-states including the automatic wait state; OUTI is 16
# T-states and OTIR is 21 while it repeats.
#
# These are models, not measurements - the plan's second DUT is where a real
# machine's own numbers come from. Each is set at or slightly *faster* than the
# machine it stands for, so passing one bounds the real host rather than
# approximating it. Every one of them is well inside the datasheet's 8 us
# tw(CS-H1), which is the point: real hosts violate it and a real TMS9918A
# corrupts, so "the datasheet says 8 us" was never the useful answer.
PROFILES = (
    ("z80 4MHz OTIR", 5250, 625, "Tatung Einstein, the machine the read fix came from"),
    ("z80 4MHz OUTI", 4000, 625, "unrolled, the fastest a stock 4 MHz Z80 manages"),
    ("z80 3.58MHz OUTI", 4470, 698, "ColecoVision, MSX, NABU"),
    ("z80 8MHz OUTI", 2000, 313, "an accelerated Z80, past any stock machine"),
    ("tms9900 3MHz", 5330, 667, "TI-99/4A, two clock cycles of strobe"),
)


def profile_timing(p, period_ns, strobe_ns):
    """setup/pulse/hold/divider for a host profile, at the finest grid that fits.

    Ascending divider, so the first one that fits is also the least quantised.
    """
    for divider in range(1, 65):
        tick = p.tick_ns * divider
        total, pulse = period_ns // tick, strobe_ns // tick
        setup = (total - pulse) // 2
        hold = total - pulse - setup
        if (MIN_SETUP <= setup <= 255 and MIN_PULSE <= pulse <= 255
                and MIN_HOLD <= hold <= 255):
            return setup, pulse, hold, divider
    return None


def profiles(p, failures, notes, count=1024, repeat=4):
    """Every supported host's tightest loop, both directions, must work.

    This is the closest thing to a verification of the Einstein fix available
    without an Einstein: the reported symptom was GFXII text corruption, which is
    exactly a tight VRAM write loop, and the first profile is that loop.

    Unlike the sweeps above these are pass/fail. A capability number regressing is
    a metric moving; a host profile failing reaches somebody's machine.
    """
    checks = 0
    try:
        # The read floor, measured here rather than borrowed, so each profile can
        # report how much room it has rather than only that it passed.
        floor = bracket(lambda v: sustained(p, True, BURST, 3, *split(v))[0] == 3,
                        17, 500)
        margin = floor * p.tick_ns if floor else 0
        for name, period_ns, strobe_ns, machines in PROFILES:
            shape = profile_timing(p, period_ns, strobe_ns)
            if shape is None:
                failures.append("%s: no probe timing expresses %d ns with a %d ns "
                                "strobe" % (name, period_ns, strobe_ns))
                continue
            setup, pulse, hold, divider = shape
            tick = p.tick_ns * divider
            for read in (True, False):
                passes, attempts, errors = sustained(p, read, count, repeat, *shape)
                if passes != attempts:
                    failures.append(
                        "%s (%s): %d of %d %s bursts of %d bytes failed at %g ns per "
                        "access with a %g ns strobe, %d bytes wrong"
                        % (name, machines, attempts - passes, attempts,
                           "read" if read else "write", count,
                           (setup + pulse + hold) * tick, pulse * tick, errors))
                checks += attempts
            period = (setup + pulse + hold) * tick
            notes.append("  %-18s %6g ns access, %5g ns strobe, %3.0f B/ms both ways, "
                         "%.1fx the %s floor - %s"
                         % (name, period, pulse * tick, 1e6 / period,
                            period / float(margin or period),
                            "%g ns read" % margin if margin else "measured", machines))
    finally:
        p.command("TIMING", *SAFE_TIMING)
    notes.append("host profiles: %d verified bursts across %d machines' tightest loops"
                 % (checks, len(PROFILES)))
    return checks


GROUPS = {
    "walking": walking,
    "address": address_lines,
    "wrap": wrap,
    "phantom": phantom,
    "readahead": readahead,
    "register": register_vs_address,
    "latch": latch_matrix,
    "status": status_pointer,
    "glitch-write-start": glitch_write_start,
    "glitch-write-end": glitch_write_end,
    "glitch-read-start": glitch_read_start,
    "glitch-read-end": glitch_read_end,
    "conformance": conformance,
    "drive": drive_control,
    "mode-window": mode_window,
    "read-window": read_window,
    "write-window": write_window,
    "read-strobe": read_strobe,
    "write-strobe": write_strobe,
    "read-recovery": read_recovery,
    "write-recovery": write_recovery,
    "read-period": read_period,
    "write-period": write_period,
    "alternation": alternation,
    "read-burst": read_burst_floor,
    "write-burst": write_burst_floor,
    "display": display_load,
    "profiles": profiles,
}

# Conformance and rejection first, then the capability sweeps, then the host
# profiles. The sweeps come last of the three because they are the only group that
# leaves the probe holding a timing other than its safe one, and because the
# STRESS engine they ride writes over all of VRAM.
ORDER = ("walking", "address", "wrap", "readahead", "register", "latch", "status",
         "phantom", "conformance", "glitch-write-start", "glitch-write-end",
         "glitch-read-start", "glitch-read-end", "drive",
         "mode-window", "read-window", "write-window", "read-strobe", "write-strobe",
         "read-recovery", "write-recovery", "read-period", "write-period", "alternation",
         "read-burst", "write-burst", "display", "profiles")

# The sweeps return rows rather than a check count: they are measurements with a
# pass/fail read off them, and the rows are what belongs in the record.
SWEEPS = ("glitch-write-start", "glitch-write-end", "glitch-read-start",
          "glitch-read-end", "mode-window", "read-window", "read-strobe", "write-strobe",
          "read-recovery", "write-recovery", "read-period", "write-period",
          "alternation")

# Which way round the sweep reads. A rejection sweep finds the widest excursion
# the firmware turns away; a capability sweep finds the tightest cycle it keeps
# up with. Same curve, opposite sentence, and printing one as the other has
# already confused a reader once.
CAPABILITY = ("mode-window", "read-window", "read-strobe", "write-strobe", "read-recovery",
              "write-recovery", "read-period", "write-period", "alternation")

# Several sweeps under one name, each reporting itself as it goes.
CONTROLS = ("drive", "write-window", "read-burst", "write-burst", "display")


def run(p, only=None):
    failures, notes, checks, sweeps = [], [], 0, {}
    for name in ORDER:
        if only and name not in only:
            continue
        result = GROUPS[name](p, failures, notes)
        if name in SWEEPS:
            sweeps[name] = {"rows": result, "limits": measure(result)}
            checks += sum(r["trials"] for r in result)
        elif name in CONTROLS:
            sweeps[name] = result
            checks += sum(r["trials"] for leg in result for r in leg["rows"])
        else:
            checks += result
    out = outcome.property_result(failures, notes, checks)
    if sweeps:
        out["sweeps"] = sweeps
        cross_check(p, sweeps, out["notes"], failures)
        for name, sweep in sweeps.items():
            if name in CONTROLS:
                continue
            limits, rows = sweep["limits"], sweep["rows"]
            if name in CAPABILITY:
                out["notes"].append(
                    "  %-18s clean from %s, highest loss at %s, dead below %s"
                    % (name, _ns(limits["clean_from"]), _ns(limits["degrades_at"]),
                       _ns(limits["rejected_below"])))
                # Working at the narrowest the instrument can express is not a
                # measurement of the board, and must not be recorded as one.
                if limits["rejected_below"] is None and rows:
                    out["notes"].append(
                        "  %-18s AT THE INSTRUMENT: already works at %g ns, the tightest "
                        "the probe can drive, so the real limit is below the fixture"
                        % ("", rows[0]["ns"]))
                continue
            out["notes"].append(
                "  %-18s rejected up to %s, always taken from %s, p50 %s"
                % (name, _ns(limits["rejected_below"]), _ns(limits["taken_from"]),
                   _ns(limits["p50"])))
            # A sweep with no rejection floor found nothing the instrument can
            # generate that the firmware turns away. That is a different statement
            # from a low threshold and it should not read as one.
            if limits["rejected_below"] is None and rows:
                out["notes"].append(
                    "  %-18s REJECTS NOTHING: taken at %g ns, the narrowest the probe "
                    "can make, so the threshold is below the instrument rather than low"
                    % ("", rows[0]["ns"]))
    return out


def cross_check(p, sweeps, notes, failures):
    """Two sweeps that must agree, because they measure the same thing twice.

    The read strobe floor and the read data-valid window are reached by completely
    different routes - one is a sustained 4096-byte burst through the probe's
    transfer engine, the other a single cycle chopped into events - and they are
    tied together by one fact about the fixture: the transfer engine samples three
    ticks before the strobe rises. So the shortest strobe that works must be the
    window plus those three ticks. If the two ever disagree, one of them is
    measuring something other than what it says, and that is worth catching here
    rather than in a year's time when a number looks odd.
    """
    strobe = sweeps.get("read-strobe", {}).get("limits", {}).get("clean_from")
    window = sweeps.get("read-window", {}).get("limits", {}).get("clean_from")
    if strobe is None or window is None:
        return
    predicted = window + 3 * p.tick_ns
    notes.append("  cross-check: data valid %g ns after /CSR falls, the transfer engine "
                 "samples %g ns before the rise, so the shortest strobe should be %g ns "
                 "and measures %g ns - %+g ns, which is what the second read of a burst "
                 "costs over the first" % (window, 3 * p.tick_ns, predicted, strobe,
                                           strobe - predicted))
    if abs(strobe - predicted) > 5 * p.tick_ns:
        failures.append("read strobe floor %g ns and read window %g ns disagree by more "
                        "than five ticks; one of the two sweeps is not measuring what "
                        "it claims" % (strobe, window))


def _ns(value):
    return "-" if value is None else "%g ns" % value


def rig(p):
    """What measured these numbers.

    Absolute figures here carry the fixture's own latency and a nanosecond is only
    comparable against another taken the same way, so the probe's identity travels
    with the result rather than being remembered. A change of probe firmware then
    shows up as a step in the rig rather than a step in the target.
    """
    info = dict(p.info)
    info.pop("id", None)
    info.pop("ok", None)
    return info


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", default=None, metavar="PORT[:MODE]")
    ap.add_argument("--only", nargs="*", default=None, help=" ".join(ORDER))
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--save", default=None, metavar="TAG",
                    help="write the result to test/live/runs/hostbus-TAG.json")
    args = ap.parse_args()
    if args.list:
        for name in ORDER:
            print("%-18s %s" % (name, (GROUPS[name].__doc__ or "").split("\n")[0]))
        return 0
    where = spec(args.port)
    if not where:
        raise SystemExit("no probe port: pass --port COM10 or set LIVE9918_PROBE_CDC")
    with probe_module.opened(where) as p:
        result = run(p, args.only)
        result["rig"] = rig(p)
    if args.save:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "runs",
                            "hostbus-%s.json" % args.save)
        with open(path, "w") as f:
            json.dump(result, f, indent=1, sort_keys=True)
        print("saved %s" % path)
    return outcome.finish("Host bus protocol", result)


if __name__ == "__main__":
    sys.exit(main())
