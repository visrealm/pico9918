/**
 * \file
 * \brief pico9918-core - the private instance layout
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * The instance struct, the register and status accessors, and the privileged inline
 * surface: everything a host needs that a plain consumer must not reach for. An
 * emulator includes pico9918.h and its platform header; a host that drives the chip
 * from an interrupt includes this, and takes on the ordering rules each entry states.
 *
 * The Impl entries are inline rather than calls because their callers are the host bus
 * handlers and the per-scanline path, where a `bl` is not free.
 *
 * A DEBUGGER IS THE SECOND AUDIENCE, and the rule for it is which surface, not which
 * operation.
 *
 * Reads are public because reads are safe, and the published set is meant to be
 * complete. pico9918.h has the chip - pico9918_peek_status, pico9918_status_value,
 * pico9918_read_data_no_inc, pico9918_reg_value, pico9918_vram_value - and gpu/gpu.h
 * has the GPU: pico9918_gpu_pc, pico9918_gpu_mem_value, pico9918_gpu_mem_size,
 * pico9918_gpu_reg_value, pico9918_gpu_status. A bridge that finds itself in this
 * header for a READ has taken a wrong turn rather than made a judgement call, and if
 * something genuinely has no public read then the gap is the bug.
 *
 * Two of those pairs look alike and are not:
 *
 *   pico9918_vram_value    | the guest's view, so it stops at 0x3FFF
 *   pico9918_gpu_mem_value | the GPU's, so it reaches GRAM, the palette, the register
 *                          | and status windows and the workspace above 0xFFFF
 *   pico9918_reg_value     | VR0-VR63, and the guest's view of them: on a locked device
 *                          | it decodes three address bits, so reg 30 reads R6. The
 *                          | physical register behind that is TMS_REGISTER, below
 *   pico9918_gpu_reg_value | the GPU's own R0-R15, out of its workspace
 *
 * WRITES that must not behave like the guest are the reason to be here. Every public
 * register write goes through the bus and so takes the unlock gate and the locked-mask
 * aliasing with it - a locked device redirects VR30 to R6 rather than refusing it. A
 * register editor writing what the operator typed wants `TMS_REGISTER(tms9918, reg) =
 * value`, which is the whole of the crossing.
 *
 * And the invariant that removal established: a PUBLIC entry must not silently write
 * somewhere other than where its parameter names. The engine below takes the raw select
 * byte, `0x80 | reg`, and is here rather than published for exactly that reason - typed
 * as a register enum it turned PICO9918_REG_UNLOCK into a write of R1. Anything moved
 * out to the public surface has to be honest about its own argument first.
 *
 * Nothing here is stable. It is versioned with the library and moves when the library
 * does, so a tool that reaches in is pinned to a commit. That is the trade, and it is
 * why anything on the guest's normal path belongs on the public surface instead.
 */

#pragma once

#include "platform.h"

#ifdef PICO_BUILD
#include "pico/stdlib.h"
#endif

#include "../pico9918.h"
#include "../pico9918_config.h"


#define GRAPHICS_NUM_COLS   32
#define GRAPHICS_NUM_ROWS   24
#define GRAPHICS_CHAR_WIDTH 8

#define TEXT_NUM_COLS   40
#define TEXT_NUM_ROWS   24
#define TEXT_CHAR_WIDTH 6
#define TEXT_PADDING_PX 8
#define TEXT80_NUM_COLS 80

/* 80 columns at eight bits a pixel: a board capability, not a chip. It buys the tile palette
   select, ECM, the bitmap layer and the shared composite in 80-column text, none of which a
   four-bit line can represent at all. Off unless a board asks for it. */
#ifndef PICO9918_TEXT80_8BPP
#define PICO9918_TEXT80_8BPP 0
#endif

/* The widest line any mode on this build renders. 80 columns show 480 pixels inside a 512-pixel
   line, which is 256 bytes at four bits a pixel and 512 at eight. */
#if PICO9918_TEXT80_8BPP
#define SCANLINE_BYTES_MAX (TMS9918_PIXELS_X * 2)
/* the picture begins at sprite pixel 8 in either depth, so its byte offset doubles with the depth
   while the sprite grid it is measured in does not */
#define TEXT80_PADDING_PX (TEXT_PADDING_PX * 2)
#else
#define SCANLINE_BYTES_MAX TMS9918_PIXELS_X
#define TEXT80_PADDING_PX  TEXT_PADDING_PX
#endif

/* room for the cell a fine scroll uncovers past the picture's far edge */
#define SCANLINE_BUFFER_BYTES (SCANLINE_BYTES_MAX + 8)
#define SCANLINE_MASK_WORDS   ((SCANLINE_BUFFER_BYTES + 31) / 32)

#define PATTERN_BYTES         8
#define GFXI_COLOR_GROUP_SIZE 8

#define MAX_SPRITES 32

#define SPRITE_ATTR_Y        0
#define SPRITE_ATTR_X        1
#define SPRITE_ATTR_NAME     2
#define SPRITE_ATTR_COLOR    3
#define SPRITE_ATTR_BYTES    4
#define LAST_SPRITE_YPOS     0xD0
#define MAX_SCANLINE_SPRITES 4

#define PICO9918_MODE_TMS9918 0
#define PICO9918_MODE_F18A    1

#ifndef PICO9918_MODE
#define PICO9918_MODE PICO9918_MODE_TMS9918
#endif

#define BASE_VRAM_SIZE (1 << 14) /* 16kB */

/* The register file is the same width in both modes. Enhanced registers are read outside
   the unlock gate - the backdrop's R24, the frame module's R19 - and a narrower array makes
   those reads out of bounds rather than zero, which is what a TMS9918A returns anyway. */
#define TMS_REGISTERS        64
#define TMS_STATUS_REGISTERS 16

/* What the F18A mode buys is the mapping: 64KB of VRAM with the registers, the status file,
   the palette and the scanline counter visible to the GPU at fixed addresses. */
#if PICO9918_MODE == PICO9918_MODE_F18A
#define VRAM_SIZE        (1 << 16) /* 64kB */
#define MAPPED_REGISTERS 1
#define MAPPED_STATUS    1
#else
#define VRAM_SIZE        BASE_VRAM_SIZE
#define MAPPED_REGISTERS 0
#define MAPPED_STATUS    0
#endif

#define VRAM_MASK (BASE_VRAM_SIZE - 1) /* 0x3fff */

/* CPU-side VRAM mask. TODO: base-dependent, for 16K TMS9918A/F18A mirroring
   against the V9938's 128K */
#define PICO9918_CPU_VRAM_MASK(tms) VRAM_MASK

/* R1 bit 7 is the 4K/16K DRAM select, and only a part that drives DRAM has one. An F18A
   has SRAM, so a MODE=F18A build without the runtime switch is never asked and the
   transform below folds away entirely. */
#if PICO9918_MODE == PICO9918_MODE_F18A && !PICO9918_BUILD_RUNTIME_CHIP
#define PICO9918_VRAM_4K_CHIP(T) false
#else
#define PICO9918_VRAM_4K_CHIP(T) PICO9918_HAS(T, PICO9918_FEAT_VRAM_4K)
#endif


typedef struct
{
  uint8_t base[BASE_VRAM_SIZE]; // 0x0000-0x3FFF (16KB)
#if PICO9918_MODE == PICO9918_MODE_F18A
  /* video ram */
  uint8_t gram1[0x1000]; // 0x4000-0x4fff (4KB) 2x repeated 2KB
  uint16_t pram[0x0800]; // 0x5000-0x5fff (4KB) 32x repeated 128B

  /* 64 write-only registers */
  uint8_t registers[TMS_REGISTERS]; // 0x6000-0x6040

  uint8_t gram2[0x1000 - TMS_REGISTERS]; // 0x6040-0x6FFF (~4KB)
  uint8_t scanline;                      // 0x7000
  uint8_t blanking;                      // 0x7001
  uint8_t gram3[0x4000 - 2];             // 0x7002-0xAFFF (~16KB)

  /* status registers (read-only) */
  uint8_t status[TMS_STATUS_REGISTERS]; // 0xB000

  uint8_t gram4[0x5000 - TMS_STATUS_REGISTERS]; // 0xB010-0xFFFF (~20KB)
  uint8_t wrksp[36];                            // 0x10000 overflow for hidden workspace
#else
  /* At no address anything can reach: the CPU mask stops the data port at 0x3fff and there
     is no GPU. The palette is where the renderer reads colour from, so it exists in both
     modes - here it is simply fixed at what pico9918_reset writes. */
  uint16_t pram[64];
  uint8_t scanline;
  uint8_t blanking;
#endif
} pico9918_mem_map_t;

/* Whether a GPU pass can be capped, and so whether the library can pace one itself. The
   hand-written Thumb cores run a program to completion and ignore a cap; the builds that
   have them are boards, which run the GPU on a core of their own. Here rather than in
   gpu.c because the scanline path has to fold the pacing away, not test for it. */
#if defined(PICO_BUILD) && !defined(PICO9918_GPU_C_CORE)
#define PICO9918_GPU_BUDGETED 0
#else
#define PICO9918_GPU_BUDGETED 1
#endif

/* Has the F18A been unlocked, and is this the write that unlocks it? A TMS9918A cannot be,
   so both are literals there - and graphics_i_scan_line forks on the first exactly once,
   which is what makes the entire enhanced renderer fold away in that build rather than
   needing a condition per feature. Honouring an unlock that widens lockedMask while the
   renderer ignores every register it admits would be worse than not honouring it. */
#if PICO9918_MODE == PICO9918_MODE_F18A
#define PICO9918_UNLOCKED(T)        ((T)->isUnlocked)
#define PICO9918_UNLOCK_WRITE(R, V) \
  ((R) == (0x80 | PICO9918_REG_UNLOCK) && ((V) & 0xfc) == PICO9918_R57_UNLOCK)
#else
#define PICO9918_UNLOCKED(T)        false
#define PICO9918_UNLOCK_WRITE(R, V) false
#endif

/* What a personality answers to. Derived once, in pico9918_set_chip, so each site reads
   one bit rather than re-deriving the ladder. Only what a personality can be asked to do
   differently is a bit: the GPU is not one, because a program can only be started
   through registers the unlock gate already covers.

   Without the runtime switch there is nothing to gate - the build is one chip, and what
   that chip can do it already does - so every one of them folds to a literal true and
   the gates cost a board exactly what they cost it before they existed. */
#define PICO9918_FEAT_UNLOCK  0x01 /* the F18A unlock write is honoured */
#define PICO9918_FEAT_CONFIG  0x02 /* the VR58/59 config port and R63 firmware update */
#define PICO9918_FEAT_OVERLAY 0x04 /* the splash and diagnostics overlays */
#define PICO9918_FEAT_BITMAP  0x08 /* R0 M3 is decoded, so Graphics II exists */
#define PICO9918_FEAT_VRAM_4K 0x10 /* R1 bit 7 is decoded, so 4K DRAM addressing exists */

#if PICO9918_BUILD_RUNTIME_CHIP
#if PICO9918_MODE != PICO9918_MODE_F18A
#error "PICO9918_RUNTIME_CHIP needs the F18A build - a MODE=0 archive has no personality above the base to select"
#endif
#define PICO9918_HAS(T, F) (((T)->features & (F)) != 0)
/* SR1 is what software probing for an F18A reads: 0xE0 is the F18A ID, and the PICO9918
   sets 0x08 for anyone who cares that it is not a real one. The base personality shares
   the F18A value and never shows it: reaching SR1 needs a write to R15, which is above
   the eight a locked device admits. */
#define PICO9918_SR1_ID(T) (((T)->chip == PICO9918_CHIP_PICO9918) ? 0xE8 : 0xE0)
#else
#define PICO9918_HAS(T, F) true
#define PICO9918_SR1_ID(T) 0xE8
#endif

#if MAPPED_REGISTERS
#define TMS_REGISTER(T, R) (T->vram.map.registers[R])
#else
#define TMS_REGISTER(T, R) (T->registers[R])
#endif

#if MAPPED_STATUS
#define TMS_STATUS(T, R) (T->vram.map.status[R])
#else
#define TMS_STATUS(T, R) (T->status[R])
#endif

/* Graphics II is the A in TMS9918A: the pre-A part does not decode R0 M3. Unlike M4 this
   is not a mode-gated question - every build can be the pre-A part - so PICO9918_HAS
   alone is the gate, and it folds to a literal true without the runtime switch. */
#define PICO9918_GM2(T) PICO9918_HAS(T, PICO9918_FEAT_BITMAP)

/* A TMS9918A does not decode R0 bit 2. Build-time as well as runtime: PICO9918_HAS folds
   to true in a MODE=0 archive, so neither may be written as PICO9918_HAS alone. */
#if PICO9918_MODE == PICO9918_MODE_F18A
#define PICO9918_CAN_UNLOCK(T) PICO9918_HAS(T, PICO9918_FEAT_UNLOCK)
#define PICO9918_M4(T)                                                                             \
  (PICO9918_CAN_UNLOCK(T) && (TMS_REGISTER(T, TMS_REG_0) & TMS_R0_MODE_TEXT_80))
#else
#define PICO9918_CAN_UNLOCK(T) false
#define PICO9918_M4(T)         false
#endif


/* PRIVATE DATA STRUCTURE
  * ---------------------- */
struct pico9918_s
{
  /* First: every VRAM, register and status access goes through this union, so at offset
     zero the instance pointer doubles as its base, and the GPU MPU guard's two ranges
     are page-aligned by construction rather than by an offset-derived mask. */
  union
  {
    uint8_t bytes[VRAM_SIZE];
    pico9918_mem_map_t map;
  } vram;

  /* current address for cpu access (auto-increments) */
  uint32_t currentAddress;

  uint16_t gpuAddress;

  /* The GPU's status register, across a bounded step and nothing else. run9900 keeps
     it in a local, which is all a run to completion needs; a budget can expire between
     the compare that sets a flag and the jump that reads it, so a resume that started
     from zero would take the wrong branch. Only the portable core takes a budget, so
     only the portable core reads this. */
  uint16_t gpuStatus;

  /* address or register write stage (0 or 1) */
  uint8_t regWriteStage;

  /* holds first stage of write to address/register port */
  uint8_t regWriteStage0Value;

  /* buffered value */
  uint8_t readAheadBuffer;

  uint8_t lockedMask;  // 0x07 when locked, 0x3F when unlocked
  uint8_t unlockCount; // number of unlock steps taken
  bool isUnlocked;     // boolean version of lockedMask - read through PICO9918_UNLOCKED

  volatile uint8_t restart;
  volatile uint8_t flash;

#if PICO9918_MODE == PICO9918_MODE_F18A && PICO9918_GPU_BUDGETED
  /* Zero leaves an armed program to whoever else runs it. See pico9918_gpu_set_clock. */
  uint32_t gpuIps;
  uint32_t gpuSlice;
#endif

  /* palette writes are done in two stages too */
  uint8_t palWriteStage;
  uint8_t palWriteStage0Value;
  uint8_t palDirty;

  /* runtime base VDP selection (independent of F18A unlock) */
  uint8_t vdpBase; /* PICO9918_BASE_TMS9918 or PICO9918_BASE_V9938 */

#if PICO9918_BUILD_RUNTIME_CHIP
  /* which chip this instance answers as, and the same thing as the bit per feature the
     gates read. Both survive a reset - see pico9918_set_chip. Absent from a board's
     build, which is one chip and gates nothing. */
  uint8_t chip;     /* pico9918_chip_t */
  uint8_t features; /* PICO9918_FEAT_* - read through PICO9918_HAS */
#endif

  bool scanlineHasSprites;

  uint32_t startTime;
  uint32_t stopTime;
  uint32_t currentTime;

  struct
  {
    uint16_t y;      /* raw scanline */
    uint16_t y1;     /* T1 layer Y after scroll (= y when locked or scroll=0) */
    uint16_t y2;     /* T2 layer Y after scroll (= y when T2 disabled or locked) */
    bool swapY1Page; /* T1 name-table page swap flag */
    bool swapY2Page; /* T2 name-table page swap flag */
    uint8_t* pixels; /* pointer to caller-owned output pixel buffer */
  } scanCtx;

#if !MAPPED_REGISTERS
  uint8_t registers[TMS_REGISTERS];
#endif

#if !MAPPED_STATUS
  uint8_t status[TMS_STATUS_REGISTERS];
#endif

  uint8_t config[256];
  bool configDirty;

  /* the VDP state the configuration seeds - palette, sprite limit, scanlines - is
     owed. Not palDirty above, which is the converted palette copy owing PRAM. */
  bool configVdpDirty;

  /* Aligned tile rendering optimization buffers - one extra tile for the scroll offset */
  uint8_t __aligned(4) tileLayer2Buffer[SCANLINE_BUFFER_BYTES];
  uint8_t __aligned(4) tileLayer1Buffer[SCANLINE_BUFFER_BYTES];
  uint32_t __aligned(4) layerSelectionMask[SCANLINE_MASK_WORDS]; // 1 bit per pixel: 0=T1, 1=T2
  uint32_t __aligned(4) finalMask[SCANLINE_MASK_WORDS];          // 1 bit per pixel: 0=T1, 1=T2

  /* Frame interrupt / status state. Not volatile, and touched from both the CPU-interface
     handlers and the frame path; the critical section around updateInterrupts is what
     orders them. */
  bool frameInt;       /* current /INT pin state (true = asserted) */
  uint8_t frameStatus; /* SR0 shadow - the latch the frame path merges into */
  bool frameDoneInt;   /* interrupt already raised this frame? */

#if !PICO9918_SINGLE_INSTANCE
  /* The integration layer's host callbacks, per instance. Absent from the single-instance
     struct entirely: that build keeps them in file statics, so a board's layout is what it
     was and the MPU guard's offset assertion above still holds. */
  struct
  {
    pico9918_config_applied_fn fn;
    void* userdata;
  } configApplied;

  struct
  {
    pico9918_config_reload_fn fn;
    void* userdata;
  } configReload;

  struct
  {
    pico9918_gpu_flash_fn fn;
    void* userdata;
  } gpuFlash;

  struct
  {
    pico9918_gpu_config_save_fn fn;
    void* userdata;
  } gpuConfigSave;
#endif
};

#ifdef PICO_BUILD
/* MPU guard anchor (see guard() in gpu/gpu.c): the guarded windows are derived
   from vram's address. At offset zero, and with the instance 256-aligned, both land at the
   start of their own 256-byte page and neither can cross one. Move vram and that stops
   being true by construction - restore the layout rather than shift the window. */
_Static_assert(offsetof(struct pico9918_s, vram) == 0,
               "vram offset moved - the GPU MPU guard ranges could cross a page boundary");
#endif

#if PICO9918_SINGLE_INSTANCE
extern pico9918_t* const tms9918;
#endif

/**
 * \brief where a CPU-side VRAM access lands
 *
 * A part that drives DRAM multiplexes the address as a row and a column, and R1 bit 7
 * says how wide each half is. At 16K it is seven bits of each and the address is used
 * raw. At 4K it is six of each, driven into the seven that the 16K DRAMs on the board
 * still want, which rotates the middle seven bits up one place and leaves the low six
 * and the top one where they were.
 *
 * Only the CPU side. Display fetches take the address the tables name, because an F18A
 * has no such bit at all and nothing drives a picture out of 4K on a 16K machine.
 */
PICO9918_INLINE_HOT uint32_t pico9918_cpu_vram_addr_impl(PICO9918_INST_ARG uint32_t addr)
{
  addr &= PICO9918_CPU_VRAM_MASK(tms9918);

  if (PICO9918_VRAM_4K_CHIP(tms9918) && !(TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_RAM_16K))
  {
    /* static bits | shifted bits | rotated bit */
    addr = (addr & 0x203f) | ((addr & 0x0fc0) << 1) | ((addr & 0x1000) >> 6);
  }

  return addr;
}

/**
 * \brief set a register from the second byte of a host register write
 *
 * \p regSelect is that byte, not a register number: bit 7 set, the register in the low
 * six. The locked-mask aliasing and the M4 rule are both defined on it, which is why it
 * is not a pico9918_register_t. Carries the unlock sequence, the GPU arming writes and
 * the palette rebuild, and deliberately does not reconcile /INT - pico9918_write_addr
 * does that on the way out. Out of line, unlike its neighbours here: it is large, and
 * the inline entry below is its only hot caller.
 */
PICO9918_DLLEXPORT
void pico9918_write_reg_value_impl(PICO9918_INST_ARG uint8_t regSelect, uint8_t value);

/**
 * \brief write an address (mode = 1) to the tms9918
 *
 * data: the data (DB0 -> DB7) to send
 */
PICO9918_INLINE_HOT void pico9918_write_addr_impl(PICO9918_INST_ARG uint8_t data)
{
  if (tms9918->regWriteStage == 0)
  {
    /* first stage byte - either an address LSB or a register value */

    tms9918->regWriteStage0Value = data;
    tms9918->regWriteStage       = 1;
  }
  else
  {
    /* second byte - either a register number or an address MSB */

    if (data & 0x80) /* register */
    {
      if ((data & 0x40) == 0) // 64 registers, so only bit 6 is reserved
      {
        pico9918_write_reg_value_impl(PICO9918_INST data, tms9918->regWriteStage0Value);
      }
    }
    else /* address */
    {
      tms9918->currentAddress = tms9918->regWriteStage0Value | ((data & 0x3f) << 8);
      if ((data & 0x40) == 0)
      {
        tms9918->readAheadBuffer =
          tms9918->vram.bytes[pico9918_cpu_vram_addr_impl(PICO9918_INST tms9918->currentAddress)];
        tms9918->currentAddress += (int8_t)TMS_REGISTER(tms9918, PICO9918_REG_VRAM_INC); // increment register
      }
    }
    tms9918->regWriteStage = 0;
  }
}

/**
 * \brief is R#15's status-register select live?
 *
 * True on an F18A-unlocked device, and also on a locked V9938-base one - the V9938 has
 * R#15 in its base register set, so status select is not an unlock privilege there.
 *
 * The second disjunct is dead today and folds away: only the unlock sequence widens
 * `lockedMask` past 0x07, so a locked device cannot have had R#15 written whatever its
 * base. It is written base-aware so that V9938 support swaps a base rather than a
 * scattered condition.
 */
PICO9918_INLINE bool pico9918_status_select_active(PICO9918_INST_ONLY_ARG)
{
  return PICO9918_UNLOCKED(tms9918) || (tms9918->vdpBase == PICO9918_BASE_V9938);
}

/**
 * \brief THE single implementation of "a status register was just read" - shared by the
 * public read (pico9918_read_status_impl) and the CPU-interface read reconcile
 * (pico9918_status_read_reconcile_impl); see the entry shims for which side
 * effects each one owns.
 *
 * readReg: the selected status register (0..15)
 * readVal: the value the reader received
 *
 * SR0: clear only the flags that were actually seen set, out of the SR0 shadow
 *      the frame path latches into. A seen 5S additionally restores the sprite
 *      number field to 31 (the reset value) - the flag and its ID clear together.
 * SR1: bit 0 is the R#19 line-interrupt flag, clear-on-read.
 */
/* Defined below, and needed here: reading SR1 clears the scanline flag, which can be the
   only thing holding /INT down. An ISR that acknowledged and returned with the pin still
   asserted would re-enter immediately. */
PICO9918_INLINE_HOT bool pico9918_interrupt_status_impl(PICO9918_INST_ONLY_ARG);

PICO9918_INLINE_HOT void pico9918_status_read_core(PICO9918_INST_ARG uint8_t readReg, uint8_t readVal)
{
  if (readReg == PICO9918_SR_STATUS)
  {
    readVal &= (PICO9918_SR0_INT | PICO9918_SR0_5S | PICO9918_SR0_COLLISION);
    tms9918->frameStatus &= ~readVal; // Clear only the flags that were set
    if (readVal & PICO9918_SR0_5S)    // Was 5th Sprite flag set?
      tms9918->frameStatus |= 0x1f;   // Set sprite number to 31
    TMS_STATUS(tms9918, PICO9918_SR_STATUS) = tms9918->frameStatus;
    if (readVal & PICO9918_SR0_INT) // Was Interrupt flag set?
    {
      tms9918->frameInt = false;
      PICO9918_HOST_SET_INT(false);
    }
  }
  else if (readReg == PICO9918_SR_IDENT)
  {
    if (readVal & PICO9918_SR1_HF)
    {
      TMS_STATUS(tms9918, PICO9918_SR_IDENT) &= (uint8_t)~PICO9918_SR1_HF;
      /* the frame source may still be asserting, so re-derive rather than clearing */
      const bool stillInt = pico9918_interrupt_status_impl(PICO9918_INST_ONLY);
      if (stillInt != tms9918->frameInt)
      {
        tms9918->frameInt = stillInt;
        PICO9918_HOST_SET_INT(stillInt);
      }
    }
  }
}

/**
 * \brief CPU-interface entry: the host's read-ahead already handed the CPU a value, so
 * this only applies the read's side effects. The host supplies both the value the
 * CPU saw and the register it came from (zero while status select is inactive).
 */
PICO9918_INLINE_HOT void pico9918_status_read_reconcile_impl(PICO9918_INST_ARG uint8_t readReg, uint8_t readVal)
{
  tms9918->regWriteStage = 0;

  if (!pico9918_status_select_active(PICO9918_INST_ONLY)) readReg = 0;

  pico9918_status_read_core(PICO9918_INST readReg, readVal);
}

/**
 * \brief read from the status register
 *
 * Emulator-facing entry: fetches the value itself, then runs the same core. It
 * additionally resets the data-port palette staging, which the CPU-interface
 * path never did (that port is written through a different host seam).
 *
 * PICO9918_INLINE, not PICO9918_INLINE_HOT. The core it calls reaches
 * PICO9918_HOST_SET_INT, which on Pico is an SDK `static inline` gpio_put, and a
 * non-static inline may not call a static one - which is why every entry on this
 * surface is static. Its only caller is pico9918_read_status() in pico9918.c, the
 * external definition consumers link against.
 *
 * TWO BEHAVIOURS worth stating outright, because this is a public entry point and
 * no gate makes either visible:
 *
 * 1. Reading SR0 does NOT scrub the sprite-number field. It clears only the
 *    (INT|5S|COL) flags actually seen set, restoring the number to 31 only when 5S
 *    was among them. That follows the documented register layout: F is
 *    clear-on-read, but SP4-SP0 is a data field, not a flag.
 * 2. On a host that defines PICO9918_HOST_SET_INT, reading SR0 with F set RELEASES
 *    THE /INT LINE and clears the library's interrupt shadow - so every input with
 *    bit 7 set writes the pin. A host running its own /INT plumbing alongside this
 *    call will see an unrequested pin write; such a host should drive the line from
 *    the library's state rather than in parallel with it.
 */
PICO9918_INLINE uint8_t pico9918_read_status_impl(PICO9918_INST_ONLY_ARG)
{
  tms9918->regWriteStage = 0;

  tms9918->palWriteStage = 0;
  TMS_REGISTER(tms9918, PICO9918_REG_PALETTE_CONTROL) &=
    (uint8_t)~PICO9918_R47_DATA_PORT; // reset data port palette mode

  const uint8_t readReg = TMS_REGISTER(tms9918, PICO9918_REG_STATUS_SELECT) & PICO9918_R15_STATUS_NUM;
  const uint8_t readVal = TMS_STATUS(tms9918, readReg);

  pico9918_status_read_core(PICO9918_INST readReg, readVal);

  return readVal;
}

/** \brief read from the status register without resetting it */
PICO9918_INLINE_HOT uint8_t pico9918_peek_status_impl(PICO9918_INST_ONLY_ARG)
{
  return TMS_STATUS(tms9918, PICO9918_SR_STATUS);
}

/**
 * \brief write data (mode = 0) to the tms9918
 *
 * data: the data (DB0 -> DB7) to send
 */
PICO9918_INLINE_HOT void pico9918_write_data_impl(PICO9918_INST_ARG uint8_t data)
{
  if (TMS_REGISTER(tms9918, PICO9918_REG_PALETTE_CONTROL) &
      PICO9918_R47_DATA_PORT) // data port is in palette mode
  {
    if (tms9918->palWriteStage == 0)
    {
      tms9918->palWriteStage0Value = data & 0x0f;
      ++tms9918->palWriteStage;
    }
    else
    {
      tms9918->palWriteStage = 0;

      // this looks backwards because ARM is little-endian, TMS9900 is big-endian.
      tms9918->vram.map.pram[TMS_REGISTER(tms9918, PICO9918_REG_PALETTE_CONTROL) & PICO9918_R47_INDEX] =
        (tms9918->palWriteStage0Value) | (data << 8);
      tms9918->palDirty = 1;

      // reset data port palette mode
      if (TMS_REGISTER(tms9918, PICO9918_REG_PALETTE_CONTROL) & PICO9918_R47_AUTO_INC)
      {
        ++TMS_REGISTER(tms9918, PICO9918_REG_PALETTE_CONTROL);
      }
      else
      {
        TMS_REGISTER(tms9918, PICO9918_REG_PALETTE_CONTROL) &= (uint8_t)~PICO9918_R47_DATA_PORT;
      }
    }
  }
  else
  {
    tms9918->regWriteStage                                                         = 0;
    tms9918->readAheadBuffer                                                       = data;
    tms9918->vram.bytes[pico9918_cpu_vram_addr_impl(PICO9918_INST tms9918->currentAddress)] = data;
    tms9918->currentAddress += (int8_t)TMS_REGISTER(tms9918, PICO9918_REG_VRAM_INC); // increment register
  }
}


/** \brief read data (mode = 0) from the tms9918 */
PICO9918_INLINE_HOT uint8_t pico9918_read_data_impl(PICO9918_INST_ONLY_ARG)
{
  tms9918->regWriteStage   = 0;
  uint8_t currentValue     = tms9918->readAheadBuffer;
  tms9918->readAheadBuffer =
    tms9918->vram.bytes[pico9918_cpu_vram_addr_impl(PICO9918_INST tms9918->currentAddress)];
  tms9918->currentAddress += (int8_t)TMS_REGISTER(tms9918, PICO9918_REG_VRAM_INC); // increment register
  return currentValue;
}

/** \brief refill the read-ahead buffer from the current address and return the new value */
PICO9918_INLINE_HOT uint8_t pico9918_read_ahead_data_impl(PICO9918_INST_ONLY_ARG)
{
  tms9918->regWriteStage = 0;
  tms9918->readAheadBuffer =
    tms9918->vram.bytes[pico9918_cpu_vram_addr_impl(PICO9918_INST tms9918->currentAddress)];
  tms9918->currentAddress += (int8_t)TMS_REGISTER(tms9918, PICO9918_REG_VRAM_INC); // increment register
  return tms9918->readAheadBuffer;
}

/** \brief return the buffered value without reading VRAM or advancing the address */
PICO9918_INLINE_HOT uint8_t pico9918_read_data_no_inc_impl(PICO9918_INST_ONLY_ARG)
{
  return tms9918->readAheadBuffer;
}

/** \brief return true if both INT status and INT control set */
/**
 * \brief whether /INT should be asserted
 *
 * Two independent sources, as on the F18A: the end-of-frame flag under R1's enable, and
 * the scanline flag under R0's. Neither gates the other - a program that wants only the
 * scanline interrupt turns R1's off - so the horizontal source cannot be folded into SR0.
 * The scanline term leads with the flag because it is clear on almost every line, and the
 * whole term folds away in a TMS9918A build, which has no R19 to arm it.
 */
PICO9918_INLINE_HOT bool pico9918_interrupt_status_impl(PICO9918_INST_ONLY_ARG)
{
  return ((TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_INT_ENABLE) &&
          (TMS_STATUS(tms9918, PICO9918_SR_STATUS) & PICO9918_SR0_INT)) ||
         (PICO9918_UNLOCKED(tms9918) && (TMS_STATUS(tms9918, PICO9918_SR_IDENT) & PICO9918_SR1_HF) &&
          (TMS_REGISTER(tms9918, TMS_REG_0) & TMS_R0_INT_SCANLINE));
}

/** \brief raise the interrupt flag in SR0 and the frame shadow */
PICO9918_INLINE_HOT void pico9918_interrupt_set_impl(PICO9918_INST_ONLY_ARG)
{
  tms9918->frameStatus |= PICO9918_SR0_INT;
  TMS_STATUS(tms9918, PICO9918_SR_STATUS) |= PICO9918_SR0_INT;
}

/**
 * \brief set status flag
 *
 * Writes the SR0 shadow too. SR0 has exactly one authoritative value: the frame
 * path latches into the shadow and publishes it, so a setter that moved only the
 * register would leave the next latch merging into a stale value.
 */
PICO9918_INLINE_HOT void pico9918_set_status_impl(PICO9918_INST_ARG uint8_t status)
{
  tms9918->frameStatus   = status;
  TMS_STATUS(tms9918, PICO9918_SR_STATUS) = status;
}

/* Frame interrupt / status state accessors
 * ----------------------------------------
 * frameInt / frameStatus / frameDoneInt are NOT exposed as extern variables: both
 * the CPU-interface handlers and the frame path mutate them, so going through the
 * Impl layer keeps ownership in one place.
 */

/* the SR0 latch the frame path merges into */
PICO9918_INLINE uint8_t pico9918_frame_status_impl(PICO9918_INST_ONLY_ARG)
{
  return tms9918->frameStatus;
}

/* current /INT pin state */
PICO9918_INLINE bool pico9918_frame_int_impl(PICO9918_INST_ONLY_ARG)
{
  return tms9918->frameInt;
}

/* has an interrupt been raised this frame? */
PICO9918_INLINE bool pico9918_frame_done_int_impl(PICO9918_INST_ONLY_ARG)
{
  return tms9918->frameDoneInt;
}

PICO9918_INLINE void pico9918_set_frame_done_int_impl(PICO9918_INST_ARG bool done)
{
  tms9918->frameDoneInt = done;
}

/* Frame counter and dropped-frame accounting
 * ------------------------------------------
 * Defined in pico9918_frame.c, which owns every write. The host only READS them,
 * which is what makes plain module globals behind inline accessors the right shape
 * here, rather than the instance fields the interrupt/status state uses: that state
 * is mutated by BOTH the bus-interface handlers and the frame path, so ownership has
 * to be forced through one place. Nothing outside this module writes these, so there
 * is no such split to arbitrate, and `pico9918_palette_lut` below is the precedent
 * for a library-owned global on the privileged Impl surface.
 *
 * The choice is measured, not stylistic. The scanline path reads the count three
 * times per border scanline and must not gain work. As instance fields the offsets
 * land past the 64KB vram union, so each read needs a literal-pool offset AND the
 * instance base - two instructions more in the scanline path and two in the GPIO IRQ
 * handler, verified by building it both ways. As globals the pool holds the address
 * directly.
 *
 * Not volatile and not guarded: all writes are on core 1 (the frame path and the
 * reset IRQ, which is also core 1), and every reader is on core 1 too.
 */
extern int pico9918_frame_count;
extern int pico9918_dropped_frames_count;

#if PICO9918_DIAG_GPU_FRAME_COUNTER
/* GPU frames observed at end of frame, read by the diag overlay's GPU-frames row -
   which is why it is on this surface rather than a file static: the frame module
   writes it and the overlay reads it directly. */
extern uint32_t pico9918_gpu_frame_count;
#endif

/* Read-only from the host's point of view: the frame module owns the increment and
   the reset, and advances the global directly - the end-of-frame sequence is
   internal to it. The host's scanline reads this as the splash animation clock and
   the startup-diagnostics threshold. */
PICO9918_INLINE int pico9918_frame_count_impl(PICO9918_INST_ONLY_ARG)
{
  return pico9918_frame_count;
}

/* console-reset entry for the frame counter. Deliberately does NOT clear the
   dropped-frame window, and does NOT clear pico9918_valid_writes: the dropped-frame
   count is a rolling 16-frame average that is allowed to span a reset, and
   validWrites is a once-per-run latch whose consumers (the splash hand-off, the
   startup diagnostics screen) must not be re-armed by a console reset. */
PICO9918_INLINE void pico9918_frame_reset_count_impl(PICO9918_INST_ONLY_ARG)
{
  pico9918_frame_count = 0;
#if PICO9918_DIAG_GPU_FRAME_COUNTER
  pico9918_gpu_frame_count = 0;
#endif
}

/* Vertical geometry and the display-enable latch
 * ----------------------------------------------
 * Defined in pico9918_frame.c alongside the frame counter, for consistency of
 * ownership. Unlike the frame counter, globals are NOT cheaper here: MEASURED both
 * ways, instance fields make the scanline path two instructions SHORTER, and
 * inspecting the two disassemblies that difference is register-allocation churn in
 * the prologue rather than address arithmetic at the read sites. A codegen
 * coin-flip, not a structural cost - so do not assume either form is free without
 * looking.
 *
 * Globals are used because they match the frame counter these sit beside: one module
 * owns one kind of state one way.
 *
 * Not volatile and not guarded: the frame module is the only writer and the host's
 * scanline the only reader, both on core 1. */
extern int pico9918_v_pixels;
extern uint32_t pico9918_v_border;
extern bool pico9918_valid_writes;
extern uint8_t pico9918_v_scale;
extern uint16_t pico9918_v_virtual;

/* The border colour word the border-fill DMA instance reads. Declared here only so
   pico9918.c's initLookups() can point the fill
   instance at it: the frame module owns the storage, the placement and every write,
   and the init site owns every fill instance. Not an accessor, because there is
   nothing to accessorise - the one out-of-module user needs its ADDRESS, once, at
   init. Placement is .scratch_y; see the definition. */
extern uint32_t pico9918_border_bg;

/* Active VDP display lines, and the top border offset in virtual lines. Written
   once per frame by pico9918_frame_geometry; read by the host's scanline for its
   border test and for the overlay geometry it forwards.

   vBorder is UNSIGNED, and that is load-bearing rather than incidental - see the
   narrowing note on pico9918_frame_geometry_t in pico9918_frame.h. */
PICO9918_INLINE int pico9918_v_pixels_impl(PICO9918_INST_ONLY_ARG)
{
  return pico9918_v_pixels;
}

PICO9918_INLINE uint32_t pico9918_v_border_impl(PICO9918_INST_ONLY_ARG)
{
  return pico9918_v_border;
}

/* Has the VDP display been enabled at all since power-on or the last console
   reset? Latched by pico9918_frame_end on the first frame that sees R1 bit 6 set,
   and never cleared - the splash hand-off and the startup diagnostics screen both
   hang off it and are once-per-run. */
PICO9918_INLINE bool pico9918_valid_writes_impl(PICO9918_INST_ONLY_ARG)
{
  return pico9918_valid_writes;
}

/* Dropped frames over the trailing 16-frame window.
 *
 * RETAINED with no in-tree caller, deliberately: the frame module owns the counter
 * and runs the diagnostics refresh, so it reads its own global directly. The
 * accessor stays because the counter is a documented part of what this module
 * accounts for, and an integrator driving frames has no other way to read it.
 * Compare pico9918_frame_count_impl, which the host does read. */
PICO9918_INLINE int pico9918_dropped_frames_impl(PICO9918_INST_ONLY_ARG)
{
  return pico9918_dropped_frames_count;
}

/* Function:  pico9918_frame_map_line_impl
 * ---------------------------------------
 * the interlace field mapping: which VDP line a given display line renders.
 *
 * y:      the line WITHIN THE DISPLAY REGION, i.e. after the top border has been
 *         subtracted. Not the raw VGA-encoded line: the scanline path applies the
 *         mapping only on its active arm, where y is already border-relative, and the
 *         mapping must see the same value it saw before the move.
 * field:  the field number, which the caller has already separated out of the raw y's
 *         bit 12 - it needs it on the border arm too.
 * interlaced / fieldOrder: the mode's interlace parameters.
 *
 * Applied ONLY under interlace AND double-rows; otherwise the VDP line is y unchanged
 * and the field number is DISCARDED. The two fields interleave the doubled VDP line
 * space, and fieldOrder selects which field takes the even lines.
 *
 * An inline function rather than four lines inside pico9918_frame_scanline, and the
 * reason is the gate, not tidiness. The golden frame surface's mapping group calls
 * THIS, so it compares the shipping code against its own independent model. The
 * scanline as a whole cannot serve that purpose - it does DMA and renders a line.
 * This is the smallest thing that can.
 *
 * PICO9918_INLINE, so its one library caller inlines it and the per-scanline path is
 * unchanged.
 *
 * The `y * 2 + (field ^ fieldOrder)` FUSION IS DELIBERATE and must not be decomposed
 * into explicit even/odd cases. The harness's independent reference does exactly that
 * decomposition, and its whole value is that it does not share this algebra. */
PICO9918_INLINE uint16_t pico9918_frame_map_line_impl(PICO9918_INST_ARG uint16_t y, uint8_t field,
                                                      bool interlaced, uint8_t fieldOrder)
{
  uint16_t tmsY = y;
  if (interlaced && (TMS_REGISTER(tms9918, TMS_REG_0) & TMS_R0_DOUBLE_ROWS))
    tmsY = y * 2 + (field ^ fieldOrder);
  return tmsY;
}

/**
 * \brief recompute the interrupt state and, only if it changed, drive the pin.
 *
 * The single place the /INT pin is asserted or released from a recomputation.
 * Both the post-write reconcile and updateInterrupts' tail need exactly this.
 */
PICO9918_INLINE void pico9918_frame_sync_int_impl(PICO9918_INST_ONLY_ARG)
{
  bool newInt = pico9918_interrupt_status_impl(PICO9918_INST_ONLY);
  if (newInt != tms9918->frameInt)
  {
    tms9918->frameInt = newInt;
    PICO9918_HOST_SET_INT(newInt);
  }
}

/**
 * \brief CPU-interface entry: called after a register/address write. An R1 interrupt
 * enable/disable must take effect at once - updateInterrupts only runs on active
 * scanlines and at the trigger line, so without this a border-time R1 mask would
 * leave /INT stuck asserted.
 */
PICO9918_INLINE void pico9918_write_reconcile_int_impl(PICO9918_INST_ONLY_ARG)
{
  pico9918_frame_sync_int_impl(PICO9918_INST_ONLY);
}

/**
 * \brief console-reset entry for the interrupt/status state. frameDoneInt resets to
 * TRUE, not false: it suppresses the end-of-frame fallback interrupt until the
 * next frame starts cleanly.
 *
 * Does NOT drive the pin - the reset handler's tail order is load-bearing (host
 * read-ahead push and other non-VDP work run between this and the pin write), so
 * the caller issues PICO9918_HOST_SET_INT at its own point.
 */
PICO9918_INLINE void pico9918_frame_reset_int_impl(PICO9918_INST_ONLY_ARG)
{
  pico9918_set_status_impl(PICO9918_INST 0x00);
  tms9918->frameInt     = false;
  tms9918->frameDoneInt = true;
}


/* Palette LUT (pico9918_palette.c)
 * -----------------------------------
 * 256 entries consumed by PICO9918_EXPAND_INDEXED. Regular SRAM, not a scratch
 * bank - placement preserved from the firmware.
 *
 * NO HOST CODE MAY REFERENCE ANY OF THE THREE. They are here, on the
 * library-internal impl surface, because that is where library-internal state
 * belongs and they have in-library consumers: the frame module's scanline reads the
 * LUT and drives both rebuild triggers, and the golden harness drives the same
 * rebuild decision directly (test/golden/golden.c, the post-palette scene surface -
 * which is what makes palDirty observable at all). */
extern PICO9918_PALETTE_LUT_T pico9918_palette_lut[256];

#if !PICO9918_SINGLE_INSTANCE
extern const pico9918_t* pico9918_palette_owner;
#endif

void pico9918_palette_regenerate(PICO9918_INST_ONLY_ARG);

/* Does the LUT need rebuilding?
 *
 * SR2 bit 7 is F18A-SPECIFIC (the GPU busy flag): the GPU may have written
 * palette registers behind our back, so a rebuild is forced while it runs.
 * V9938's S#2 bit 7 is TR, which idles at 1 - under the V9938 base this must
 * be gated per base (step 6) or the palette would rebuild every scanline. */
PICO9918_INLINE bool pico9918_palette_dirty(PICO9918_INST_ONLY_ARG)
{
#if !PICO9918_SINGLE_INSTANCE
  /* the converted palette is one module-level LUT, so it belongs to whoever rebuilt it
     last: a second instance in the same mode would otherwise draw the first one's colours */
  if (pico9918_palette_owner != tms9918) return true;
#endif
  return tms9918->palDirty || (TMS_STATUS(tms9918, PICO9918_SR_GPU) & 0x80);
}
