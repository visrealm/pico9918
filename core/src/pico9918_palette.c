/**
 * \file
 * \brief pico9918-core - Palette to pixel-pair LUT
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Builds the 256-entry LUT that the scanline expansion consumes. The indexed
 * scanline buffer is always a 256-byte stream; only the meaning of a byte
 * changes per mode, so the build is selected once per rebuild and never per
 * pixel.
 *
 * The colour transform itself is host policy - PICO9918_PIXEL_FROM_RGB12 - so
 * this TU is portable and the Pico build still emits the BGR12 pair trick.
 * (A pram entry is byte-swapped RGB444, 0xGB0R; the macro's input contract is
 * documented in platform/pico/platform_pico.h.)
 */

#include "impl/pico9918_priv.h"

/*
 * The pixel LUT. Regular SRAM, NOT a scratch bank - preserved from the
 * firmware exactly; scratch-placing it is a separate measured experiment.
 */
PICO9918_PALETTE_LUT_T __aligned(4) pico9918_palette_lut[256];

#if !PICO9918_SINGLE_INSTANCE
const pico9918_t* pico9918_palette_owner = 0;
#endif

/* Word reads over the uint16_t palette, so the two-entry load is not a strict-aliasing
   violation. Both entries are converted from that one load - see
   PICO9918_PIXEL_FROM_RGB12_PAIR, which is the same policy applied to each half. */
typedef uint32_t PICO9918_MAY_ALIAS pico9918_aliasing_u32_t;
_Static_assert(_Alignof(pico9918_t) >= 4, "palette LUT build requires word alignment");
_Static_assert(offsetof(pico9918_t, vram.map.pram) % 4 == 0, "palette LUT build must be word aligned");

/* Convert two adjacent palette entries as doubled pixels, and return the converted
   pair packed into a word for the paired build below to reuse. */
static inline uint32_t cachePixelPair(PICO9918_PALETTE_LUT_T* dest, const uint16_t* source)
{
  const pico9918_aliasing_u32_t* alignedSource =
    (const pico9918_aliasing_u32_t*)PICO9918_ASSUME_ALIGNED(source, 4);
  const uint32_t packed                 = PICO9918_PIXEL_FROM_RGB12_PAIR(*alignedSource);
  dest[0]                               = PICO9918_PIXEL_PAIR(PICO9918_LOW16(packed));
  dest[1]                               = PICO9918_PIXEL_PAIR(packed >> 16);
  return packed;
}

PICO9918_NOINLINE void pico9918_palette_regenerate(PICO9918_INST_ONLY_ARG)
{
  tms9918->palDirty = 0;
#if !PICO9918_SINGLE_INSTANCE
  pico9918_palette_owner = tms9918;
#endif

  const bool pixelsDoubled = pico9918_display_mode(PICO9918_INST_ONLY) != TMS_MODE_TEXT80 ||
                             pico9918_line_bytes(PICO9918_INST_ONLY) != TMS9918_PIXELS_X;
  const uint16_t* source   = tms9918->vram.map.pram;

  if (pixelsDoubled)
  {
    for (int i = 0; i < 64; i += 8)
    {
      cachePixelPair(pico9918_palette_lut + i + 0, source + i + 0);
      cachePixelPair(pico9918_palette_lut + i + 2, source + i + 2);
      cachePixelPair(pico9918_palette_lut + i + 4, source + i + 4);
      cachePixelPair(pico9918_palette_lut + i + 6, source + i + 6);
    }
  }
  else
  {
    uint16_t __aligned(4) tmpPal[16];
    for (int i = 0; i < 16; i += 2)
    {
      const uint32_t packed     = cachePixelPair(pico9918_palette_lut + i, source + i);
      pico9918_aliasing_u32_t* dest    = (pico9918_aliasing_u32_t*)PICO9918_ASSUME_ALIGNED(tmpPal + i, 4);
      *dest                     = packed;
    }
    for (int high = 1; high < 16; ++high)
    {
      const uint32_t highPixel      = tmpPal[high];
      PICO9918_PALETTE_LUT_T* dest    = pico9918_palette_lut + (high << 4);
#if defined(__GNUC__)
#pragma GCC unroll 1
#endif
      for (int low = 0; low < 16; low += 2)
      {
        const pico9918_aliasing_u32_t* pairSource =
          (const pico9918_aliasing_u32_t*)PICO9918_ASSUME_ALIGNED(tmpPal + low, 4);
        uint32_t pair                      = *pairSource;
        dest[low]                          = (pair << 16) | highPixel;
        dest[low + 1]                      = (pair & 0xFFFF0000) | highPixel;
      }
    }
  }
}
