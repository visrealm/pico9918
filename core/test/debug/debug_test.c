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
 * The map pico9918_debug_region publishes and the span pico9918_debug_read copies out of
 * it. Two things are under test and neither is the copy loop: that the regions land where
 * the union actually puts its fields, and that read and region agree about where each one
 * stops - a pane splitting bulk work on region boundaries has to land on the same bytes
 * the read does, or it silently skips or doubles a run.
 *
 * The edges are here because every one of them is a coin-flip an implementation would
 * otherwise settle silently: zero length, a null buffer, exactly at the end, past it, and
 * a length that would run off the end of the map.
 */

#include "impl/pico9918_priv.h"
#include "pico9918_debug.h"

#include <stdio.h>
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

  printf("%s: debugger surface, %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
