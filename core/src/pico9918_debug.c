/*
 * pico9918-core - debugger access
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Compiled only when PICO9918_DEBUG_API is on, which a board never sets.
 */

#include "pico9918_debug.h"

#include "impl/pico9918_priv.h"

/*
 * The map, as runs of equal flags rather than as the fields it is made of - adjacent
 * fields a debugger cannot tell apart are one region, which is what "how far that stays
 * true" means. Each row ENDS where the next begins, so row i covers [row i-1 end, end).
 *
 *   0x00000  base VRAM and GRAM below the palette      read, write
 *   0x05000  PRAM                                      read, write, palette
 *   0x06000  the 64 registers                          read
 *   0x06040  GRAM, the scanline and blanking bytes     read, write
 *   0x0B000  the 16 status bytes                       read
 *   0x0B010  GRAM and the GPU workspace overflow       read, write
 *
 * Derived with offsetof rather than transcribed, so a change to the union moves the
 * regions with it instead of silently disagreeing.
 */
#define MAP_AT(FIELD) ((uint32_t)offsetof(pico9918_mem_map_t, FIELD))

#define DEBUG_RW (PICO9918_DEBUG_READABLE | PICO9918_DEBUG_WRITABLE)

typedef struct
{
  uint32_t end;
  uint32_t flags;
} pico9918_debug_region_t;

static const pico9918_debug_region_t debugMap[] = {
  {MAP_AT(pram), DEBUG_RW},
  {MAP_AT(registers), DEBUG_RW | PICO9918_DEBUG_PALETTE},
  {MAP_AT(gram2), PICO9918_DEBUG_READABLE | PICO9918_DEBUG_REGISTERS},
  {MAP_AT(status), DEBUG_RW},
  {MAP_AT(gram4), PICO9918_DEBUG_READABLE | PICO9918_DEBUG_STATUS},
  {(uint32_t)sizeof(((pico9918_t*)0)->vram), DEBUG_RW},
};

/** \brief see the header. The flags at addr, and where they stop being true. */
PICO9918_DLLEXPORT
uint32_t pico9918_debug_region(uint32_t addr, uint32_t* end)
{
  for (unsigned i = 0; i < sizeof(debugMap) / sizeof(debugMap[0]); ++i)
  {
    if (addr < debugMap[i].end)
    {
      if (end) *end = debugMap[i].end;
      return debugMap[i].flags;
    }
  }

  /* past the end: 0 flags and an end of addr, which stops a walk rather than looping */
  if (end) *end = addr;
  return 0;
}

/** \brief see the header. A span of the backing state, disturbing nothing. */
PICO9918_DLLEXPORT
size_t pico9918_debug_read(PICO9918_INST_ARG uint32_t addr, uint8_t* out, size_t len)
{
  const uint32_t size = pico9918_gpu_mem_size();

  if (!out || len == 0 || addr >= size) return 0;

  size_t count = size - addr;
  if (count > len) count = len;

  const uint8_t* const from = (const uint8_t*)&tms9918->vram + addr;
  for (size_t i = 0; i < count; ++i) out[i] = from[i];

  return count;
}
