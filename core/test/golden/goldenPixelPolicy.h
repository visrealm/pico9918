/**
 * \file
 * \brief pico9918-core - the golden harness's pixel policy: Pico BGR12 on a desktop
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Force-included (-include) into every TU of the golden build - the library
 * AND the harness - so the desktop build runs the SHIPPING pixel policy
 * instead of the desktop default.
 *
 * Why the goldens pin the Pico policy and not the desktop default:
 *
 *   The pixel policy is host-overridable by design (see platform/desktop -
 *   every PICO9918_* symbol is \#ifndef-guarded for exactly this), and the harness
 *   uses that mechanism to check the documented pack formula against the policy
 *   that actually ships. The BGR12 formula is what a device produces; capturing the
 *   desktop RGBA8888 expansion instead would pin a surface no device ever emits -
 *   and a doubly broken one, since platform/desktop's FROM_RGB12 also reads the
 *   wrong nibbles for a real pram entry (every colour gets G=0xff).
 *
 *   It would also pin a broken one. pico9918_palette.c is written for a
 *   16-bit pixel: the DOUBLED class packs a pixel pair with
 *   `PICO9918_PIXEL_FROM_RGB12(data) * 0x10001` and the PAIRED class stages
 *   through `uint16_t tmpPal[16]`. Under the desktop 32-bit RGBA pixel the
 *   multiply overflows (0xffaa00cc -> 0x007600cc) and tmpPal truncates
 *   (0xffaa00cc -> 0x000000cc), so the desktop LUT is corrupt today. That
 *   defect is real and reported separately; it is NOT baked into the goldens.
 *
 * The definitions below are copied from platform/pico/platform_pico.h
 * (which cannot be included off-target - it pulls in hardware/dma.h). Keeping
 * them textually identical is the point: the harness runs the real formula.
 * The independent cross-check that this formula is CORRECT lives in golden.c
 * (refPixel), written from the prose spec rather than copied.
 *
 * The input is a pram entry: byte-swapped RGB444, 0xGB0R. Output is BGR12 in the
 * low 12 bits with a dead copy of green in 15-12. The full contract, and why the
 * 0xFF0F mask is not optional, is in platform/pico/platform_pico.h.
 */

#pragma once

#include <stdint.h>

#define PICO9918_PIXEL_T uint16_t

#define PICO9918_PIXEL_FROM_RGB12(rgb) \
  ((PICO9918_PIXEL_T)(((rgb) & 0xFF0F) | ((((rgb) & 0xFF0F) >> 12) << 4)))

#define PICO9918_PIXEL_DARKEN(p)  ((PICO9918_PIXEL_T)(((p) >> 2) & 0x333))

#define PICO9918_INK_T uint32_t
#define PICO9918_INK_PIXELS    2
#define PICO9918_INK_FILL(fg)  ((uint32_t)(fg) * 0x10001u)
#define PICO9918_INK_DARKEN(w) (((w) >> 2) & 0x03330333u)
#define PICO9918_INK_ONE(k)    (0xffffu << ((k) * 16))

#define PICO9918_PALETTE_LUT_T uint32_t

#define PICO9918_EXPAND_INDEXED(dst, src, n, lut) \
  do { \
    const uint8_t* _s = (const uint8_t*)(src); \
    const uint8_t* _e = _s + (n); \
    uint32_t* _d = (uint32_t*)(dst); \
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
