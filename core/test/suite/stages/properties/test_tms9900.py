#!/usr/bin/env python3
"""The GPU's TMS9900, run the way the firmware runs one: uploaded and started.

core/test/tms9900 already holds this instruction set to these values, and on a board
it does reach the hand-written Thumb core - but as a UF2 that REPLACES the firmware,
flashed by hand and read over USB serial. So the core it proves is one running alone:
no GPU loop around it, no budget cutting a program in half between instructions, no
MPU armed, no renderer on the other core, and nothing that runs unless someone
remembers to flash it.

This asks the other question with the same cases. Does the shipping core still
execute correctly inside the shipping firmware, from a harness that runs it? Same
mechanism as the DMA property: write a program into VRAM over the wire, point the
GPU at it, read back what it left - so the only thing that differs between a board
run and a desktop run is which core executed it, which makes the pair a differential
test of the two.

**The sweeps compute their expectations.** Everything that writes a word sets LGT,
AGT and EQ from the result, and the arithmetic and the shifts add C and OV, so
`add_st`, `sub_st` and `shift_st` derive what a case must leave and the sweep checks
every operand pair against that rather than against a table someone typed. Where the
answer cannot be computed - what BLWP does with a context, where PIX puts a pixel -
the case states it.

Three things about this core will mislead anyone reading a failure:

    ST is the low byte    LGT=0x80 down to OP=0x04, not a real part's bits 15-10
    STST shifts it up     the word a program gets back is st << 8
    ST is not cleared     LI, MOV and the compares preserve C, OV and OP, and the
                          shifts preserve OV, so flags carry across instructions

The last one is why every program opens with the prologue `a()` emits: ST arrives
holding whatever the case before it left, and a sweep that predicts a whole status
word has to start from a known one. Flags a case wants are then seeded deliberately.

A sweep is one program, not one per case. Each iteration stores its result and its
status word into scratch and the whole sweep is read back at the end, which is what
keeps a hundred and fifty assertions to one round trip over SWD.
"""

import argparse
import struct
import sys

import suite.outcome as outcome
import suite.scoreboard as scoreboard
import suite.stages.gpu as gpu
from suite.access.backend import backend_args, open_backend

# All of it inside scoreboard.LOW_VRAM, so the board can show what it is doing while
# it does it - `scoreboard.start` is given the span and checks. DATA is the only part
# read back, so everything a case asserts about memory lives in it: results, the
# subroutines a branch case calls, and the contexts BLWP builds.
PROG, PROG_LEN = 0x0600, 0x0400
DATA, DATA_LEN = 0x0A00, 0x0200
SCRATCH = 0x0A00          # where a sweep leaves its results
AUX = 0x0B00              # subroutines, vectors, and the workspaces they name
STACK = 0x0B80            # R15 for the F18A stack ops, which grow downwards

# WP is hardcoded 0xFFFE by the GPU loop, which puts R0 at the top of VRAM and
# R1-R15 in the instance's `wrksp` overflow past 64KB. Both are readable.
WP = 0xFFFE

IDLE = 0x0340
MARK = 0xDEAD             # a marker a taken jump leaves standing and CLR knocks down

# ST as this core lays it out - the low byte, not a real part's bits 15-10
LGT, AGT, EQ, C, OV, OP = 0x80, 0x40, 0x20, 0x10, 0x08, 0x04

# VR32 is the bitmap base in 64-byte units, VR35 its width in pixels. PIX writes
# through them, so they are pointed at DATA rather than the 0x0000 they default to,
# which is the pattern table the scoreboard is drawn from.
BML_BASE_REG, BML_WIDTH_REG = 32, 35

JOB = gpu.Program(file=None, entry=PROG, credit=None, note=None, timeout=5.0)


class Asm:
    """One method per instruction, returning self so a program is one expression.
    Mirrors the encoders in core/test/tms9900/tms9900_test.c, deliberately: a case
    here should read against the one there."""

    def __init__(self):
        self.words = []

    def w(self, *ws):
        self.words.extend(ws)
        return self

    def li(self, r, imm):    return self.w(0x0200 | r, imm)
    def ai(self, r, imm):    return self.w(0x0220 | r, imm)
    def andi(self, r, imm):  return self.w(0x0240 | r, imm)
    def ori(self, r, imm):   return self.w(0x0260 | r, imm)
    def ci(self, r, imm):    return self.w(0x0280 | r, imm)
    def stwp(self, r):       return self.w(0x02A0 | r)
    def stst(self, r):       return self.w(0x02C0 | r)
    def lwpi(self, wp):      return self.w(0x02E0, wp)
    def limi(self, mask):    return self.w(0x0300, mask)
    def idle(self):          return self.w(IDLE)

    def blwp(self, addr):    return self.w(0x0420, addr)
    def b_ind(self, r):      return self.w(0x0450 | r)
    def b_abs(self, addr):   return self.w(0x0460, addr)
    def x(self, r):          return self.w(0x0480 | r)
    def clr(self, r):        return self.w(0x04C0 | r)
    def neg(self, r):        return self.w(0x0500 | r)
    def inv(self, r):        return self.w(0x0540 | r)
    def inc(self, r):        return self.w(0x0580 | r)
    def inct(self, r):       return self.w(0x05C0 | r)
    def dec(self, r):        return self.w(0x0600 | r)
    def dect(self, r):       return self.w(0x0640 | r)
    def bl_abs(self, addr):  return self.w(0x06A0, addr)
    def swpb(self, r):       return self.w(0x06C0 | r)
    def seto(self, r):       return self.w(0x0700 | r)
    def abs(self, r):        return self.w(0x0740 | r)

    def sra(self, r, c):     return self.w(0x0800 | ((c & 0xF) << 4) | r)
    def srl(self, r, c):     return self.w(0x0900 | ((c & 0xF) << 4) | r)
    def sla(self, r, c):     return self.w(0x0A00 | ((c & 0xF) << 4) | r)
    def src(self, r, c):     return self.w(0x0B00 | ((c & 0xF) << 4) | r)
    def slc(self, r, c):     return self.w(0x0E00 | ((c & 0xF) << 4) | r)

    def push(self, r):        return self.w(0x0D00 | r)
    def pop(self, r):         return self.w(0x0F00 | r)

    def jcc(self, cond, off): return self.w(0x1000 | ((cond & 0xF) << 8) | (off & 0xFF))

    def coc(self, rs, rd):   return self.w(0x2000 | (rd << 6) | rs)
    def czc(self, rs, rd):   return self.w(0x2400 | (rd << 6) | rs)
    def xor(self, rs, rd):   return self.w(0x2800 | (rd << 6) | rs)
    def pix(self, rs, rd):   return self.w(0x2C00 | (rd << 6) | rs)
    def mpy(self, rs, rd):   return self.w(0x3800 | (rd << 6) | rs)
    def div(self, rs, rd):   return self.w(0x3C00 | (rd << 6) | rs)

    def szc(self, rs, rd):   return self.w(0x4000 | (rd << 6) | rs)
    def szcb(self, rs, rd):  return self.w(0x5000 | (rd << 6) | rs)
    def s(self, rs, rd):     return self.w(0x6000 | (rd << 6) | rs)
    def sb(self, rs, rd):    return self.w(0x7000 | (rd << 6) | rs)
    def c(self, rs, rd):     return self.w(0x8000 | (rd << 6) | rs)
    def cb(self, rs, rd):    return self.w(0x9000 | (rd << 6) | rs)
    def a(self, rs, rd):     return self.w(0xA000 | (rd << 6) | rs)
    def ab(self, rs, rd):    return self.w(0xB000 | (rd << 6) | rs)
    def mov(self, rs, rd):   return self.w(0xC000 | (rd << 6) | rs)
    def movb(self, rs, rd):  return self.w(0xD000 | (rd << 6) | rs)
    def soc(self, rs, rd):   return self.w(0xE000 | (rd << 6) | rs)
    def socb(self, rs, rd):  return self.w(0xF000 | (rd << 6) | rs)

    def mov_ir(self, rs, rd):   return self.w(0xC000 | (rd << 6) | (1 << 4) | rs)
    def mov_pr(self, rs, rd):   return self.w(0xC000 | (rd << 6) | (3 << 4) | rs)
    def mov_ri(self, rs, rd):   return self.w(0xC000 | ((0x10 | rd) << 6) | rs)
    def mov_ar(self, addr, rd): return self.w(0xC000 | (rd << 6) | (2 << 4), addr)
    def mov_ra(self, rs, addr): return self.w(0xC000 | (0x20 << 6) | rs, addr)

    def store(self, r, addr):
        """The result and the status word one sweep slot leaves behind."""
        return self.stst(2).mov_ra(r, addr).mov_ra(2, addr + 2)

    def image(self, length):
        """Padded to `length` with IDLE, so a case cannot run into whatever the one
        before it left and a runaway stops rather than executing the display."""
        if len(self.words) * 2 > length:
            raise ValueError("%d bytes of program, over the %d it is loaded into"
                             % (len(self.words) * 2, length))
        words = self.words + [IDLE] * (length // 2 - len(self.words))
        return b"".join(struct.pack(">H", x & 0xFFFF) for x in words)


PROLOGUE = 4              # words: the A clears C and OV, the MOVB clears OP
BODY = PROG + PROLOGUE * 2
SCRUB = 14                # the register the prologue spends, so no case may seed it


def a():
    """A program that starts from a known status word. Nothing here clears ST
    wholesale - the arithmetic keeps OP, the shifts keep OV, and the GPU loop carries
    the whole byte from the last program it ran - so every case opens by doing it."""
    return Asm().li(SCRUB, 0).a(SCRUB, SCRUB).movb(SCRUB, SCRUB)


def flags(value, carry=False, overflow=False, parity=False):
    """LGT, AGT and EQ from a result - how every instruction that writes a word sets
    them - plus whichever of C, OV and OP the caller knows about."""
    v = value & 0xFFFF
    st = LGT if v else EQ
    if 0 < v < 0x8000:
        st |= AGT
    return st | (C if carry else 0) | (OV if overflow else 0) | (OP if parity else 0)


def add_st(dst, src):
    """C on unsigned carry out; OV when the operands agree in sign and the result
    does not agree with them."""
    r = (dst + src) & 0xFFFF
    return r, flags(r, carry=(dst + src) > 0xFFFF,
                    overflow=bool(~(dst ^ src) & (dst ^ r) & 0x8000))


def sub_st(dst, src):
    """dst - src. C is borrow-NOT here: set when dst is at or above src unsigned,
    which is the opposite sense to the one the name suggests."""
    r = (dst - src) & 0xFFFF
    return r, flags(r, carry=dst >= src,
                    overflow=bool((dst ^ src) & (dst ^ r) & 0x8000))


def shift_st(op, value, count):
    """The result, and the bit that fell out of it. SRA, SRL, SRC and SLC leave OV
    alone; only SLA sets it, and then from every sign change along the way rather
    than from the last one, so that one is simulated instead of solved."""
    n = count or 16
    v = value & 0xFFFF
    if op == "srl":
        r = (v >> n) if n < 16 else 0
        return r, flags(r, carry=bool((v >> (n - 1)) & 1))
    if op == "sra":
        fill = 0xFFFF if v & 0x8000 else 0
        r = ((v >> n) | (fill << (16 - n))) & 0xFFFF if n < 16 else fill
        return r, flags(r, carry=bool((v >> (n - 1)) & 1))
    if op == "src":
        m = n & 15
        r = ((v >> m) | (v << (16 - m))) & 0xFFFF if m else v
        return r, flags(r, carry=bool((v >> (n - 1)) & 1))
    if op == "slc":
        m = n & 15
        r = ((v << m) | (v >> (16 - m))) & 0xFFFF if m else v
        return r, flags(r, carry=bool((v << (n - 1)) & 0x8000))

    vv, changed = v, False
    for _ in range(n):
        top = vv & 0x8000
        vv = (vv << 1) & 0xFFFFFFFF
        changed = changed or top != (vv & 0x8000)
    return vv & 0xFFFF, flags(vv, carry=bool(vv & 0x10000), overflow=changed)


def R(n):
    return ("R", n)


def M(addr):
    return ("M", addr)


def Byte(addr):
    return ("B", addr)


class Case:
    """One program, the state it starts from, and what it must leave behind.

    `regs` seeds the workspace, `mem` seeds DATA, `regfile` seeds VDP registers, and
    `want` is every value checked after: registers by index, memory by address."""

    def __init__(self, name, code, regs=None, mem=None, want=None, regfile=None):
        if regs and SCRUB in regs:
            raise ValueError("%s seeds R%d, which the prologue spends" % (name, SCRUB))
        self.name = name
        self.code = code
        self.regs = regs or {}
        self.mem = mem or {}
        self.want = want or {}
        self.regfile = regfile or {}


def execute(t, case):
    """Run one case and return the registers and DATA as it left them."""
    t.vram(PROG, case.code.image(PROG_LEN))

    data = bytearray(DATA_LEN)
    for addr, value in case.mem.items():
        struct.pack_into(">H", data, addr - DATA, value & 0xFFFF)
    t.vram(DATA, bytes(data))

    workspace = bytearray(32)
    for reg, value in case.regs.items():
        struct.pack_into(">H", workspace, reg * 2, value & 0xFFFF)
    t.vram(WP, bytes(workspace))

    for reg, value in case.regfile.items():
        t.reg(reg, value)

    gpu.spin(t, JOB)
    return t.read(t.vdp + WP, 32), t.read(t.vdp + DATA, DATA_LEN)


def verify(t, case, fails):
    """Every expectation of one case. Returns how many were checked."""
    registers, data = execute(t, case)
    for (kind, at), wanted in sorted(case.want.items()):
        if kind == "R":
            got, where, digits = struct.unpack_from(">H", registers, at * 2)[0], "R%d" % at, 4
        elif kind == "M":
            got, where, digits = struct.unpack_from(">H", data, at - DATA)[0], "[%04X]" % at, 4
        else:
            got, where, digits = data[at - DATA], "[%04X].b" % at, 2
        if got != wanted:
            fails.append("%s: %s = %0*X, wanted %0*X"
                         % (case.name, where, digits, got, digits, wanted))
    return len(case.want)


# ---------------------------------------------------------------------------
# The groups, in the order core/test/tms9900 runs them
# ---------------------------------------------------------------------------

def data_transfer():
    return [
        Case("LI", a().li(0, 0x1234).idle(), want={R(0): 0x1234}),
        Case("MOV R,R", a().li(0, 0xABCD).mov(0, 1).idle(), want={R(1): 0xABCD}),
        Case("MOV *R,R", a().mov_ir(0, 1).idle(),
             regs={0: SCRATCH}, mem={SCRATCH: 0xBEEF}, want={R(1): 0xBEEF}),
        Case("MOV R,*R", a().mov_ri(0, 1).idle(),
             regs={0: 0x1234, 1: SCRATCH}, want={M(SCRATCH): 0x1234}),
        Case("MOV @a,R", a().mov_ar(SCRATCH, 2).idle(),
             mem={SCRATCH: 0xCAFE}, want={R(2): 0xCAFE}),
        Case("MOV R,@a", a().li(0, 0x5A5A).mov_ra(0, SCRATCH).idle(),
             want={M(SCRATCH): 0x5A5A}),
        Case("MOV *R+,R", a().mov_pr(0, 2).mov_pr(0, 3).idle(),
             regs={0: SCRATCH}, mem={SCRATCH: 0x1111, SCRATCH + 2: 0x2222},
             want={R(2): 0x1111, R(3): 0x2222, R(0): SCRATCH + 4}),
        # a byte operand on a register is its HIGH byte, at both ends
        Case("MOVB R,R", a().li(0, 0xAB00).movb(0, 1).idle(), want={R(1): 0xAB00}),
        Case("SWPB", a().li(0, 0x1234).swpb(0).idle(), want={R(0): 0x3412}),
    ]


def arithmetic():
    return [
        Case("A", a().li(0, 5).li(1, 7).a(0, 1).idle(), want={R(1): 12}),
        Case("S", a().li(0, 5).li(1, 7).s(0, 1).idle(), want={R(1): 2}),
        Case("AI", a().li(0, 0x1000).ai(0, 0x0234).idle(), want={R(0): 0x1234}),
        Case("NEG", a().li(0, 5).neg(0).idle(), want={R(0): 0xFFFB}),
        Case("ABS negative", a().li(0, 0xFFFB).abs(0).idle(), want={R(0): 5}),
        Case("ABS positive", a().li(0, 5).abs(0).idle(), want={R(0): 5}),
        # 0x8000 has no positive, so ABS raises OV and leaves it where it is
        Case("ABS 8000",
             a().li(0, 0x8000).li(1, 0).abs(0).jcc(0x9, 2).li(1, MARK).idle(),
             want={R(0): 0x8000, R(1): MARK}),
        Case("INC / INCT", a().li(0, 1).inc(0).inct(0).idle(), want={R(0): 4}),
        Case("DEC / DECT", a().li(0, 10).dec(0).dect(0).idle(), want={R(0): 7}),
        Case("AB", a().li(0, 0x0500).li(1, 0x0700).ab(0, 1).idle(), want={R(1): 0x0C00}),
        Case("SB", a().li(0, 0x0500).li(1, 0x0700).sb(0, 1).idle(), want={R(1): 0x0200}),
        # MPY and DIV span a register pair, the destination and the one after it
        Case("MPY", a().li(0, 300).li(2, 400).mpy(2, 0).idle(),
             want={R(0): (300 * 400) >> 16, R(1): (300 * 400) & 0xFFFF}),
        Case("DIV", a().li(0, 0).li(1, 1000).li(2, 7).div(2, 0).idle(),
             want={R(0): 1000 // 7, R(1): 1000 % 7}),
        # a divisor no larger than the high word cannot give a 16-bit quotient, so
        # the part raises OV and leaves the dividend alone
        Case("DIV overflow",
             a().li(0, 5).li(1, 0).li(2, 4).li(3, 0)
                .div(2, 0).jcc(0x9, 2).li(3, MARK).idle(),
             want={R(0): 5, R(1): 0, R(3): MARK}),
        Case("DIV clears OV",
             a().li(4, 0x7FFF).li(5, 1).a(4, 5)
                .li(0, 0).li(1, 40).li(2, 8).li(3, 0)
                .div(2, 0).jcc(0x9, 2).li(3, MARK).idle(),
             want={R(0): 5, R(1): 0, R(3): 0}),
    ]


ADD_PAIRS = ((0, 0), (1, 2), (0xFFFF, 1), (0x7FFF, 1), (0x8000, 0x8000),
             (0x8000, 0xFFFF), (0xFFFF, 0xFFFF), (0x1234, 0xEDCC),
             (0x4000, 0x4000), (0x0001, 0x7FFF), (0xC000, 0xC000), (0x00FF, 0xFF00))

SUB_PAIRS = ((0, 0), (7, 5), (5, 7), (0x8000, 1), (0, 1), (0x7FFF, 0xFFFF),
             (0x8000, 0x7FFF), (0xFFFF, 0xFFFF), (0x1234, 0x1234),
             (0x0000, 0x8000), (0x7FFF, 0x8000), (0xFF00, 0x00FF))


def alu_sweep(name, pairs, emit, model):
    """One program for a whole sweep: each pair leaves its result and its status word
    in scratch, so a dozen operand pairs are one run and one read."""
    p, want = a(), {}
    for i, (dst, src) in enumerate(pairs):
        emit(p.li(0, src).li(1, dst))
        p.store(1, SCRATCH + i * 4)
        result, st = model(dst, src)
        want[M(SCRATCH + i * 4)] = result
        want[M(SCRATCH + i * 4 + 2)] = st << 8
    return Case("%s, %d pairs" % (name, len(pairs)), p.idle(), want=want)


def add_sweep():
    return [alu_sweep("A", ADD_PAIRS, lambda p: p.a(0, 1), add_st)]


def sub_sweep():
    return [alu_sweep("S", SUB_PAIRS, lambda p: p.s(0, 1), sub_st)]


SHIFT_VALUES = (0x8001, 0x4002, 0xFFFF, 0x0001)
SHIFT_COUNTS = (1, 4, 8, 15)


def shifts():
    """Each shift against four values at four counts, and a last case for the count
    of zero, which means R0's low nibble - and sixteen when that is zero too."""
    cases = []
    for op in ("sra", "srl", "sla", "src", "slc"):
        p, want, slot = a(), {}, 0
        for value in SHIFT_VALUES:
            for count in SHIFT_COUNTS:
                p.li(1, value)
                getattr(p, op)(1, count)
                p.store(1, SCRATCH + slot * 4)
                result, st = shift_st(op, value, count)
                want[M(SCRATCH + slot * 4)] = result
                want[M(SCRATCH + slot * 4 + 2)] = st << 8
                slot += 1
        cases.append(Case("%s, %d cases" % (op.upper(), slot), p.idle(), want=want))

    p, want = a(), {}
    for slot, r0 in enumerate((3, 0)):
        p.li(0, r0).li(1, 0x8001).srl(1, 0)
        p.store(1, SCRATCH + slot * 4)
        result, st = shift_st("srl", 0x8001, r0 or 16)
        want[M(SCRATCH + slot * 4)] = result
        want[M(SCRATCH + slot * 4 + 2)] = st << 8
    cases.append(Case("SRL by R0, and by 16", p.idle(), want=want))
    return cases


def eq_case(name, code, taken):
    """COC and CZC touch nothing but EQ, so the answer is read with a jump rather
    than a STST - the rest of the status word is whatever the LI before them left."""
    return Case(name, code.jcc(0x3, 1).clr(2).idle(),
                want={R(2): MARK if taken else 0})


def logical():
    return [
        Case("CLR / SETO", a().li(0, 0xFFFF).clr(0).li(1, 0).seto(1).idle(),
             want={R(0): 0, R(1): 0xFFFF}),
        Case("INV", a().li(0, 0x0F0F).inv(0).idle(), want={R(0): 0xF0F0}),
        Case("ANDI", a().li(0, 0xFF00).andi(0, 0x0FF0).idle(), want={R(0): 0x0F00}),
        Case("ORI", a().li(0, 0xFF00).ori(0, 0x00FF).idle(), want={R(0): 0xFFFF}),
        Case("XOR", a().li(0, 0xFF00).li(1, 0x0FF0).xor(0, 1).idle(), want={R(1): 0xF0F0}),
        # SZC clears the bits the source has set, SOC sets them
        Case("SZC", a().li(0, 0x0F0F).li(1, 0xFFFF).szc(0, 1).idle(), want={R(1): 0xF0F0}),
        Case("SOC", a().li(0, 0x0F0F).li(1, 0xF000).soc(0, 1).idle(), want={R(1): 0xFF0F}),
        Case("SZCB", a().li(0, 0x0F00).li(1, 0xFF00).szcb(0, 1).idle(), want={R(1): 0xF000}),
        Case("SOCB", a().li(0, 0x0F00).li(1, 0xF000).socb(0, 1).idle(), want={R(1): 0xFF00}),
        eq_case("COC, all present",
                a().li(0, 0x0F0F).li(1, 0xFFFF).li(2, MARK).coc(0, 1), True),
        eq_case("COC, one missing",
                a().li(0, 0x0F0F).li(1, 0x0F0E).li(2, MARK).coc(0, 1), False),
        eq_case("CZC, all clear",
                a().li(0, 0x00FF).li(1, 0xFF00).li(2, MARK).czc(0, 1), True),
        eq_case("CZC, one set",
                a().li(0, 0x00FF).li(1, 0xFF01).li(2, MARK).czc(0, 1), False),
    ]


def compare_branch():
    """C compares source against destination, which is the opposite way round to the
    obvious reading of the encoding, so both directions are here."""
    return [
        Case("CI equal", a().li(0, 5).ci(0, 5).stst(1).idle(), want={R(1): EQ << 8}),
        Case("CI greater", a().li(0, 7).ci(0, 5).stst(1).idle(),
             want={R(1): (LGT | AGT) << 8}),
        Case("CI less", a().li(0, 3).ci(0, 5).stst(1).idle(), want={R(1): 0}),
        Case("C, source greater", a().li(0, 7).li(1, 5).c(0, 1).stst(2).idle(),
             want={R(2): (LGT | AGT) << 8}),
        Case("C, source less", a().li(0, 3).li(1, 5).c(0, 1).stst(2).idle(),
             want={R(2): 0}),
        # CB carries the parity of the SOURCE byte, not of a result - so 0x01 is odd
        Case("CB equal", a().li(0, 0x0100).li(1, 0x0100).cb(0, 1).stst(2).idle(),
             want={R(2): (EQ | OP) << 8}),
        # R0 counts ten passes down and R1 counts them up
        Case("counted loop", a().li(0, 10).li(1, 0).inc(1).dec(0).jcc(0x6, -3).idle(),
             want={R(0): 0, R(1): 10}),
        Case("BL @a, RT",
             a().bl_abs(AUX).li(0, 0x1111).idle(),
             mem={AUX: 0x0202, AUX + 2: 0x2222, AUX + 4: 0x045B},
             want={R(0): 0x1111, R(2): 0x2222, R(11): BODY + 4}),
        Case("B *R", a().li(1, AUX).b_ind(1).idle(),
             mem={AUX: 0x0203, AUX + 2: 0x3333, AUX + 4: IDLE},
             want={R(3): 0x3333}),
        Case("B @a", a().b_abs(AUX).idle(),
             mem={AUX: 0x0204, AUX + 2: 0x4444, AUX + 4: IDLE},
             want={R(4): 0x4444}),
    ]


# nibble, name, and whether a status word takes the jump
CONDITIONS = (
    (0x0, "JMP", lambda st: True),
    (0x1, "JLT", lambda st: not st & (AGT | EQ)),
    (0x2, "JLE", lambda st: not st & LGT or bool(st & EQ)),
    (0x3, "JEQ", lambda st: bool(st & EQ)),
    (0x4, "JHE", lambda st: bool(st & (LGT | EQ))),
    (0x5, "JGT", lambda st: bool(st & AGT)),
    (0x6, "JNE", lambda st: not st & EQ),
    (0x7, "JNC", lambda st: not st & C),
    (0x8, "JOC", lambda st: bool(st & C)),
    (0x9, "JNO", lambda st: not st & OV),
    (0xA, "JL", lambda st: not st & (LGT | EQ)),
    (0xB, "JH", lambda st: bool(st & LGT) and not st & EQ),
    (0xC, "JOP", lambda st: bool(st & OP)),
)

# the operand to compare against five, and the status word that leaves
COMPARES = ((3, 0), (5, EQ), (7, LGT | AGT))

# C, OV and OP all survive a compare, so a seed set once holds for a whole program.
# These are chosen for the flag each raises, not for the result each computes.
SEEDS = (
    ("no flags", 0, lambda p: p),
    ("carry", C, lambda p: p.li(4, 0xFFFF).li(5, 1).a(4, 5)),
    ("overflow", OV, lambda p: p.li(4, 0x7FFF).li(5, 1).a(4, 5)),
    ("parity", OP, lambda p: p.li(4, 0x0100).movb(4, 4)),
)


def jumps():
    """Every condition against every comparison, under each flag a compare cannot
    set for itself. The marker is loaded BEFORE the compare and knocked down only
    when the jump is not taken, so nothing in between can disturb ST - CLR does not
    touch it, which is what makes it the thing to knock the marker down with."""
    cases = []
    for seed_name, seed_st, seed in SEEDS:
        p, want, slot = seed(a()), {}, 0
        for operand, compare_st in COMPARES:
            for cond, _, takes in CONDITIONS:
                p.li(1, MARK).li(0, operand).ci(0, 5).jcc(cond, 1).clr(1)
                p.mov_ra(1, SCRATCH + slot * 2)
                want[M(SCRATCH + slot * 2)] = MARK if takes(seed_st | compare_st) else 0
                slot += 1
        cases.append(Case("CI, 13 conditions, %s" % seed_name, p.idle(), want=want))
    return cases


def misc():
    return [
        Case("STWP", a().stwp(0).idle(), want={R(0): WP}),
        Case("STST", a().li(0, 0).ci(0, 0).stst(1).idle(), want={R(1): EQ << 8}),
        # X executes the word its operand holds, so an R0 holding an LI runs one and
        # the immediate that follows the X in the stream is the one that LI takes
        Case("X", a().li(0, 0x0202).x(0).w(0x9999).idle(), want={R(2): 0x9999}),
        # LWPI moves the workspace, so what R5 means afterwards is a memory address
        Case("LWPI", a().lwpi(AUX).li(5, 0x7777).idle(), want={M(AUX + 10): 0x7777}),
        # there is no interrupt mask here, but the immediate still has to be eaten:
        # a LIMI that left it would execute its own operand as an instruction
        Case("LIMI", a().limi(3).li(0, 0x1234).idle(), want={R(0): 0x1234}),
    ]


# RET and CALL, canonically and at the far end of the range each owns
RET, RET_TOP = 0x0C00, 0x0C7E
CALL_ABS, CALL_ABS_TOP = 0x0CA0, 0x0CE0


def call_ret(name, call, ret):
    """A call into AUX that loads R3 and returns, under whichever spelling of the two
    the caller wants to prove."""
    return Case(name,
                a().li(15, STACK).w(call, AUX).li(0, 0x1111).idle(),
                mem={AUX: 0x0203, AUX + 2: 0x4321, AUX + 4: ret},
                want={R(0): 0x1111, R(3): 0x4321, R(15): STACK, M(STACK): BODY + 8})


def stack():
    """The F18A's own four. R15 is the stack pointer and the address written is the
    one BEFORE the decrement, which is the half of the convention that is easy to get
    wrong: a push and a pop have to agree about where the top is.

    The second pair of each is the encoding rather than the behaviour. f18a_gpu.vhd
    selects this whole group on ir(5 to 7) - bits 10:8 - and only the RET/CALL arm
    reads a further bit, ir(8), which is bit 7 because ir is declared (0 to 15). So
    bit 7 alone separates RET from CALL and bit 6 is decoded by nobody. A core that
    narrows either page still runs everything an assembler emits, which is why this
    has to be asserted rather than noticed."""
    return [
        Case("PUSH / POP",
             a().li(15, STACK).li(1, 0x1234).push(1).li(2, 0).pop(2).idle(),
             want={R(2): 0x1234, R(15): STACK, M(STACK): 0x1234}),
        Case("PUSH / POP, undecoded bits",
             a().li(15, STACK)
                .li(1, 0x1234).w(0x0D40 | 1)
                .li(2, 0x5678).w(0x0D80 | 2)
                .li(3, 0).w(0x0FC0 | 3)
                .li(4, 0).w(0x0F40 | 4).idle(),
             want={R(3): 0x5678, R(4): 0x1234, R(15): STACK,
                   M(STACK): 0x1234, M(STACK - 2): 0x5678}),
        call_ret("CALL @a / RET", CALL_ABS, RET),
        call_ret("CALL / RET, top of each", CALL_ABS_TOP, RET_TOP),
    ]


def pix():
    """VR32 is the bitmap base in 64-byte units and VR35 its width in pixels, at two
    bits a pixel - so a row is ceil(width / 4) bytes. Rs carries x in the high byte
    and y in the low one; Rd is a colour, or 0x4000 to ask only for the address."""
    return [
        # width 10 is a stride of 3, so (x=5, y=3) is byte 3*3 + 5//4 = 10
        Case("PIX address", a().li(1, 0x0503).li(2, 0x4000).pix(1, 2).idle(),
             regfile={BML_BASE_REG: SCRATCH // 0x40, BML_WIDTH_REG: 10},
             want={R(2): SCRATCH + 10}),
        # base 0x3FC0 at a stride of one byte: row 100 leaves 16KB, and it wraps
        Case("PIX wraps at 16KB", a().li(1, 0x0064).li(2, 0x4000).pix(1, 2).idle(),
             regfile={BML_BASE_REG: 0xFF, BML_WIDTH_REG: 4},
             want={R(2): (0x3FC0 + 100) & 0x3FFF}),
        # x=5 is pixel 1 of its byte, and two bits a pixel puts colour 3 at 0x30
        Case("PIX write", a().li(1, 0x0503).li(2, 0x0003).pix(1, 2).idle(),
             regfile={BML_BASE_REG: SCRATCH // 0x40, BML_WIDTH_REG: 10},
             want={Byte(SCRATCH + 10): 0x30}),
    ]


def blwp():
    """BLWP takes a two-word vector - a new WP and a new PC - and saves the old WP,
    PC and ST into R13, R14 and R15 of the context it has just entered. RTWP puts all
    three back. ST lands in the high byte of R15, which is where STST puts it too."""
    new_wp, entry = AUX + 0x20, AUX + 0x40
    return [
        Case("BLWP / RTWP",
             a().blwp(AUX).li(0, 0x1111).idle(),
             mem={AUX: new_wp, AUX + 2: entry,
                  entry: 0x0203, entry + 2: 0xD0AE, entry + 4: 0x0380},
             want={R(0): 0x1111,
                   M(new_wp + 6): 0xD0AE,
                   M(new_wp + 26): WP,
                   M(new_wp + 28): BODY + 4,
                   M(new_wp + 30): EQ << 8}),
    ]


def stress():
    """Thousands of instructions through one loop rather than one instruction at a
    time, which is the only thing here that would catch a core getting every opcode
    right and its own loop wrong."""
    fib = a().li(0, 10).li(1, 0).li(2, 1)
    fib.mov(2, 3).a(1, 2).mov(3, 1).dec(0).jcc(0x6, -5)

    total = a().li(0, 1000).li(1, 0).li(2, 0)
    total.inc(2).a(2, 1).dec(0).jcc(0x6, -4)

    return [
        Case("fibonacci(10)", fib.idle(), want={R(0): 0, R(1): 55, R(2): 89}),
        Case("sum of 1..1000", total.idle(),
             want={R(0): 0, R(1): sum(range(1001)) & 0xFFFF, R(2): 1000}),
    ]


GROUPS = (
    ("DATA TRANSFER", data_transfer),
    ("ARITHMETIC", arithmetic),
    ("ADD SWEEP", add_sweep),
    ("SUB SWEEP", sub_sweep),
    ("SHIFTS", shifts),
    ("LOGICAL", logical),
    ("COMPARE + BRANCH", compare_branch),
    ("JUMP CONDITIONS", jumps),
    ("MISC", misc),
    ("F18A STACK", stack),
    ("F18A PIX", pix),
    ("BLWP / RTWP", blwp),
    ("STRESS", stress),
)


def run(t):
    fails, notes, checks = [], [], 0
    t.unlock()
    board = scoreboard.start(t, "GPU TMS9900", (PROG, DATA + DATA_LEN))
    for title, build in GROUPS:
        board.running(title)
        before, count = len(fails), 0
        for case in build():
            count += verify(t, case, fails)
        wrong = len(fails) - before
        checks += count
        notes.append("%-18s %4d checks  %s"
                     % (title, count, "OK" if not wrong else "%d wrong" % wrong))
        board.verdict(not wrong)
    board.summary()
    return outcome.property_result(fails, notes, checks)


def main():
    ap = argparse.ArgumentParser()
    backend_args(ap)
    args = ap.parse_args()
    with open_backend(args) as t:
        return outcome.finish("GPU TMS9900", run(t))


if __name__ == "__main__":
    sys.exit(main())
