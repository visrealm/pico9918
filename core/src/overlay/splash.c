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
  /* every parameter is unused when the overlay is off, and the library's
   * warnings-as-errors posture (MSVC /W4 /WX) rejects that */
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
    /* INTENTIONAL 16-bit wraparound, and it is the row gate - do not "fix" the
     * narrowing with a cast or a signed rewrite. logoOffset is signed and goes
     * negative, so for every row outside the logo band this subtraction wraps to
     * a large uint16 and the comparison below is false. Only the rows actually
     * in the band land in 0..splashHeight-1. Compilers may warn here (MSVC
     * C4244); the truncation is the mechanism, not an accident. */
    y -= vBorder + vPixels + logoOffset;
    if (y < splashHeight)
    {
      /* the source image is 2bpp, so 4 pixels in a byte
       * this doesn't need to be overly performant as it only
       * gets called in the first few seconds of startup (or reset)
       */
      const int leftBorderPx     = 4;
      const int splashBpp        = 2;
      const int splashPixPerByte = 8 / splashBpp;
      uint8_t* splashPtr         = splash + (y * splashWidth / splashPixPerByte);

      for (int x = leftBorderPx; x < leftBorderPx + splashWidth; x += splashPixPerByte)
      {
        uint8_t c       = *(splashPtr++);
        uint8_t pixMask = 0xc0;
        uint8_t offset  = 6;

        for (int px = 0; px < 4; ++px, offset -= 2, pixMask >>= 2)
        {
          uint8_t palIndex = (c & pixMask) >> offset;
          if (palIndex) pixels[x + px] = splash_pal[palIndex];
        }
      }
    }
  }
#endif
}
