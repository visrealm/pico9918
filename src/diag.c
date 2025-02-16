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

#include "diag.h"

#include "display.h"

#include "impl/vrEmuTms9918Priv.h"

#include "pico/divider.h"

#if PICO9918_DIAG
  #include "bmp_font.h"  
#endif

#include <stdbool.h>


#define TIMING_DIAG PICO9918_DIAG
#define REG_DIAG PICO9918_DIAG

typedef struct 
{
  union {
    uint32_t words[3];
    char digits[sizeof(uint32_t) * 3];
  };
  int start;
} IntString;

static void clear(IntString *number)
{
  number->start = 0;
  number->words[0] = 0;
  number->words[1] = 0;
  number->words[2] = 0;
}


/* for diagnostics / statistics */
IntString renderTimeStr = {0};
IntString frameTimeStr = {0};
IntString renderTimePerScanlineStr = {0};
IntString totalTimePerScanlineStr = {0};
IntString temperatureStr = {0};

uint32_t accumulatedRenderTime = 0;
uint32_t accumulatedFrameTime = 0;
uint32_t accumulatedScanlines = 0;

static float precLookup[] = {1.0f, 10.0f, 100.0f, 1000.0f};

/* convert a float to a string */
static void flt2Str(float flt, int prec, IntString *out)
{
  if (prec > 3) prec = 3;
  flt *= precLookup[prec];
  uint32_t number = (uint32_t)(flt + 0.5f);

  out->start = sizeof(out->digits) - 1;
  out->digits[out->start] = '\0';
  while (prec--)
  {
    divmod_result_t dmResult = divmod_u32u32(number, 10);
    number = to_quotient_u32(dmResult);
    out->digits[--out->start] = '0' + to_remainder_u32(dmResult);
  }
  out->digits[--out->start] = '.';
  if (!number)
  {
    out->digits[--out->start] = '0';
  }
  else
  {
    while (number && out->start)
    {
      divmod_result_t dmResult = divmod_u32u32(number, 10);
      number = to_quotient_u32(dmResult);
      out->digits[--out->start] = '0' + to_remainder_u32(dmResult);
    }  
  }
}

/* convert an integer to string */
static void uint2Str(uint32_t number, IntString *out)
{
  out->start = sizeof(out->digits) - 1;
  out->digits[out->start] = '\0';
  if (!number)
  {
    out->digits[--out->start] = '0';
  }
  else
  {
    while (number && out->start)
    {
      divmod_result_t dmResult = divmod_u32u32(number, 10);
      number = to_quotient_u32(dmResult);
      out->digits[--out->start] = '0' + to_remainder_u32(dmResult);
    }  
  }
}

void initDiagnostics()
{
  clear(&renderTimeStr);
  clear(&frameTimeStr);
  clear(&renderTimePerScanlineStr);
  clear(&totalTimePerScanlineStr);
  clear(&temperatureStr);
}

/* set the temperature value to display */
void diagSetTemperature(float tempC)
{
  flt2Str(tempC, 2, &temperatureStr);
}

/* update diagnostics values */
void updateDiagnostics(uint32_t frameCount)
{
#if TIMING_DIAG
  const uint32_t framesPerUpdate = 1 << 2;
  if ((frameCount & (framesPerUpdate - 1)) == 0)
  {
    uint2Str(accumulatedRenderTime / framesPerUpdate, &renderTimeStr);
    uint2Str(accumulatedFrameTime / framesPerUpdate, &frameTimeStr);
    uint2Str(accumulatedRenderTime / accumulatedScanlines, &renderTimePerScanlineStr);
    uint2Str(accumulatedFrameTime / accumulatedScanlines, &totalTimePerScanlineStr);

    accumulatedRenderTime = accumulatedFrameTime = accumulatedScanlines = 0;
  }
#endif
}


#if PICO9918_DIAG

#define CHAR_HEIGHT 8
#define CHAR_WIDTH  6

/* render a debug text scanline */
void renderText(uint16_t scanline, const char *text, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg, uint16_t* pixels)
{
  int fontY = scanline - y;
  if (fontY < 0 || fontY >= CHAR_HEIGHT) return;

  fontY <<= 1;
  char c = 0;
  pixels[x++] = bg;
  while (c = *text)
  {
    c -= 32;
    char b = font[(((c & 0x30) + fontY) << 3) + (c & 0xf)];
    for (int i = 0; i < 6; ++i)
    {
      pixels[x++] = (b & 0x80) ? fg : bg;
      b <<= 1;
    }

    ++text;
  }
}


/* render a bcd value scanline */
inline void renderStr(uint16_t scanline, IntString *str, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg, uint16_t* pixels)
{
  renderText(scanline, str->digits + str->start, x, y, fg, bg, pixels);
}


void updateRenderTime(uint32_t renderTime, uint32_t frameTime)
{
  ++accumulatedScanlines;
  accumulatedRenderTime += renderTime;
  accumulatedFrameTime += frameTime;
}

void renderDiagnostics(uint16_t y, uint16_t* pixels)
{
#if TIMING_DIAG   
  // Output average render time in microseconds (just vrEmuTms9918ScanLine())
  // We only have ~16K microseconds to do everything for an entire frame.
  // blanking takes around 10% of that, leaving ~14K microseconds
  // ROW30 mode will inflate this figure since... more scanlines 
  int xPos = 2;
  int yPos = 2;
  renderStr(y, &frameTimeStr, xPos, yPos, 0x0f40, 0, pixels);
  renderStr(y, &totalTimePerScanlineStr, xPos + 36, yPos, 0x004f, 0, pixels); yPos += 8;

  renderStr(y, &renderTimeStr, xPos, yPos, 0x0f40, 0, pixels);
  renderStr(y, &renderTimePerScanlineStr, xPos + 36, yPos, 0x004f, 0, pixels); yPos += 8;

  renderStr(y, &temperatureStr, xPos, yPos, 0x004f, 0, pixels); yPos += 8;
  
  renderText(y, "HELLO", xPos, yPos, 0x0fff, 0, pixels);
#endif

#if REG_DIAG

  /* Register diagnostics. with this enabled, we overlay a binary 
   * representation of the VDP registers to the right-side of the screen */
  const int diagBitWidth = 4;
  const int diagBitSpacing = 2;
  const int diagRightMargin = 6;

  const int16_t diagBitOn = 0x2e2;
  const int16_t diagBitOff = 0x222;

  uint8_t reg = y / 3;
  if (reg < 64)
  {
    if (y % 3 != 0)
    {
      int8_t regValue = TMS_REGISTER(tms9918, reg);
      int x = VIRTUAL_PIXELS_X - ((8 * (diagBitWidth + diagBitSpacing)) + diagRightMargin);
      for (int i = 0; i < 8; ++i)
      {
        const bool on = regValue < 0;
        for (int xi = 0; xi < diagBitWidth; ++xi)
        {
          pixels[x++] = on ? diagBitOn : diagBitOff;
        }
        x += diagBitSpacing + (i == 3) * diagBitSpacing;
        regValue <<= 1;
      }
    }
    else if ((reg & 0x07) == 0)
    {
      int x = VIRTUAL_PIXELS_X - diagRightMargin + 1;

      for (int xi = 0; xi < diagBitWidth; ++xi)
      {
        pixels[x++] ^= pixels[x];
      }
    }
  }

#endif  
}

#endif

