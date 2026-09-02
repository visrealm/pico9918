/**
 * \file
 * \brief pico9918-core - Platform Abstraction (RP2040 / RP2350)
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Included by impl/platform.h when PICO_BUILD is defined.
 * Do not include directly.
 */

#pragma once

#include <stdint.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "pico/divider.h"

/*
 * Tier-1 host op: drive the /INT pin.
 *
 * A single gpio_put with COMPILE-TIME polarity. No runtime branch, no function
 * pointer - it expands on the per-active-scanline path.
 *
 * The host supplies the pin, and optionally the polarity, as compile definitions
 * reaching every library TU (the PICO9918 firmware emits both from its root
 * CMakeLists via add_definitions()):
 *
 *   PICO9918_INT_GPIO        - required. GPIO number driving /INT.
 *   PICO9918_INT_ACTIVE_HIGH - optional. Defined when the board drives INT high;
 *                            absent means active-low, which is the TMS9918A part.
 *
 * No default pin: guessing one would silently drive the wrong line if the host's
 * definition ever failed to arrive, and nothing downstream could detect it.
 */
#ifndef PICO9918_INT_GPIO
#error "PICO9918_INT_GPIO must be defined by the host build (the GPIO driving /INT)"
#endif

#ifdef PICO9918_INT_ACTIVE_HIGH
#define PICO9918_HOST_SET_INT(active) gpio_put(PICO9918_INT_GPIO, (active))
#else
#define PICO9918_HOST_SET_INT(active) gpio_put(PICO9918_INT_GPIO, !(active))
#endif


/*
 * Section placement - fast SRAM banks.
 */
#define PICO9918_SECTION_SCRATCH_X(name) __attribute__((section(".scratch_x." #name)))
#define PICO9918_SECTION_SCRATCH_Y(name) __attribute__((section(".scratch_y." #name)))

/* Objects every byte of which is written before anything reads them, so they can skip
   the crt0 .bss zero-fill. The SDK macro names the object, so it wraps the declarator. */
#define PICO9918_UNINITIALIZED(decl) __uninitialized_ram(decl)

/* Keep a cold function resident in flash (XIP) rather than copied to RAM, for a
   firmware built copy_to_ram with PICO9918_COLD_IN_FLASH on. Not the SDK's
   __in_flash_func: that host redefines it, and a library cannot depend on a host
   header. See pico9918's src/xip.h for the reachability rule this relies on. */
#if PICO9918_COLD_IN_FLASH
#define PICO9918_IN_FLASH_FUNC(fn) __attribute__((section(".flashcode." __STRING(fn)), noinline)) fn
#else
#define PICO9918_IN_FLASH_FUNC(fn) fn
#endif


/*
 * DMA abstraction
 *
 * THE CHANNEL NUMBERS ARE MACROS, NOT `extern unsigned int`, and that is a measured
 * requirement rather than a style choice. The SDK's dma_channel_* helpers index
 * dma_hw->ch[], so a COMPILE-TIME channel folds the whole thing to a store at a
 * fixed address held once in the literal pool. An extern is opaque to the TU, so
 * every access instead emits a load of the channel number, a shift by 6 to scale it
 * to the 64-byte channel stride, and an add - three extra instructions AT EVERY SITE.
 *
 * This is the same trap pico9918HostOps.h records for the PIO state-machine indices,
 * and it is a real one here: as externs, the seven fill sites on the scanline path
 * each pay a load, a shift and an add for nothing, and the library's own fills in
 * pico9918_scan_line and the mode renderers pay it too.
 *
 * A host that needs different channels overrides these before including the library,
 * which is the documented mechanism for every other PICO9918_* platform macro. They
 * must stay compile-time constants either way.
 *
 * COLLISION SAFETY. Every channel here is reserved at library init via
 * PICO9918_DMA_CLAIM below, which panics ("DMA channel %d is already claimed") if
 * anything else already holds one. dma_claim_unused_channel() was measured and
 * rejected: it forces the numbers to runtime values, which the fill and copy sites
 * would pay for at every expansion. Claiming a SPECIFIC channel gets the same
 * guarantee for free, and a stronger one - allocation order cannot silently shuffle
 * which channel the display owns.
 */

/*
 * 32-bit fill instances
 *
 * Each instance is an independent DMA channel with its own read address and
 * transfer count, so two fills can be in flight concurrently:
 *
 *   PICO9918_FILL_BORDER - the scanline's border fill
 *
 * Compile-time channel numbers, for the reason stated above: an instance then costs
 * NOTHING at runtime, each expansion resolving to the register writes alone.
 *
 * Usage:
 *   PICO9918_FILL32_INIT(inst, srcPtr)  once, at library/frame init - never lazily
 *   PICO9918_FILL32_SET_COUNT(inst, n)  set transfer count (no trigger)
 *   PICO9918_FILL32_TRIGGER(inst, dst)  start fill at dst, reusing the last count
 *   PICO9918_FILL32_WAIT(inst)          block until the fill completes
 *
 * SET_COUNT deliberately does not trigger, so a caller can "set count once,
 * trigger twice" (the left/right border pair).
 */
#ifndef PICO9918_FILL_BORDER
#define PICO9918_FILL_BORDER 2u
#endif

#define PICO9918_FILL32_INIT(inst, srcPtr) \
  do \
  { \
    dma_channel_config _cfg = dma_channel_get_default_config(inst); \
    channel_config_set_read_increment(&_cfg, false); \
    channel_config_set_write_increment(&_cfg, true); \
    channel_config_set_transfer_data_size(&_cfg, DMA_SIZE_32); \
    dma_channel_set_config((inst), &_cfg, false); \
    dma_channel_set_read_addr((inst), (srcPtr), false); \
  } while (0)

#define PICO9918_FILL32_SET_COUNT(inst, n)    dma_channel_set_trans_count((inst), (n), false)
#define PICO9918_FILL32_TRIGGER(inst, dstPtr) dma_channel_set_write_addr((inst), (dstPtr), true)
#define PICO9918_FILL32_WAIT(inst)            dma_channel_wait_for_finish_blocking(inst)

/*
 * Two more fill instances and a copy channel, for the unified tile pipeline.
 *
 *   PICO9918_FILL_MASKS  clears the row-mask block in one transfer
 *   PICO9918_FILL_LINE   lays the background colour across a line
 *   PICO9918_COPY        moves a layer run into the pixel line
 *
 * The copy channel runs byte-wide or word-wide depending on the source alignment of
 * the run, so it carries two configs and selects one per run rather than rebuilding
 * a config in the scanline path.
 */
#ifndef PICO9918_FILL_MASKS
#define PICO9918_FILL_MASKS 5u
#endif

#ifndef PICO9918_FILL_LINE
#define PICO9918_FILL_LINE 6u
#endif

#ifndef PICO9918_COPY
#define PICO9918_COPY 7u
#endif

extern dma_channel_config pico9918_copy_byte;
extern dma_channel_config pico9918_copy_word;

/* The library defines the two configs above; off-target they do not exist. */
#define PICO9918_COPY_STATE() \
  dma_channel_config pico9918_copy_byte; \
  dma_channel_config pico9918_copy_word;

#define PICO9918_COPY_INIT(inst) \
  do \
  { \
    dma_channel_config _cfg = dma_channel_get_default_config(inst); \
    channel_config_set_write_increment(&_cfg, true); \
    channel_config_set_high_priority(&_cfg, true); \
    channel_config_set_transfer_data_size(&_cfg, DMA_SIZE_8); \
    pico9918_copy_byte = _cfg; \
    channel_config_set_transfer_data_size(&_cfg, DMA_SIZE_32); \
    pico9918_copy_word = _cfg; \
    dma_channel_set_config((inst), &pico9918_copy_byte, false); \
  } while (0)

#define PICO9918_COPY_SET_WIDTH(inst, wordAligned) \
  dma_channel_set_config((inst), (wordAligned) ? &pico9918_copy_word : &pico9918_copy_byte, false)

#define PICO9918_COPY_SET_SRC(inst, srcPtr) dma_channel_set_read_addr((inst), (srcPtr), false)
#define PICO9918_COPY_SET_DST(inst, dstPtr) dma_channel_set_write_addr((inst), (dstPtr), false)
#define PICO9918_COPY_TRIGGER(inst, n)      dma_channel_set_trans_count((inst), (n), true)
#define PICO9918_COPY_WAIT(inst)            dma_channel_wait_for_finish_blocking(inst)

/* Integer divide-modulo for the F18A timing counters. The RP2040's SIO has a hardware
   divider behind this and the RP2350 does not, where the SDK's is software. Cold path. */
#define PICO9918_DIVMOD_U32(n, d, q, r) \
  do \
  { \
    divmod_result_t _dm = divmod_u32u32((n), (d)); \
    (q)                 = to_quotient_u32(_dm); \
    (r)                 = to_remainder_u32(_dm); \
  } while (0)

/*
 * Reserve every channel this platform header names. Called once from library init,
 * before any channel is configured. Panics on collision with any other user.
 *
 * dma_claim_mask takes the whole set in one call, so one bitmask replaces the
 * per-channel calls and folds to a single constant argument.
 */
#define PICO9918_DMA_CLAIM() \
  dma_claim_mask((1u << PICO9918_FILL_BORDER) | (1u << PICO9918_FILL_MASKS) | \
                 (1u << PICO9918_FILL_LINE) | (1u << PICO9918_COPY))


/*
 * Pixel output policy - BGR12 in the low 12 bits of a uint16_t (4-4-4, not 5-6-5).
 *
 * INPUT CONTRACT: the argument is a pram entry, which is *byte-swapped* RGB444,
 * i.e. 0xGB0R - NOT the canonical 0x0RGB the macro name suggests. pram is filled
 * either by bswap16 of a canonical 0xARGB literal (pico9918.c, palette reset
 * and pico9918_config.c) or directly in that order by the F18A DPM port
 * (pico9918_priv.h: palWriteStage0Value | (data << 8)). The name is therefore
 * a misnomer, kept until the pico9918-core fork renames it (plan section 10).
 *
 *   in:  bits 15-12 = G, 11-8 = B, 7-4 = A or 0, 3-0 = R
 *   out: bits 15-12 = G (dead), 11-8 = B, 7-4 = G, 3-0 = R
 *
 *   data &= 0xFF0F;              clear bits 7-4. On the DPM path they are already
 *                                zero, but the palette-reset path byte-swaps a
 *                                0xARGB literal and lands ALPHA there - this is
 *                                what strips it. Not optional.
 *   data |= ((data >> 12) << 4); copy GREEN (not red) down into bits 7-4.
 *
 * So it is a bit shuffle, not a colour-space conversion. Verified against the DAC:
 * the RGB pin group is 12 wide starting at GPIO2 (vga.h VGA_RGB_PINS_COUNT,
 * vga.c sm_config_set_out_pins) with shift-right OSR, so word bit 0 drives red's
 * LSB, bit 4 green's, bit 8 blue's; bits 15-12 never reach a pin.
 *
 * CAUTION - bits 15-12 are dead at the pin boundary but NOT everywhere: the
 * RP2040 CRT-scanline dim shifts the whole word right by one UNMASKED
 * (vga.c, currentBuffer[i] >>= 1), so anything in bit 12 lands in bit 11 -
 * blue's MSB. Keep the top nibble zero in any value that can reach the
 * framebuffer. The RP2350 branch masks 0x07770777 and is unaffected.
 */
typedef uint16_t PICO9918_PIXEL_T;

#define PICO9918_PIXEL_FROM_RGB12(rgb) ((PICO9918_PIXEL_T)(((rgb) & 0xFF0F) | ((((rgb) & 0xFF0F) >> 12) << 4)))

/* Both entries of a word at once: the transform above is a masked nibble move, so
   one xor-and-mask over the pair does what two applications would. */
#define PICO9918_PIXEL_FROM_RGB12_PAIR(packed) \
  ((packed) ^ (((packed) ^ ((packed) >> 8)) & 0x00F000F0u))

/* The same 16-bit pixel in both halves of a word, so a writer emits two pixels at a
   time. The RP2040 arm avoids GCC's multi-instruction expansion of the constant
   multiply, and its low-half extraction avoids the shift pair. */
#if PICO_RP2040
#define PICO9918_PIXEL_PAIR(p) \
  __extension__({ \
    uint32_t _v = (p), _f = 0x10001u; \
    __asm__("mul %0, %1" : "+l"(_v) : "l"(_f) : "cc"); \
    _v; \
  })
#define PICO9918_LOW16(x) \
  __extension__({ \
    uint32_t _l, _x = (x); \
    __asm__("uxth %0, %1" : "=l"(_l) : "l"(_x)); \
    _l; \
  })
#else
#define PICO9918_PIXEL_PAIR(p) ((uint32_t)(p) * 0x10001u)
#define PICO9918_LOW16(x)      ((uint32_t)(uint16_t)(x))
#endif

/* Dim an existing pixel (diagnostics overlay) - two stops down, per channel. */
#define PICO9918_PIXEL_DARKEN(p) ((PICO9918_PIXEL_T)(((p) >> 2) & 0x333))

/* One stop down, both pixels of a word at once - the CRT-scanline effect. The mask is
   what keeps each channel's LSB out of the channel below it, and out of the next
   pixel's MSB. vga.c omits it on RP2040, where the strays are dead at the pins and the
   pindir mask covers the rest; anything writing to a host buffer cannot. */
#define PICO9918_PIXEL_PAIR_DIM(w) (((w) >> 1) & 0x07770777u)

/*
 * The unit the overlay's glyph blit works on. Two pixels to a word here, so a
 * glyph cell is three stores rather than six, and the darkened background falls
 * out of the same masked expression as the ink.
 */
typedef uint32_t PICO9918_INK_T;
#define PICO9918_INK_PIXELS    2
#define PICO9918_INK_FILL(fg)  PICO9918_PIXEL_PAIR(fg)
#define PICO9918_INK_DARKEN(w) (((w) >> 2) & 0x03330333u)
#define PICO9918_INK_ONE(k)    (0xffffu << ((k) * 16))

/*
 * Palette LUT - 256 entries of a packed pixel *pair* (two 16-bit pixels in one
 * 32-bit word), so the expansion loop emits one store per two output pixels.
 */
typedef uint32_t PICO9918_PALETTE_LUT_T;

/*
 * Indexed bytes -> pixel pairs. 8-way unrolled; n is a multiple of 8
 * (TMS9918_PIXELS_X and its V9938 multiples all are).
 */
#if PICO_RP2040
#include "hardware/interp.h"

/*
 * Four packed indices in, four LUT addresses out, from the two interpolators - so the
 * line is read a word at a time and the LUT is never indexed here. PICO9918_EXPAND_INIT
 * points them at the LUT, and must run on the core that does the expanding: the
 * interpolators are per-core state.
 */
#define PICO9918_EXPAND_INIT(lut) \
  do \
  { \
    const uint8_t _shift[4] = {0, 8, 14, 22}; \
    for (uint _i = 0; _i < 4; ++_i) \
    { \
      interp_hw_t* _interp = (_i & 2) ? interp1 : interp0; \
      const uint _lane     = _i & 1; \
      interp_config _c     = interp_default_config(); \
      interp_config_set_shift(&_c, _shift[_i]); \
      interp_config_set_mask(&_c, 2, 9); \
      interp_config_set_cross_input(&_c, _lane == 1); \
      interp_set_config(_interp, _lane, &_c); \
      interp_set_base(_interp, _lane, (uintptr_t)(lut)); \
    } \
  } while (0)

#define PICO9918_EXPAND_INDEXED(dst, src, n, lut) \
  do \
  { \
    const uint32_t* _s = (const uint32_t*)(src); \
    const uint32_t* _e = _s + (n) / 4; \
    uint32_t* _d       = (uint32_t*)(dst); \
    while (_s < _e) \
    { \
      uint32_t _w       = *_s++; \
      interp1->accum[0] = _w; \
      interp0->accum[0] = _w << 2; \
      _d[0]             = *(const uint32_t*)interp0->peek[0]; \
      _d[1]             = *(const uint32_t*)interp0->peek[1]; \
      _d[2]             = *(const uint32_t*)interp1->peek[0]; \
      _d[3]             = *(const uint32_t*)interp1->peek[1]; \
      _w                = *_s++; \
      interp1->accum[0] = _w; \
      interp0->accum[0] = _w << 2; \
      _d[4]             = *(const uint32_t*)interp0->peek[0]; \
      _d[5]             = *(const uint32_t*)interp0->peek[1]; \
      _d[6]             = *(const uint32_t*)interp1->peek[0]; \
      _d[7]             = *(const uint32_t*)interp1->peek[1]; \
      _d += 8; \
    } \
  } while (0)
#else
/*
 * The RP2350 has interpolators; what it lacks is the IOPORT that makes a peek free. Over AHB a
 * peek costs about an SRAM read while Thumb-2 folds the indexing into the load, so routing
 * through them would add work here - and its SHIFT rotates rather than shifts, so the lane
 * config above would not port as written.
 */
#define PICO9918_EXPAND_INIT(lut) ((void)0)

#define PICO9918_EXPAND_INDEXED(dst, src, n, lut) \
  do \
  { \
    const uint8_t* _s              = (const uint8_t*)(src); \
    const uint8_t* _e              = _s + (n); \
    uint32_t* _d                   = (uint32_t*)(dst); \
    const PICO9918_PALETTE_LUT_T* _l = (lut); \
    while (_s < _e) \
    { \
      _d[0] = _l[_s[0]]; \
      _d[1] = _l[_s[1]]; \
      _d[2] = _l[_s[2]]; \
      _d[3] = _l[_s[3]]; \
      _d[4] = _l[_s[4]]; \
      _d[5] = _l[_s[5]]; \
      _d[6] = _l[_s[6]]; \
      _d[7] = _l[_s[7]]; \
      _d += 8; \
      _s += 8; \
    } \
  } while (0)
#endif

/*
 * The 80-column 8bpp line, which is already at full pixel width: a LUT entry holds the
 * colour in both halves, so a destination word is two indices and two lookups rather
 * than one index doubled. Only reachable on a board with that tier, which is why it
 * needs no RP2040 arm.
 */
#define PICO9918_EXPAND_INDEXED_WIDE(dst, src, n, lut) \
  do \
  { \
    const uint8_t* _s              = (const uint8_t*)(src); \
    const uint8_t* _e              = _s + (n); \
    uint32_t* _d                   = (uint32_t*)(dst); \
    const PICO9918_PALETTE_LUT_T* _l = (lut); \
    while (_s < _e) \
    { \
      _d[0] = (_l[_s[0]] & 0xffff) | (_l[_s[1]] << 16); \
      _d[1] = (_l[_s[2]] & 0xffff) | (_l[_s[3]] << 16); \
      _d[2] = (_l[_s[4]] & 0xffff) | (_l[_s[5]] << 16); \
      _d[3] = (_l[_s[6]] & 0xffff) | (_l[_s[7]] << 16); \
      _d += 4; \
      _s += 8; \
    } \
  } while (0)
