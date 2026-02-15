/*
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 */

#include "display.h"

#include "splash.h"
#include "vga.h"

#if !PICO9918_NO_SPLASH
#include "bmp_splash.h"
#endif

static int logoOffset = 100;
static bool canHideSplash = false;

/*
 * reset the splash popup (after... reset)
 */
void resetSplash()
{
  logoOffset = 100;
}

void allowSplashHide()
{
  canHideSplash = true;
}

/*
 * output the PICO9918 splash logo / firmware version at the bottom of the screen
 */
void outputSplash(uint16_t y, uint32_t frameCount, uint32_t vBorder, uint32_t vPixels, uint16_t* pixels)
{
#if !PICO9918_NO_SPLASH

  if (y == 0)
  {
    if (frameCount & 0x01)
    {
      if (frameCount < 200 && logoOffset > (22 - splashHeight)) --logoOffset;
      else if (canHideSplash && frameCount > 500) ++logoOffset;
    }
  }

  if (y < (vgaCurrentParams()->params.vVirtualPixels - 1))
  {
    y -= vBorder + vPixels + logoOffset;
    if (y < splashHeight)
    {
      /* the source image is 2bpp, so 4 pixels in a byte
       * this doesn't need to be overly performant as it only
       * gets called in the first few seconds of startup (or reset)
       */
      const int leftBorderPx = 4;
      const int splashBpp = 2;
      const int splashPixPerByte = 8 / splashBpp;
      uint8_t* splashPtr = splash + (y * splashWidth / splashPixPerByte);

      for (int x = leftBorderPx; x < leftBorderPx + splashWidth; x += splashPixPerByte)
      {
        uint8_t c = *(splashPtr++);
        uint8_t pixMask = 0xc0;
        uint8_t offset = 6;

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
