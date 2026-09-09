/**
 * \file
 * \brief pico9918-core - the debugger surface
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * What is under test is nowhere near the copy loops. It is that the regions land where the
 * union actually puts its fields; that region, read and write agree about where each one
 * stops, since a pane splitting bulk work on those boundaries has to land on the same
 * bytes the transfer does; and that the register write does the four things it promises
 * and not a fifth.
 *
 * That last one is checked twice. Once case by case - R30 on a locked device landing at
 * R30 rather than R6, R55 not arming a program, R57 not moving the unlock latch - and
 * once by snapshotting the whole instance across a write and asserting that exactly two
 * bytes moved. The second is the one that catches state nobody thought to name, including
 * state added to pico9918_t after this was written.
 *
 * The edges are here because every one of them is a coin-flip an implementation would
 * otherwise settle silently: zero length, a null buffer, exactly at the end, past it, and
 * a length that would run off the end of the map.
 */

#include "impl/pico9918_priv.h"
#include "pico9918_debug.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void fail(const char* what, unsigned long wanted, unsigned long got)
{
  ++failures;
  printf("  FAIL %s: want %lx got %lx\n", what, wanted, got);
}

static void expectRegion(const char* what, uint32_t addr, uint32_t wantFlags, uint32_t wantEnd)
{
  uint32_t end   = 0xffffffffu;
  uint32_t flags = pico9918_debug_region(addr, &end);

  if (flags != wantFlags) fail(what, wantFlags, flags);
  if (end != wantEnd) fail(what, wantEnd, end);
}

int main(void)
{
  const uint32_t size = pico9918_gpu_mem_size();

  pico9918_init();

  /* 1. the regions are where the union puts them, not where a table says they are. Each
        is probed at its first byte and at its last, so an off-by-one in either bound of
        the run shows up as a wrong end rather than passing on the midpoint. */
  expectRegion("base-first", 0, PICO9918_DEBUG_READABLE | PICO9918_DEBUG_WRITABLE, 0x5000);
  expectRegion("base-last", 0x4fff, PICO9918_DEBUG_READABLE | PICO9918_DEBUG_WRITABLE, 0x5000);
  expectRegion("pram-first", 0x5000,
               PICO9918_DEBUG_READABLE | PICO9918_DEBUG_WRITABLE | PICO9918_DEBUG_PALETTE, 0x6000);
  expectRegion("pram-last", 0x5fff,
               PICO9918_DEBUG_READABLE | PICO9918_DEBUG_WRITABLE | PICO9918_DEBUG_PALETTE, 0x6000);
  expectRegion("regs-first", 0x6000, PICO9918_DEBUG_READABLE | PICO9918_DEBUG_REGISTERS, 0x6040);
  expectRegion("regs-last", 0x603f, PICO9918_DEBUG_READABLE | PICO9918_DEBUG_REGISTERS, 0x6040);
  expectRegion("gram2-first", 0x6040, PICO9918_DEBUG_READABLE | PICO9918_DEBUG_WRITABLE, 0xb000);
  expectRegion("gram2-last", 0xafff, PICO9918_DEBUG_READABLE | PICO9918_DEBUG_WRITABLE, 0xb000);
  expectRegion("status-first", 0xb000, PICO9918_DEBUG_READABLE | PICO9918_DEBUG_STATUS, 0xb010);
  expectRegion("status-last", 0xb00f, PICO9918_DEBUG_READABLE | PICO9918_DEBUG_STATUS, 0xb010);
  expectRegion("gram4-first", 0xb010, PICO9918_DEBUG_READABLE | PICO9918_DEBUG_WRITABLE, size);
  expectRegion("wrksp-last", size - 1, PICO9918_DEBUG_READABLE | PICO9918_DEBUG_WRITABLE, size);

  /* and the workspace overflow is inside the map, which is the whole reason the map is
     not 64KB - R1-R15 live past 0xFFFF and a register pane has to reach them */
  if (size <= 0x10000) fail("map-stops-at-64k", 0x10001, size);

  /* 2. past the end terminates a walk rather than looping: 0 flags, end == addr */
  expectRegion("past-end", size, 0, size);
  expectRegion("far-past-end", 0xfffffff0u, 0, 0xfffffff0u);

  /* 3. a walk covers the map exactly once - every byte in one region, no gap, no
        overlap, and it terminates. This is the check that a hand-written table cannot
        pass by accident. */
  uint32_t at = 0, regions = 0;
  while (at < size)
  {
    uint32_t end = 0;
    if (pico9918_debug_region(at, &end) == 0) fail("walk-hole", 1, at);
    if (end <= at)
    {
      fail("walk-stuck", at + 1, end);
      break;
    }
    at = end;
    ++regions;
  }
  if (at != size) fail("walk-end", size, at);
  if (regions != 6) fail("walk-regions", 6, regions);

  /* a null end is legal, because a caller that only wants the flags should not have to
     invent somewhere to put a number it will not read */
  if (pico9918_debug_region(0x5000, NULL) !=
      (PICO9918_DEBUG_READABLE | PICO9918_DEBUG_WRITABLE | PICO9918_DEBUG_PALETTE))
    fail("region-null-end", 1, 0);

  /* 4. the span reads the backing state, and reads the same bytes the scalar accessor
        does - one sentinel per region, written straight into the instance so the read is
        not being compared against the path that wrote it */
  static const uint32_t probes[] = {0x0000, 0x3fff, 0x4000, 0x5000, 0x6000,
                                    0x6040, 0xb000, 0xb010, 0xffff};
  for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i)
  {
    uint8_t got = 0;

    ((uint8_t*)&tms9918->vram)[probes[i]] = (uint8_t)(0x5a + i);
    if (pico9918_debug_read(PICO9918_INST probes[i], &got, 1) != 1) fail("probe-short", 1, 0);
    if (got != (uint8_t)(0x5a + i)) fail("probe-value", (uint8_t)(0x5a + i), got);
    if (got != pico9918_gpu_mem_value(PICO9918_INST probes[i]))
      fail("probe-vs-scalar", got, pico9918_gpu_mem_value(PICO9918_INST probes[i]));
  }

  /* the workspace overflow reads through the span too, which walking vram.bytes cannot
     do - that array stops at 0xFFFF */
  tms9918->vram.map.wrksp[4] = 0xa5;
  {
    uint8_t got = 0;

    if (pico9918_debug_read(PICO9918_INST 0x10004, &got, 1) != 1) fail("wrksp-short", 1, 0);
    if (got != 0xa5) fail("wrksp-value", 0xa5, got);
  }

  /* 5. a span crossing a region boundary is NOT short - only the write refuses windows,
        and a memory pane reading 0x5FF0-0x600F must see all 32 bytes */
  {
    uint8_t buf[32];

    memset(buf, 0, sizeof(buf));
    if (pico9918_debug_read(PICO9918_INST 0x5ff0, buf, sizeof(buf)) != sizeof(buf))
      fail("read-across-window", (unsigned long)sizeof(buf), 0);
  }

  /* 6. the edges, each of which is a decision rather than a consequence */
  {
    uint8_t buf[16];

    if (pico9918_debug_read(PICO9918_INST 0, buf, 0) != 0) fail("read-zero-len", 0, 1);
    if (pico9918_debug_read(PICO9918_INST 0, NULL, sizeof(buf)) != 0) fail("read-null", 0, 1);
    if (pico9918_debug_read(PICO9918_INST size, buf, sizeof(buf)) != 0) fail("read-at-end", 0, 1);
    if (pico9918_debug_read(PICO9918_INST size + 1, buf, sizeof(buf)) != 0)
      fail("read-past-end", 0, 1);

    /* short rather than wrapped: four bytes left, sixteen asked for */
    const size_t got = pico9918_debug_read(PICO9918_INST size - 4, buf, sizeof(buf));
    if (got != 4) fail("read-short-at-end", 4, (unsigned long)got);
  }

  /* 7. the span write lands in the backing state, and stops at the first byte it will
        not write rather than skipping it and carrying on */
  {
    uint8_t buf[32];

    memset(buf, 0x77, sizeof(buf));
    if (pico9918_debug_write(PICO9918_INST 0x1000, buf, sizeof(buf)) != sizeof(buf))
      fail("write-vram", (unsigned long)sizeof(buf), 0);
    if (((uint8_t*)&tms9918->vram)[0x1000] != 0x77) fail("write-vram-first", 0x77, 0);
    if (((uint8_t*)&tms9918->vram)[0x101f] != 0x77) fail("write-vram-last", 0x77, 0);

    /* into the register window from below: short at 0x6000, and the file is untouched */
    ((uint8_t*)&tms9918->vram)[0x6000] = 0x11;
    if (pico9918_debug_write(PICO9918_INST 0x5ff0, buf, sizeof(buf)) != 0x10)
      fail("write-into-regs", 0x10, 0);
    if (((uint8_t*)&tms9918->vram)[0x6000] != 0x11) fail("write-regs-leaked", 0x11, 0x77);

    /* starting inside it: nothing at all, which is how a caller tells it was refused */
    if (pico9918_debug_write(PICO9918_INST 0x6010, buf, sizeof(buf)) != 0)
      fail("write-in-regs", 0, 1);

    /* and the status window the same way, from both sides */
    ((uint8_t*)&tms9918->vram)[0xb000] = 0x22;
    if (pico9918_debug_write(PICO9918_INST 0xaff0, buf, sizeof(buf)) != 0x10)
      fail("write-into-status", 0x10, 0);
    if (((uint8_t*)&tms9918->vram)[0xb000] != 0x22) fail("write-status-leaked", 0x22, 0x77);
    if (pico9918_debug_write(PICO9918_INST 0xb008, buf, sizeof(buf)) != 0)
      fail("write-in-status", 0, 1);

    /* a run that crosses two writable regions is NOT short - PRAM abuts plain GRAM and
       they differ only in what a write there owes afterwards */
    if (pico9918_debug_write(PICO9918_INST 0x4ff0, buf, sizeof(buf)) != sizeof(buf))
      fail("write-across-pram", (unsigned long)sizeof(buf), 0);

    /* the edges, same set as the read */
    if (pico9918_debug_write(PICO9918_INST 0, buf, 0) != 0) fail("write-zero-len", 0, 1);
    if (pico9918_debug_write(PICO9918_INST 0, NULL, sizeof(buf)) != 0) fail("write-null", 0, 1);
    if (pico9918_debug_write(PICO9918_INST size, buf, sizeof(buf)) != 0) fail("write-at-end", 0, 1);

    const size_t tail = pico9918_debug_write(PICO9918_INST size - 4, buf, sizeof(buf));
    if (tail != 4) fail("write-short-at-end", 4, (unsigned long)tail);
  }

  /* 8. and a write into PRAM leaves the palette owing a republish, or the edit takes and
        the picture does not change */
  {
    const uint8_t entry[2] = {0x0f, 0x0a};

    tms9918->palDirty = 0;
    if (pico9918_debug_write(PICO9918_INST 0x5000, entry, sizeof(entry)) != sizeof(entry))
      fail("write-pram", 2, 0);
    if (!tms9918->palDirty) fail("write-pram-not-dirty", 1, 0);

    /* a write that misses PRAM must NOT raise it, or the flag says nothing */
    tms9918->palDirty = 0;
    if (pico9918_debug_write(PICO9918_INST 0x1000, entry, sizeof(entry)) != sizeof(entry))
      fail("write-vram-again", 2, 0);
    if (tms9918->palDirty) fail("write-vram-dirtied-palette", 0, 1);

    /* and a refused write raises nothing either, having done nothing */
    tms9918->palDirty = 0;
    if (pico9918_debug_write(PICO9918_INST 0x6000, entry, sizeof(entry)) != 0)
      fail("write-regs-again", 0, 1);
    if (tms9918->palDirty) fail("write-regs-dirtied-palette", 0, 1);
  }

  /* 9. the register read is the file's own byte. R30 on a LOCKED device is the case that
        matters: the guest's read folds it to three bits and answers R6, and a pane
        showing that is showing the wrong register with no way to tell. */
  TMS_REGISTER(tms9918, 6)  = 0x66;
  TMS_REGISTER(tms9918, 30) = 0x30;
  if (tms9918->isUnlocked) fail("locked-precondition", 0, 1);
  if (pico9918_debug_reg(PICO9918_INST 30) != 0x30)
    fail("reg-locked-30", 0x30, pico9918_debug_reg(PICO9918_INST 30));
  if (pico9918_reg_value(PICO9918_INST (pico9918_register_t)30) != 0x66)
    fail("reg-guest-folds", 0x66, pico9918_reg_value(PICO9918_INST (pico9918_register_t)30));
  if (pico9918_debug_reg(PICO9918_INST 64) != 0) fail("reg-out-of-range", 0, 1);

  /* and it agrees with the same byte through the map, which is the other way to it */
  if (pico9918_debug_reg(PICO9918_INST 30) != pico9918_gpu_mem_value(PICO9918_INST 0x6000 + 30))
    fail("reg-vs-map", 0x30, pico9918_gpu_mem_value(PICO9918_INST 0x6000 + 30));

  /* 10. the palette comes back in host order, undoing the big-endian storage - written
         through the library's own path so the test is not just bswapping its own bswap */
  tms9918->vram.map.pram[7] = __builtin_bswap16(0x0abc);
  if (pico9918_debug_palette(PICO9918_INST 7) != 0x0abc)
    fail("palette-host-order", 0x0abc, pico9918_debug_palette(PICO9918_INST 7));
  if (pico9918_debug_palette(PICO9918_INST 64) != 0) fail("palette-out-of-range", 0, 1);

  /* and it round-trips with the span write, which is what a palette editor does. The
     order in the map is the low byte first and it holds R, the second holding GB - so
     0x0acb is stored 0a cb, not cb 0a. */
  {
    const uint8_t entry[2] = {0x0a, 0xcb};

    if (pico9918_debug_write(PICO9918_INST 0x5000 + 7 * 2, entry, sizeof(entry)) != 2)
      fail("palette-write", 2, 0);
    if (pico9918_debug_palette(PICO9918_INST 7) != 0x0acb)
      fail("palette-round-trip", 0x0acb, pico9918_debug_palette(PICO9918_INST 7));
  }

  /* 11. the address latch is EFFECTIVE. A 4K chip with R1's 16K bit clear permutes the
         address rather than masking it, so a mask would name a different byte - this is
         the case the plain mask gets wrong. */
  tms9918->currentAddress = 0x1234;
  if (pico9918_debug_vram_address(PICO9918_INST_ONLY) !=
      (uint16_t)pico9918_cpu_vram_addr_impl(PICO9918_INST 0x1234))
    fail("vram-addr", pico9918_cpu_vram_addr_impl(PICO9918_INST 0x1234),
         pico9918_debug_vram_address(PICO9918_INST_ONLY));

  /* and the counter runs past the bus width between accesses, where the raw field is not
     an address at all */
  tms9918->currentAddress = 0x1ffff;
  if (pico9918_debug_vram_address(PICO9918_INST_ONLY) > 0xffff) fail("vram-addr-wide", 0, 1);

  /* 12. the GPU PC moves without the program starting, and without the register file or
         the armed state moving with it */
  {
    const uint8_t r54 = pico9918_debug_reg(PICO9918_INST PICO9918_REG_GPU_PC_MSB);
    const uint8_t r55 = pico9918_debug_reg(PICO9918_INST PICO9918_REG_GPU_PC_LSB);

    tms9918->restart = 0;
    pico9918_debug_gpu_set_pc(PICO9918_INST 0x2468);
    if (pico9918_gpu_pc(PICO9918_INST_ONLY) != 0x2468)
      fail("set-pc", 0x2468, pico9918_gpu_pc(PICO9918_INST_ONLY));
    if (pico9918_debug_gpu_armed(PICO9918_INST_ONLY)) fail("set-pc-armed-it", 0, 1);
    if (pico9918_debug_reg(PICO9918_INST PICO9918_REG_GPU_PC_MSB) != r54) fail("set-pc-r54", r54, 0);
    if (pico9918_debug_reg(PICO9918_INST PICO9918_REG_GPU_PC_LSB) != r55) fail("set-pc-r55", r55, 0);

    /* odd is masked even, the way the register path masks it */
    pico9918_debug_gpu_set_pc(PICO9918_INST 0x2469);
    if (pico9918_gpu_pc(PICO9918_INST_ONLY) != 0x2468)
      fail("set-pc-odd", 0x2468, pico9918_gpu_pc(PICO9918_INST_ONLY));

    /* and an armed program stays armed - this redirects one, it does not disarm one */
    tms9918->restart = 1;
    if (!pico9918_debug_gpu_armed(PICO9918_INST_ONLY)) fail("armed", 1, 0);
    pico9918_debug_gpu_set_pc(PICO9918_INST 0x1000);
    if (!pico9918_debug_gpu_armed(PICO9918_INST_ONLY)) fail("set-pc-disarmed-it", 1, 0);
    tms9918->restart = 0;
  }

  /* 13. the register write is the physical store. Each case is one postcondition, and
         the preserved half is checked by snapshotting the instance and diffing it - a
         list of fields written out by hand is a list that goes stale. */
  {
    /* R30 on a LOCKED device: the bug that removed the first attempt at a public
       register write, and the one a "write R57, check R1" test cannot see */
    TMS_REGISTER(tms9918, 6) = 0x66;
    if (tms9918->isUnlocked) fail("write-locked-precondition", 0, 1);
    if (!pico9918_debug_reg_write(PICO9918_INST 30, 0x3c)) fail("write-reg-30", 1, 0);
    if (pico9918_debug_reg(PICO9918_INST 30) != 0x3c)
      fail("write-reg-30-value", 0x3c, pico9918_debug_reg(PICO9918_INST 30));
    if (pico9918_debug_reg(PICO9918_INST 6) != 0x66)
      fail("write-reg-30-hit-6", 0x66, pico9918_debug_reg(PICO9918_INST 6));

    /* above the file: nothing happens and it says so */
    TMS_REGISTER(tms9918, 0) = 0x0d;
    if (pico9918_debug_reg_write(PICO9918_INST 64, 0xff)) fail("write-reg-64", 0, 1);
    if (pico9918_debug_reg(PICO9918_INST 0) != 0x0d) fail("write-reg-64-wrapped", 0x0d, 0);

    /* the palette is left owing a republish */
    tms9918->palDirty = 0;
    pico9918_debug_reg_write(PICO9918_INST 7, 0x21);
    if (!tms9918->palDirty) fail("write-reg-not-dirty", 1, 0);

    /* and the cached mode is current WITHOUT a scanline having run, which is the case
       pico9918_frame.c hits: it reads the mode before it calls the scanline that would
       refresh it. Both mode registers, since either can change the answer. */
    pico9918_debug_reg_write(PICO9918_INST TMS_REG_0, 0);
    pico9918_debug_reg_write(PICO9918_INST TMS_REG_1, TMS_R1_MODE_TEXT | TMS_R1_DISP_ACTIVE);
    if (pico9918_display_mode(PICO9918_INST_ONLY) != TMS_MODE_TEXT)
      fail("write-reg-mode-stale", TMS_MODE_TEXT, pico9918_display_mode(PICO9918_INST_ONLY));
    pico9918_debug_reg_write(PICO9918_INST TMS_REG_1, TMS_R1_DISP_ACTIVE);
    if (pico9918_display_mode(PICO9918_INST_ONLY) != TMS_MODE_GRAPHICS_I)
      fail("write-reg-mode-back", TMS_MODE_GRAPHICS_I, pico9918_display_mode(PICO9918_INST_ONLY));

    /* R0's M4 is honoured while still locked, so it moves the mode from the other side */
    pico9918_debug_reg_write(PICO9918_INST TMS_REG_0, TMS_R0_MODE_TEXT_80);
    if (pico9918_display_mode(PICO9918_INST_ONLY) != TMS_MODE_TEXT80)
      fail("write-reg-mode-r0", TMS_MODE_TEXT80, pico9918_display_mode(PICO9918_INST_ONLY));
    pico9918_debug_reg_write(PICO9918_INST TMS_REG_0, 0);

    /* /INT follows R1's enable at once, rather than at the next active scanline */
    TMS_STATUS(tms9918, PICO9918_SR_STATUS) |= PICO9918_SR0_INT;
    pico9918_debug_reg_write(PICO9918_INST TMS_REG_1, TMS_R1_DISP_ACTIVE | TMS_R1_INT_ENABLE);
    if (!tms9918->frameInt) fail("write-reg-int-not-raised", 1, 0);
    pico9918_debug_reg_write(PICO9918_INST TMS_REG_1, TMS_R1_DISP_ACTIVE);
    if (tms9918->frameInt) fail("write-reg-int-not-dropped", 0, 1);
    TMS_STATUS(tms9918, PICO9918_SR_STATUS) &= (uint8_t)~PICO9918_SR0_INT;

    /* THE UNLOCK LATCH IS PRESERVED. Written once and written twice leave the same byte
       and the same count, so the byte cannot say which - and typing into a register pane
       is not the handshake. Both writes must leave a locked device locked. */
    pico9918_debug_reg_write(PICO9918_INST PICO9918_REG_UNLOCK, 0x1c);
    if (tms9918->isUnlocked) fail("write-r57-once-unlocked", 0, 1);
    pico9918_debug_reg_write(PICO9918_INST PICO9918_REG_UNLOCK, 0x1c);
    if (tms9918->isUnlocked) fail("write-r57-twice-unlocked", 0, 1);
    if (tms9918->lockedMask != 0x07) fail("write-r57-moved-mask", 0x07, tms9918->lockedMask);
    if (pico9918_debug_reg(PICO9918_INST PICO9918_REG_UNLOCK) != 0x1c)
      fail("write-r57-value", 0x1c, pico9918_debug_reg(PICO9918_INST PICO9918_REG_UNLOCK));

    /* and the actions are not taken. R55 arms a program, R56 runs one, R63 begins a
       firmware update, R50 bit 7 resets the file - through here, none of them do. */
    pico9918_debug_gpu_set_pc(PICO9918_INST 0x2000);
    tms9918->restart     = 0;
    tms9918->flash       = 0;
    tms9918->configDirty = false;
    TMS_REGISTER(tms9918, 9) = 0x99;

    pico9918_debug_reg_write(PICO9918_INST PICO9918_REG_GPU_PC_LSB, 0x40);
    if (pico9918_debug_gpu_armed(PICO9918_INST_ONLY)) fail("write-r55-armed", 0, 1);
    if (pico9918_gpu_pc(PICO9918_INST_ONLY) != 0x2000)
      fail("write-r55-moved-pc", 0x2000, pico9918_gpu_pc(PICO9918_INST_ONLY));

    pico9918_debug_reg_write(PICO9918_INST PICO9918_REG_GPU_CONTROL, PICO9918_R56_GPU_RUN);
    if (pico9918_debug_gpu_armed(PICO9918_INST_ONLY)) fail("write-r56-armed", 0, 1);

    pico9918_debug_reg_write(PICO9918_INST PICO9918_REG_FLASH_CONTROL, 0x80);
    if (tms9918->flash) fail("write-r63-flashed", 0, 1);

    pico9918_debug_reg_write(PICO9918_INST PICO9918_REG_ENHANCED2, PICO9918_R50_RESET);
    if (pico9918_debug_reg(PICO9918_INST 9) != 0x99)
      fail("write-r50-reset-file", 0x99, pico9918_debug_reg(PICO9918_INST 9));
    if (tms9918->configDirty) fail("write-r50-config-dirty", 0, 1);

    /* R30 of zero stays zero: the device folds it to the sprite maximum, this does not */
    pico9918_debug_reg_write(PICO9918_INST PICO9918_REG_MAX_SCAN_SPRITES, 0);
    if (pico9918_debug_reg(PICO9918_INST PICO9918_REG_MAX_SCAN_SPRITES) != 0)
      fail("write-r30-zero-folded", 0,
           pico9918_debug_reg(PICO9918_INST PICO9918_REG_MAX_SCAN_SPRITES));

    /* R15 does not snap the timers into the status file */
    TMS_STATUS(tms9918, 0x0f) = 0xee;
    pico9918_debug_reg_write(PICO9918_INST PICO9918_REG_STATUS_SELECT, 0x45);
    if (TMS_STATUS(tms9918, 0x0f) != 0xee) fail("write-r15-snapped", 0xee, TMS_STATUS(tms9918, 0x0f));
  }

  /* 14. and the preserved half, which the cases above cannot cover: every field NOT
         named in the contract, including the ones nobody thought to name. The whole
         instance is snapshotted, one neutral register written, and the bytes that moved
         compared against the two the contract allows. A field added to pico9918_t later
         and touched here fails this without anyone updating a list. */
  {
    uint8_t* const before = malloc(sizeof(pico9918_t));
    const uint8_t* const live = (const uint8_t*)tms9918;
    const uint32_t regByte = 0x6000 + 20;
    const uint32_t dirty   = (uint32_t)offsetof(pico9918_t, palDirty);
    unsigned moved = 0;

    if (!before) return fail("snapshot-alloc", 1, 0), 1;

    TMS_REGISTER(tms9918, 20) = 0x00;
    tms9918->palDirty         = 0;
    memcpy(before, tms9918, sizeof(pico9918_t));

    if (!pico9918_debug_reg_write(PICO9918_INST 20, 0x5e)) fail("snapshot-write", 1, 0);

    for (uint32_t i = 0; i < (uint32_t)sizeof(pico9918_t); ++i)
    {
      if (before[i] == live[i]) continue;
      ++moved;
      if (i != regByte && i != dirty) fail("write-reg-touched-offset", regByte, i);
    }
    if (moved != 2) fail("write-reg-moved-count", 2, moved);

    free(before);
  }

  printf("%s: debugger surface, %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
