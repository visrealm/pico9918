/**
 * \file
 * \brief pico9918-core - Platform Abstraction (portable C)
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Included by impl/platform.h when PICO_BUILD is not defined.
 * Do not include directly.
 *
 * Every macro here is overridable: a host may #define any PICO9918_* symbol
 * before including pico9918.h to substitute its own (a real lock, a BGRA
 * pixel type, a different fill). Defaults are guarded accordingly.
 */

#pragma once

#include <stdint.h>
#include <string.h>

/* No fast SRAM banks off-target. */
#define PICO9918_SECTION_SCRATCH_X(name)
#define PICO9918_SECTION_SCRATCH_Y(name)

/* No crt0 zero-fill to opt out of either - the declarator stands as written. */
#define PICO9918_UNINITIALIZED(decl) decl

/* Nothing is copied to RAM off-target, so nothing has to be held back from it. */
#define PICO9918_IN_FLASH_FUNC(fn) fn


/*
 * Tier-1 host op: drive the /INT pin.
 *
 * No pin off-target. A host that wants the edge (an emulator raising a CPU IRQ
 * line) pre-defines this before including the library - the same override rule
 * as every other macro here. pico9918_interrupt_status() remains available for
 * hosts that prefer to poll.
 *
 * The Pico platform header instead requires PICO9918_INT_GPIO and honours an
 * optional PICO9918_INT_ACTIVE_HIGH; neither has meaning off-target.
 */
#ifndef PICO9918_HOST_SET_INT
#define PICO9918_HOST_SET_INT(active) ((void)(active))
#endif




/*
 * 32-bit fill instances
 *
 * Mirrors the Pico DMA instances (see platform/pico) with independent
 * src/count slots, so two fills can be configured at once. Fills execute
 * synchronously, which makes WAIT a no-op.
 */
typedef struct
{
  const void* src;
  unsigned count;
} pico9918_fill32_t;

extern pico9918_fill32_t pico9918_fill_border;

#define PICO9918_FILL_BORDER pico9918_fill_border

#define PICO9918_FILL32_INIT(inst, srcPtr) \
  do \
  { \
    (inst).src   = (srcPtr); \
    (inst).count = 0; \
  } while (0)
#define PICO9918_FILL32_SET_COUNT(inst, n) \
  do \
  { \
    (inst).count = (unsigned)(n); \
  } while (0)

#define PICO9918_FILL32_TRIGGER(inst, dstPtr) \
  do \
  { \
    uint32_t _v  = *(const uint32_t*)(inst).src; \
    uint32_t* _d = (uint32_t*)(dstPtr); \
    for (unsigned _i = 0; _i < (inst).count; ++_i) _d[_i] = _v; \
  } while (0)

#define PICO9918_FILL32_WAIT(inst) ((void)0) /* TRIGGER is synchronous */

/* No DMA hardware to reserve off-target - the instances above are plain structs. */
#define PICO9918_DMA_CLAIM() ((void)0)

/*
 * Two more fill instances and a copy channel, matching platform/pico. Off-target the
 * fills are plain structs and the copy is memcpy, so the width hint is ignored: it
 * exists only so the Pico side can pick its transfer size from source alignment.
 */
extern pico9918_fill32_t pico9918_fill_masks;
extern pico9918_fill32_t pico9918_fill_line;

#define PICO9918_FILL_MASKS pico9918_fill_masks
#define PICO9918_FILL_LINE  pico9918_fill_line

/* `shift` carries the transfer width, because the count TRIGGER takes is a transfer
   count on the Pico side, not a byte count: word-wide runs move four bytes each. */
typedef struct
{
  const void* src;
  void* dst;
  unsigned shift;
} pico9918_copy32_t;

extern pico9918_copy32_t pico9918_copy;

#define PICO9918_COPY pico9918_copy

/* No configs to cache off-target. */
#define PICO9918_COPY_STATE()
#define PICO9918_COPY_INIT(inst) \
  do \
  { \
    (inst).shift = 0u; \
  } while (0)
#define PICO9918_COPY_SET_WIDTH(inst, wordAligned) \
  do \
  { \
    (inst).shift = (wordAligned) ? 2u : 0u; \
  } while (0)
#define PICO9918_COPY_SET_SRC(inst, srcPtr) \
  do \
  { \
    (inst).src = (srcPtr); \
  } while (0)
#define PICO9918_COPY_SET_DST(inst, dstPtr) \
  do \
  { \
    (inst).dst = (dstPtr); \
  } while (0)
#define PICO9918_COPY_TRIGGER(inst, n) memcpy((inst).dst, (inst).src, (size_t)(n) << (inst).shift)
#define PICO9918_COPY_WAIT(inst)       ((void)0) /* TRIGGER is synchronous */

/* Plain division off-target; the Pico arm goes through the SDK's divider. */
#define PICO9918_DIVMOD_U32(n, d, q, r) \
  do \
  { \
    uint32_t _n = (uint32_t)(n), _d = (uint32_t)(d); \
    (q) = _n / _d; \
    (r) = _n % _d; \
  } while (0)


/*
 * Pixel output policy - RGBA8888 in memory order (R,G,B,A at ascending bytes),
 * which is what SDL_PIXELFORMAT_RGBA32 / a GL RGBA8 texture expect on a
 * little-endian host.
 */
#ifndef PICO9918_PIXEL_T
typedef uint32_t PICO9918_PIXEL_T;
#define PICO9918_PIXEL_T PICO9918_PIXEL_T
#endif

/*
 * Expand a palette entry to RGBA8888. Each 4-bit channel is replicated into
 * 8 bits (0xf -> 0xff), alpha is opaque.
 *
 * BROKEN - DO NOT USE. This reads the entry as canonical
 * 0x0RGB, but a real pram entry is byte-swapped RGB444 (0xGB0R) - see the input
 * contract documented in platform/pico/platform_pico.h. Fed actual pram it
 * reads the always-set alpha/pad nibble as green and transposes red with blue, so
 * EVERY colour comes out with G=0xff: CGA blue (0xF00A -> pram 0x0AF0) renders as
 * R=AA G=FF B=00, bright yellow.
 *
 * Not reachable today - the golden harness selects the shipping Pico policy
 * (see test/golden/goldenPixelPolicy.h) and no emulator consumes the expansion
 * yet. This is a SECOND desktop defect, distinct from the LUT pair-packing
 * overflow. A correct version must either byte-swap first or read nibbles 15-12=G,
 * 11-8=B, 3-0=R directly.
 *
 * NOBODY IS ON EITHER, deliberately: neither is reachable from the Pico path, so
 * fixing them is unrelated work until the desktop pixel path is load-bearing. They
 * belong to whoever makes it so - the first emulator consumer, or V9938 work needing
 * desktop output.
 */
#ifndef PICO9918_PIXEL_FROM_RGB12
#define PICO9918_PIXEL_FROM_RGB12(rgb) \
  ((PICO9918_PIXEL_T)(((uint32_t)((((rgb) >> 8) & 0x0f) * 0x11)) | \
                    ((uint32_t)((((rgb) >> 4) & 0x0f) * 0x11) << 8) | \
                    ((uint32_t)((((rgb)) & 0x0f) * 0x11) << 16) | 0xff000000u))
#endif

/* Both entries of a word at once. No assumption about the transform here - it is
   applied to each half - so this only holds where a pixel fits in 16 bits, which is
   the same condition the paired (TEXT80) LUT build already carries. */
#ifndef PICO9918_PIXEL_FROM_RGB12_PAIR
#define PICO9918_PIXEL_FROM_RGB12_PAIR(packed) \
  ((((uint32_t)PICO9918_PIXEL_FROM_RGB12((packed) >> 16)) << 16) | \
   (uint16_t)PICO9918_PIXEL_FROM_RGB12((uint16_t)(packed)))
#endif

#ifndef PICO9918_PIXEL_PAIR
#define PICO9918_PIXEL_PAIR(p) ((uint32_t)(p) * 0x10001u)
#endif

#ifndef PICO9918_LOW16
#define PICO9918_LOW16(x) ((uint32_t)(uint16_t)(x))
#endif

/* Dim an existing pixel - two stops down, per channel, alpha preserved. */
#ifndef PICO9918_PIXEL_DARKEN
#define PICO9918_PIXEL_DARKEN(p) ((PICO9918_PIXEL_T)((((p) >> 2) & 0x003f3f3fu) | ((p) & 0xff000000u)))
#endif

/* The unit the overlay's glyph blit works on: one pixel to a word off-target,
   since a pixel already fills one. */
#ifndef PICO9918_INK_T
typedef PICO9918_PIXEL_T PICO9918_INK_T;
#define PICO9918_INK_T PICO9918_INK_T
#define PICO9918_INK_PIXELS    1
#define PICO9918_INK_FILL(fg)  (fg)
#define PICO9918_INK_DARKEN(w) PICO9918_PIXEL_DARKEN(w)
#define PICO9918_INK_ONE(k)    ((PICO9918_INK_T)~(PICO9918_INK_T)0)
#endif

/*
 * Palette LUT - one pixel per entry (no pair packing off-target).
 */
#ifndef PICO9918_PALETTE_LUT_T
typedef PICO9918_PIXEL_T PICO9918_PALETTE_LUT_T;
#define PICO9918_PALETTE_LUT_T PICO9918_PALETTE_LUT_T
#endif

/* Nothing per-core to set up off-target. */
#ifndef PICO9918_EXPAND_INIT
#define PICO9918_EXPAND_INIT(lut) ((void)0)
#endif

/* The 80-column 8bpp line - see the Pico header. Packing two pixels per word assumes a
   16-bit pixel, the same condition the paired LUT build already carries. */
#ifndef PICO9918_EXPAND_INDEXED_WIDE
#define PICO9918_EXPAND_INDEXED_WIDE(dst, src, n, lut) \
  do \
  { \
    const uint8_t* _s              = (const uint8_t*)(src); \
    uint32_t* _d                   = (uint32_t*)(dst); \
    const PICO9918_PALETTE_LUT_T* _l = (lut); \
    for (unsigned _i = 0; _i < (unsigned)(n); _i += 2) \
      _d[_i / 2] = (_l[_s[_i]] & 0xffff) | (_l[_s[_i + 1]] << 16); \
  } while (0)
#endif

#ifndef PICO9918_EXPAND_INDEXED
#define PICO9918_EXPAND_INDEXED(dst, src, n, lut) \
  do \
  { \
    const uint8_t* _s              = (const uint8_t*)(src); \
    PICO9918_PIXEL_T* _d             = (PICO9918_PIXEL_T*)(dst); \
    const PICO9918_PALETTE_LUT_T* _l = (lut); \
    for (unsigned _i = 0; _i < (unsigned)(n); ++_i) _d[_i] = _l[_s[_i]]; \
  } while (0)
#endif


/*
 * Timer abstraction
 * time_us_32() returns a 32-bit microsecond counter.
 */
#ifdef _WIN32
#include <windows.h>
static inline uint32_t time_us_32(void)
{
  LARGE_INTEGER freq, cnt;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&cnt);
  return (uint32_t)((cnt.QuadPart * 1000000ULL) / freq.QuadPart);
}
#else
#include <time.h>
static inline uint32_t time_us_32(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000000UL + ts.tv_nsec / 1000UL);
}
#endif


/*
 * __time_critical_func / __not_in_flash_func
 * Provided by pico/stdlib.h on Pico; no-ops here.
 */
#ifndef __time_critical_func
#define __time_critical_func(fn) fn
#endif
#ifndef __not_in_flash_func
#define __not_in_flash_func(fn) fn
#endif
#ifndef __force_inline
#define __force_inline inline
#endif
#ifndef __aligned
#if defined(_MSC_VER) && !defined(__clang__)
#define __aligned(n) __declspec(align(n))
#else
#define __aligned(n) __attribute__((aligned(n)))
#endif
#endif


/*
 * __builtin_bswap16
 * GCC/Clang provide this intrinsic; MSVC does not.
 */
#if defined(_MSC_VER) && !defined(__clang__)
#include <stdlib.h>
#define __builtin_bswap16(x) _byteswap_ushort(x)
#endif
