/**
 * \file
 * \brief pico9918-core - Splash overlay
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 */

#include "splash.h"

#include <stdbool.h>

#if !PICO9918_NO_SPLASH

/* Generated from PICO9918_SPLASH_IMAGE. The generator is told the symbol base
 * name, so the board-conditional asset still yields splash* here and this TU
 * carries no per-board #ifdef. */
#include "overlay/bmp_splash.h"

#if PICO9918_BUILD_RUNTIME_CHIP
#include "overlay/bmp_splash_pro.h"

/* Which artwork the personality wants. Set from pico9918_set_chip, where everything else
   derived from the personality is worked out once. File scope like the animation state
   above it, which is already shared. */
static bool splashIsPro = false;

/* The band is positioned from SPLASH_HEIGHT at compile time, so the two must agree. */
PICO9918_STATIC_ASSERT(SPLASHPRO_HEIGHT == SPLASH_HEIGHT,
                       "the PRO splash is a different height - SPLASH_START_POS assumes one band");

void pico9918_splash_select_pro(bool pro)
{
  splashIsPro = pro;
}
#endif

#define SPLASH_ENTER_FRAMES 60
#define SPLASH_HOLD_FRAMES  180
/* SPLASH_HEIGHT, not the splashHeight const int: this initialises a static, and
 * reading a const object is not a constant expression in standard C (GCC allows
 * it as an extension, MSVC rejects it). Same value, from the same generator. */
#define SPLASH_START_POS (SPLASH_ENTER_FRAMES + SPLASH_HEIGHT + 2)

static int logoOffset     = SPLASH_START_POS;
static bool canHideSplash = false;

#endif

/*
 * reset the splash popup (after... reset)
 */
void pico9918_splash_reset(void)
{
#if !PICO9918_NO_SPLASH
  logoOffset = SPLASH_START_POS;
#endif
}

void pico9918_splash_allow_hide(void)
{
#if !PICO9918_NO_SPLASH
  canHideSplash = true;
#endif
}

/*
 * output the PICO9918 splash logo / firmware version at the bottom of the screen
 */
void pico9918_splash_render(uint16_t y, uint32_t frameCount, uint32_t vBorder, uint32_t vPixels,
                          uint32_t vVirtualPixels, PICO9918_PIXEL_T* pixels)
{
#if PICO9918_NO_SPLASH
  (void)y;
  (void)frameCount;
  (void)vBorder;
  (void)vPixels;
  (void)vVirtualPixels;
  (void)pixels;
#else

  if (y == 0)
  {
    if (frameCount < SPLASH_ENTER_FRAMES)
      --logoOffset;
    else if (canHideSplash && frameCount > (SPLASH_ENTER_FRAMES + SPLASH_HOLD_FRAMES))
      ++logoOffset;
  }

  if (y <= vVirtualPixels)
  {
    /* WARNING: the 16-bit wraparound is the row gate, so do not "fix" the narrowing with a cast or a
     * signed rewrite. logoOffset goes negative, so a row outside the logo band wraps to a large
     * uint16 and fails the test below. MSVC C4244 may fire; the truncation is the mechanism. */
    y -= vBorder + vPixels + logoOffset;
    if (y < splashHeight)
    {
      const int leftBorderPx     = 4;
      const int splashBpp        = 2;
      const int splashPixPerByte = 8 / splashBpp;

#if PICO9918_BUILD_RUNTIME_CHIP
      uint8_t* const art          = splashIsPro ? splashPro : splash;
      const int artWidth          = splashIsPro ? splashProWidth : splashWidth;
      const PICO9918_PIXEL_T* pal = splashIsPro ? splashPro_pal : splash_pal;
#else
      uint8_t* const art          = splash;
      const int artWidth          = splashWidth;
      const PICO9918_PIXEL_T* pal = splash_pal;
#endif

      uint8_t* splashPtr = art + (y * artWidth / splashPixPerByte);

      for (int x = leftBorderPx; x < leftBorderPx + artWidth; x += splashPixPerByte)
      {
        uint8_t c       = *(splashPtr++);
        uint8_t pixMask = 0xc0;
        uint8_t offset  = 6;

        for (int px = 0; px < 4; ++px, offset -= 2, pixMask >>= 2)
        {
          uint8_t palIndex = (c & pixMask) >> offset;
          if (palIndex) pixels[x + px] = pal[palIndex];
        }
      }
    }
  }
#endif
}

#if PICO9918_BUILD_RUNTIME_CHIP

#include "overlay/bmp_f18a_badge.h"

_Static_assert(F18ABADGE_HEIGHT == PICO9918_F18A_BADGE_HEIGHT,
               "the badge asset is not the height the renderer draws");
_Static_assert(F18ABADGE_WIDTH == PICO9918_F18A_BADGE_WIDTH,
               "the badge asset is not the width the renderer draws");
/* img2carray.py emits a 1bpp byte only on every eighth pixel, so any other width
   loses its last columns with no warning. */
_Static_assert(F18ABADGE_WIDTH % 8 == 0, "the badge asset width must be a multiple of 8");

bool pico9918_f18a_badge_render(uint16_t outputLine, uint32_t frameCount, PICO9918_PIXEL_T* pixels)
{
  if (frameCount >= PICO9918_F18A_BADGE_FRAMES || outputLine >= PICO9918_F18A_BADGE_HEIGHT)
  {
    return false;
  }

  const uint8_t* row = f18aBadge + outputLine * (F18ABADGE_WIDTH / 8);

  /* Opaque, so unlike the splash above there is no index to treat as transparent. */
  for (uint32_t x = 0; x < PICO9918_F18A_BADGE_WIDTH; ++x)
  {
    pixels[x] = f18aBadge_pal[(row[x >> 3] >> (7 - (x & 7))) & 1];
  }

  return true;
}

#endif // PICO9918_BUILD_RUNTIME_CHIP
