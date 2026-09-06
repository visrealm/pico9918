/**
 * \file
 * \brief pico9918-core - debugger access
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Purpose: what a host's memory pane, register editor and disassembler need, so that
 *          none of them has to include impl/.
 *
 * These read and write the library's BACKING STATE - the instance's memory as it is
 * stored, every byte appearing exactly once, the GPU's workspace overflow included.
 * That is not the map a GPU program observes: a running personality mirrors 0x4xxx,
 * 0x5xxx, 0x6xxx and 0x7xxx across 4KB and answers 0 in the holes. Backing state is
 * what an emulator's debugger wants, because it re-lays-out nothing when the user
 * switches chip, and the decoded view is derivable from it.
 *
 * Nothing here disturbs the machine. No address latch moves, no read-ahead is
 * consumed, no status is cleared, no interrupt is acknowledged, and the guest cannot
 * tell that any of it happened - which is the entire difference between this and
 * driving the host bus.
 *
 * The scalar half of the read side is already published: pico9918_gpu_mem_size() is
 * the size of the map these address, and pico9918_gpu_mem_value() is one byte of it.
 * They are in gpu/gpu.h for historical reasons and are part of this surface.
 */

#pragma once

#include <stddef.h>

#include "gpu/gpu.h"
#include "pico9918.h"
#include "pico9918_build_config.h"

/* Not in every archive, so including this without it would fail at link time with
   nothing to say why. */
#if !PICO9918_BUILD_DEBUG_API
#error "this library was built without PICO9918_DEBUG_API"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* what pico9918_debug_region() reports about a span */
#define PICO9918_DEBUG_READABLE  0x01 /* pico9918_debug_read returns real bytes here */
#define PICO9918_DEBUG_WRITABLE  0x02 /* pico9918_debug_write stores here */
#define PICO9918_DEBUG_REGISTERS 0x04 /* the register file - pico9918_debug_reg_write */
#define PICO9918_DEBUG_STATUS    0x08 /* the status file, which the library owns */
#define PICO9918_DEBUG_PALETTE   0x10 /* PRAM - a write here republishes the palette */

/**
 * \brief what the byte at \p addr is, and how far that stays true
 *
 * The map itself, so a pane can colour protected spans and split bulk work without
 * carrying its own copy of the layout. Returns the PICO9918_DEBUG_* flags for the
 * region containing \p addr, and through \p end, which may be null, the EXCLUSIVE
 * address the region stops at - so a walk is `addr = end` until the flags come back 0.
 *
 * Past the end of the map returns 0 with `*end = addr`, which terminates such a walk.
 *
 * No instance: the layout is the build's, not the selected personality's, the same
 * reason pico9918_gpu_mem_size() takes none.
 */
PICO9918_DLLEXPORT
uint32_t pico9918_debug_region(uint32_t addr, uint32_t* end);

/**
 * \brief a span of the map, without disturbing anything
 *
 * Copies up to \p len bytes from \p addr into \p out and returns how many, which is
 * short at the end of the map and 0 past it. A null \p out, or a \p len of 0, copies
 * nothing and returns 0 rather than faulting, so a caller may probe with either.
 *
 * Every readable byte here is the byte pico9918_gpu_mem_value() returns for the same
 * address. This exists because a 64KB pane one call at a time across a shared-library
 * boundary is 65572 calls.
 */
PICO9918_DLLEXPORT
size_t pico9918_debug_read(PICO9918_INST_ARG uint32_t addr, uint8_t* out, size_t len);

#ifdef __cplusplus
}
#endif
