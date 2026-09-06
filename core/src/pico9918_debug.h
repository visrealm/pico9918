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
#define PICO9918_DEBUG_READABLE  0x01 /**< pico9918_debug_read returns real bytes here */
#define PICO9918_DEBUG_WRITABLE  0x02 /**< pico9918_debug_write stores here */
#define PICO9918_DEBUG_REGISTERS 0x04 /**< the register file - use pico9918_debug_reg_write */
#define PICO9918_DEBUG_STATUS    0x08 /**< the status file, which the library owns */
#define PICO9918_DEBUG_PALETTE   0x10 /**< PRAM - a write here republishes the palette */

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

/**
 * \brief a span of the map, written without the machine noticing
 *
 * Copies up to \p len bytes from \p in to \p addr and returns how many landed. A null
 * \p in, or a \p len of 0, writes nothing and returns 0.
 *
 * SHORT AT THE FIRST BYTE IT WILL NOT WRITE, which is the end of the map, the register
 * window and the status window - the two PICO9918_DEBUG_REGISTERS and
 * PICO9918_DEBUG_STATUS report. So a bulk loader scrubbing memory cannot start a GPU
 * program or strand a firmware update, and a caller that wants a register has
 * pico9918_debug_reg_write, which is a different operation with a different contract.
 * A run that stops immediately returns 0, which is how a caller tells a refused window
 * from an accepted one.
 *
 * A span landing in PRAM republishes the palette, because the write contract is "no
 * host-bus side effects" rather than "no effects" - without it a debugger edits the
 * palette successfully and the picture does not change.
 */
PICO9918_DLLEXPORT
size_t pico9918_debug_write(PICO9918_INST_ARG uint32_t addr, const uint8_t* in, size_t len);

/**
 * \brief a register, as the register file actually holds it
 *
 * NOT what pico9918_reg_value() answers, which is the guest's read and folds the number
 * to three bits on a locked device - so a pane showing R30 there is showing R6. This is
 * the byte at \p reg. Above 63 returns 0, the file being 64 entries.
 */
PICO9918_DLLEXPORT
uint8_t pico9918_debug_reg(PICO9918_INST_ARG uint8_t reg);

/**
 * \brief a register, stored where its number says, with no device behaviour
 *
 * NOT the device's write. pico9918_write_register_value() is a protocol: it folds the
 * number to three bits on a locked device, so asking it for R30 stores R6; it drops the
 * write entirely on a locked M4; and R55, R56, R50, R63 and R15 each set something in
 * motion. A register editor wants none of that - it wants R30 to mean R30.
 *
 * So this is the physical store, and its contract is a list rather than a principle.
 * For \p reg 0-63 it does EXACTLY four things:
 *
 *   1. stores \p value at register \p reg - not reg & lockedMask, not reg & 7
 *   2. marks the palette as owing a republish
 *   3. synchronizes the cached display mode, which R0 and R1 change
 *   4. reconciles /INT, because R1's interrupt enable must take effect at once
 *
 * Everything else is untouched: the unlock latch, the GPU's address and armed state, the
 * flash and config-dirty flags, the host address latch, every other register, every
 * status byte, every config byte. No GPU program starts, no firmware update begins, no
 * register file resets, no timer snaps.
 *
 * The unlock latch is PRESERVED rather than recomputed, and that is deliberate: the
 * device unlocks on the value arriving TWICE, so the byte stored in R57 does not
 * determine the state - after one write and after two it is the same byte and the same
 * count, differing only in the latch. Recomputing it would have to guess. Typing into a
 * register pane is not performing the handshake, so it does not move it.
 *
 * Returns false, changing nothing, for \p reg above 63.
 */
PICO9918_DLLEXPORT
bool pico9918_debug_reg_write(PICO9918_INST_ARG uint8_t reg, uint8_t value);

/**
 * \brief a live palette entry, in host byte order
 *
 * PRAM as the renderer reads it, with the big-endian storage undone - so the value is
 * the 0x0rgb an F18A program wrote, not the byte-swapped word underneath. The F18A
 * defines the low twelve bits; anything above them is whatever is stored there, because
 * this is the backing state and a debugger that wrote a raw byte should see it back.
 *
 * Above index 63 returns 0, PRAM being 64 entries.
 */
PICO9918_DLLEXPORT
uint16_t pico9918_debug_palette(PICO9918_INST_ARG uint8_t index);

/**
 * \brief where the next guest access would land
 *
 * The host address latch made EFFECTIVE, which is not the counter it is kept in: that
 * one is 32 bits and runs past the bus width between accesses, and on a 4K chip with
 * R1's 16K bit clear the machine permutes the address rather than merely masking it. So
 * a pane wanting "the byte the next read returns" cannot get there with a mask.
 */
PICO9918_DLLEXPORT
uint16_t pico9918_debug_vram_address(PICO9918_INST_ONLY_ARG);

/**
 * \brief whether a GPU program is waiting to run
 *
 * Armed, not executing: a host pacing the GPU itself asks this to find out whether there
 * is anything to step. Whether a program is still going after a slice is
 * pico9918_gpu_step_n()'s return, which is a different question.
 */
PICO9918_DLLEXPORT
bool pico9918_debug_gpu_armed(PICO9918_INST_ONLY_ARG);

/**
 * \brief move the GPU's PC without starting it
 *
 * Masked even, the way the register path masks it. Leaves the armed state exactly as it
 * found it, so this redirects a program that was going to run and does not start one
 * that was not. Writes neither R54/R55 - which would be a second, visible effect on the
 * register file - nor the status.
 *
 * pico9918_gpu_pc() reads it back.
 */
PICO9918_DLLEXPORT
void pico9918_debug_gpu_set_pc(PICO9918_INST_ARG uint16_t pc);

#ifdef __cplusplus
}
#endif
