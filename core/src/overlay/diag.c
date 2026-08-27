/**
 * \file
 * \brief pico9918-core - Diagnostics overlay
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 */

#include "diag.h"

#include "overlay/bmp_font.h"

#include "impl/pico9918_priv.h"

/* After pico9918_priv.h, which is where PICO9918_MODE_F18A comes from. */
#if PICO9918_MODE == PICO9918_MODE_F18A
#include "gpu/gpu.h"
#endif

#include <stdbool.h>
#include <string.h>


typedef struct
{
  union
  {
    uint32_t words[3];
    char digits[sizeof(uint32_t) * 3];
  };
  int start;
} IntString;

static void clear(IntString* number)
{
  number->start    = 0;
  number->words[0] = 0;
  number->words[1] = 0;
  number->words[2] = 0;
}


/* for diagnostics / statistics */
IntString frameTimeStr             = {0};
IntString renderTimePerScanlineStr = {0};
IntString temperatureStr           = {0};
#if PICO9918_MODE == PICO9918_MODE_F18A
IntString gpuPctStr                = {0};
#endif
IntString clockMhzStr              = {0};
IntString modeStr                  = {0};
IntString fpsStr                   = {0};
#if PICO9918_DIAG_GPU_FRAME_COUNTER
IntString gpuFrameStr = {0};
#endif
IntString hwVerStr  = {0};
IntString fwVerStr  = {0};
IntString outputStr = {0};

/* host-pushed OUTPUT row units ("@60" / "@50"); the driver encoding is host
   policy, so the label arrives rather than being derived here */
static const char* outputUnitsStr = "";

IntString nameTabStr    = {0};
IntString colorTabStr   = {0};
IntString pattTabStr    = {0};
IntString sprAttTabStr  = {0};
IntString sprPattTabStr = {0};

uint32_t accumulatedRenderTime = 0;
uint32_t accumulatedFrameTime  = 0;
uint32_t accumulatedScanlines  = 0;
uint32_t lastUpdateTime        = 0;

/*
 * Panel colours, expressed through the pixel policy rather than as raw host
 * words. The macro's input is a pram-order value (0xGB0R), so the canonical
 * colour is byte-swapped to feed it: pale cyan 0x07ff -> 0xff07, white 0x0fff ->
 * 0xff0f, grey 0x0888 -> 0x8808.
 *
 * The mask is load-bearing, not tidying: PICO9918_PIXEL_FROM_RGB12 replicates green
 * into bits 15-12, which are dead at the pin boundary but NOT on the RP2040
 * CRT-scanline path, where the whole word is shifted right by one UNMASKED and
 * bit 12 lands in blue's MSB. Without the mask the panel text renders
 * brighter-blue whenever CRT scanlines are enabled. Keep the top nibble clear.
 *
 * 0x0fff is a 12-bit mask on a >= 16-bit type, so this is well-defined at any
 * pixel width, but the *values* only mean BGR12 under the Pico policy - the
 * desktop RGBA8888 policy would need its own literals (no desktop consumer draws
 * these panels today).
 */
#define DIAG_COLOR(pramOrder) ((PICO9918_PIXEL_T)(PICO9918_PIXEL_FROM_RGB12(pramOrder) & 0x0fff))

const PICO9918_PIXEL_T labelColor = DIAG_COLOR(0xff07);
const PICO9918_PIXEL_T valueColor = DIAG_COLOR(0xff0f);
const PICO9918_PIXEL_T unitsColor = DIAG_COLOR(0x8808);

static float precLookup[] = {1.0f, 10.0f, 100.0f, 1000.0f};

/* convert a float to a string */
static void flt2Str(float flt, int prec, IntString* out)
{
  if (prec > 3) prec = 3;
  flt *= precLookup[prec];
  uint32_t number = (uint32_t)(flt + 0.5f);

  out->start              = sizeof(out->digits) - 1;
  out->digits[out->start] = '\0';
  while (prec--)
  {
    out->digits[--out->start] = '0' + (number % 10);
    number /= 10;
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
      out->digits[--out->start] = '0' + (number % 10);
      number /= 10;
    }
  }
}

#if PICO9918_DIAG_GPU_FRAME_COUNTER
/* convert an integer to string */
static void uint2Str(uint32_t number, int width, IntString* out)
{
  out->start              = sizeof(out->digits) - 1;
  out->digits[out->start] = '\0';
  while (number && out->start)
  {
    out->digits[--out->start] = '0' + (number % 10);
    number /= 10;
    --width;
  }

  while (width-- > 0)
  {
    out->digits[--out->start] = '0';
  }
}
#endif


/* convert an integer to hex string */
static void uint2hexStr(uint32_t number, int width, IntString* out)
{
  out->start              = sizeof(out->digits) - 1;
  out->digits[out->start] = '\0';
  while (number && out->start)
  {
    uint32_t nibble = number % 16;
    number /= 16;
    if (nibble < 10)
      out->digits[--out->start] = '0' + nibble;
    else
      out->digits[--out->start] = 'A' - 10 + nibble;
    --width;
  }

  while (width-- > 0)
  {
    out->digits[--out->start] = '0';
  }
}

/* glyphs per row of the font image, and the first character it holds */
#define FONT_CHARS 96
#define FONT_FIRST 32

/* A cell is PICO9918_DIAG_CHAR_WIDTH pixels, so this many ink words wide; the table's
   stride is rounded up to a power of two so indexing it is a shift, not a multiply. */
#define INK_WORDS  (PICO9918_DIAG_CHAR_WIDTH / PICO9918_INK_PIXELS)
#define INK_STRIDE (INK_WORDS <= 4 ? 4 : 8)

/* which pixels of which ink word a glyph's ink lands in, indexed by pattern byte,
   taken from the byte's bits 5..0 left to right */
static PICO9918_INK_T glyphMask[64][INK_STRIDE];

static void PICO9918_IN_FLASH_FUNC(glyphMaskInit)(void)
{
  for (uint32_t b = 0; b < 64; ++b)
  {
    for (uint32_t w = 0; w < INK_WORDS; ++w)
    {
      PICO9918_INK_T m = 0;
      for (uint32_t k = 0; k < PICO9918_INK_PIXELS; ++k)
      {
        if (b & (0x20u >> (w * PICO9918_INK_PIXELS + k))) m |= PICO9918_INK_ONE(k);
      }
      glyphMask[b][w] = m;
    }
  }
}

void PICO9918_IN_FLASH_FUNC(pico9918_diag_init)(void)
{
  glyphMaskInit();

  clear(&frameTimeStr);
#if PICO9918_MODE == PICO9918_MODE_F18A
  clear(&gpuPctStr);
#endif
#if PICO9918_DIAG_GPU_FRAME_COUNTER
  clear(&gpuFrameStr);
#endif
  clear(&renderTimePerScanlineStr);
  clear(&temperatureStr);
  clear(&clockMhzStr);
  clear(&modeStr);
  clear(&fpsStr);
  clear(&hwVerStr);
  clear(&fwVerStr);

  clear(&nameTabStr);
  clear(&colorTabStr);
  clear(&pattTabStr);
  clear(&sprAttTabStr);
  clear(&sprPattTabStr);
  clear(&outputStr);

  /* NOT reset here, deliberately, and it is worth saying why because it looks
     like an omission: the four timing accumulators (accumulatedRenderTime,
     accumulatedFrameTime, accumulatedScanlines, lastUpdateTime).

     They are zero-initialised statics and this function runs exactly once per
     boot, before any scanline has accumulated, so clearing them would be a
     provable no-op. A caller that re-primes the module mid-run and needs a
     known accumulator state must zero them itself; the golden harness does
     exactly that (see overlayPrimeDiag). */
}

void pico9918_diag_set_version_info(const char* hwVersion, const char* fwVersion)
{
  if (hwVersion)
  {
    strncpy(hwVerStr.digits, hwVersion, sizeof(hwVerStr.digits) - 1);
    hwVerStr.digits[sizeof(hwVerStr.digits) - 1] = '\0';
    hwVerStr.start                               = 0;
  }

  if (fwVersion)
  {
    strncpy(fwVerStr.digits, fwVersion, sizeof(fwVerStr.digits) - 1);
    fwVerStr.digits[sizeof(fwVerStr.digits) - 1] = '\0';
    fwVerStr.start                               = 0;
  }
}

void pico9918_diag_set_output_name(const char* name, const char* units)
{
  if (name)
  {
    strncpy(outputStr.digits, name, sizeof(outputStr.digits) - 1);
    outputStr.digits[sizeof(outputStr.digits) - 1] = '\0';
    outputStr.start                                = 0;
  }

  if (units) outputUnitsStr = units;
}

const char* modeNames[] = {
  "GFX I", "GFX II", "TEXT", "MULTI", "80 COL",
};

/* set the temperature value to display */
void pico9918_diag_set_temperature(float tempC)
{
  flt2Str(tempC, 2, &temperatureStr);
}

void pico9918_diag_set_clock_hz(float clockHz)
{
  flt2Str(clockHz / 1000000.0f, 1, &clockMhzStr);
}

/* host-pushed timing input. The dropped-frame count needs no push: the frame module
   owns that counter, so the FPS row reads it directly below. */
static float hostFrameRateHz = 0.0f;

void pico9918_diag_set_frame_rate(float frameRateHz)
{
  hostFrameRateHz = frameRateHz;
}

/* update diagnostics values */
void pico9918_diag_update(PICO9918_INST_ARG uint32_t frameCount)
{
  const uint32_t framesPerUpdate = 1 << 2;
  /* read off the count, so a panel's phase cannot depend on which others are enabled */
  const uint32_t phase = frameCount & (framesPerUpdate - 1);
  if (tms9918->config[PICO9918_CONF_DIAG_PERFORMANCE])
  {
    if (phase == 0)
    {
      flt2Str((float)(accumulatedFrameTime / framesPerUpdate) / 1000.0f, 3, &frameTimeStr);
      /* samples are whole microseconds, but their average resolves far finer */
      if (accumulatedScanlines)
      {
        flt2Str((float)accumulatedRenderTime / accumulatedScanlines, 2, &renderTimePerScanlineStr);
      }

      accumulatedRenderTime = accumulatedFrameTime = accumulatedScanlines = 0;

      uint32_t currentTime = PICO9918_HOST_TIME_US();

#if PICO9918_MODE == PICO9918_MODE_F18A
      /* Elapsed since the last update. The clock rises, so currentTime MUST be the
         later reading - reversed, this underflows and the row reads ~0% or exactly
         100%, never a real figure. Computed once and reused so that a fix cannot
         correct one copy of the expression and miss the other. */
      uint32_t totalTime = currentTime - lastUpdateTime;

      float gpuPct = (pico9918_gpu_time(totalTime) / (float)totalTime) * 100.0f;
      flt2Str(gpuPct, 4, &gpuPctStr);
      pico9918_gpu_reset_time();
#endif

#if PICO9918_DIAG_GPU_FRAME_COUNTER
      uint2Str(pico9918_gpu_frame_count, 1, &gpuFrameStr);
#endif

      lastUpdateTime = currentTime;
    }

    if (phase == 3)
    {
      flt2Str((16.0f - pico9918_dropped_frames_count) * (hostFrameRateHz / 16.0f), 2, &fpsStr);
    }
  }

  if (tms9918->config[PICO9918_CONF_DIAG_ADDRESS])
  {
    if (phase == 2)
    {
      uint2hexStr((TMS_REGISTER(tms9918, TMS_REG_NAME_TABLE) & 0x0f) << 10, 4, &nameTabStr);

      uint8_t mask = (pico9918_display_mode(PICO9918_INST_ONLY) == TMS_MODE_GRAPHICS_II) ? 0x80 : 0xff;
      uint2hexStr((TMS_REGISTER(tms9918, TMS_REG_COLOR_TABLE) & mask) << 6, 4, &colorTabStr);

      mask = (pico9918_display_mode(PICO9918_INST_ONLY) == TMS_MODE_GRAPHICS_II) ? 0x04 : 0x07;
      uint2hexStr(((TMS_REGISTER(tms9918, TMS_REG_PATTERN_TABLE) & mask) << 11) & 0xffff, 4, &pattTabStr);

      uint2hexStr((TMS_REGISTER(tms9918, TMS_REG_SPRITE_ATTR_TABLE) & 0x7f) << 7, 4, &sprAttTabStr);
      uint2hexStr((TMS_REGISTER(tms9918, TMS_REG_SPRITE_PATT_TABLE) & 0x07) << 11, 4, &sprPattTabStr);

      const char* s = modeNames[pico9918_display_mode(PICO9918_INST_ONLY)];
      char* d       = modeStr.digits;
      while (*s)
      {
        *d++ = *s++;
      }
      *d = 0;
    }
  }
}

int pico9918_diag_render_text(uint16_t scanline, const char* text, uint16_t x, uint16_t y, PICO9918_PIXEL_T fg,
                           PICO9918_PIXEL_T* pixels)
{
  const int fontY = scanline - y;
  if (fontY < 0 || fontY >= PICO9918_DIAG_CHAR_HEIGHT) return x;

  /* biased by the first character the image holds, so the loop indexes it directly */
  const uint8_t* __restrict fontRow = font + fontY * FONT_CHARS - FONT_FIRST;
  const uint8_t* __restrict s       = (const uint8_t*)text;
  const PICO9918_INK_T ink            = PICO9918_INK_FILL(fg);
  PICO9918_INK_T* p                   = (PICO9918_INK_T*)(pixels + x);
  uint32_t c;
  while ((c = *s++) != 0)
  {
    /* the background comes from the one mask: half the table and one load fewer */
    const PICO9918_INK_T* __restrict m = glyphMask[fontRow[c]];
    for (int w = 0; w < INK_WORDS; ++w)
    {
      p[w] = (ink & m[w]) | (PICO9918_INK_DARKEN(p[w]) & ~m[w]);
    }
    p += INK_WORDS;
  }
  return (PICO9918_PIXEL_T*)p - pixels;
}


/* one row of a panel string, whose origin is the top of the screen */
PICO9918_INLINE int renderRow(uint16_t row, const char* text, uint16_t x, PICO9918_PIXEL_T fg,
                            PICO9918_PIXEL_T* pixels)
{
  return pico9918_diag_render_text(row, text, x, 0, fg, pixels);
}


/* render a bcd value scanline
 *
 * PICO9918_INLINE, not a bare `inline`: there is no external definition of this
 * anywhere, so a plain C99 inline makes a desktop -O0 build emit calls to a symbol
 * that does not exist. TU-local - nothing outside this file names it. */
PICO9918_INLINE int renderNum(uint16_t row, IntString* str, uint16_t x, PICO9918_PIXEL_T fg, PICO9918_PIXEL_T* pixels)
{
  return renderRow(row, str->digits + str->start, x, fg, pixels);
}


void pico9918_diag_update_render_time(uint32_t renderTime, uint32_t frameTime)
{
  ++accumulatedScanlines;
  accumulatedRenderTime += renderTime;
  accumulatedFrameTime += frameTime;
}


/* darken a run with no glyph over it, whose origin and count are both whole ink words */
static int backgroundPixels(int xPos, int count, PICO9918_PIXEL_T* pixels)
{
  PICO9918_INK_T* p = (PICO9918_INK_T*)(pixels + xPos);
  for (int i = count / PICO9918_INK_PIXELS; i > 0; --i)
  {
    *p = PICO9918_INK_DARKEN(*p);
    ++p;
  }
  return xPos + count;
}


/* a nibble as four glyphs, fixed stride so the index is a shift not a load */
static const char nibbleBinStr[16][8] = {
  "((((", "((()", "(()(", "(())", "()((", "()()", "())(", "()))",
  ")(((", ")(()", ")()(", ")())", "))((", "))()", ")))(", "))))",
};

// register numbers to render
static const uint8_t extReg[] = {10, 11, 15, 19, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
                                 35, 36, 37, 38, 48, 49, 50, 51, 54, 55, 56, 57, 58, 59, 63};

const uint32_t leftXPos = 2;

static void renderLeft(const char* label, IntString* val, const char* units, uint16_t row,
                       PICO9918_PIXEL_T* pixels)
{
  uint32_t xPos = leftXPos;
  xPos          = renderRow(row, label, xPos, labelColor, pixels);
  xPos          = renderNum(row, val, xPos, valueColor, pixels);
  xPos          = renderRow(row, units, xPos, unitsColor, pixels);
  xPos          = backgroundPixels(xPos, 102 - xPos, pixels);
}

static void diagHwVer(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("HWVER : ", &hwVerStr, "", row, pixels);
}

static void diagFwVer(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("FWVER : ", &fwVerStr, "", row, pixels);
}

static void diagRenderTime(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("FRAME : ", &frameTimeStr, "&S", row, pixels);
}

static void diagScanlineRenderTime(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("RENDER: ", &renderTimePerScanlineStr, "US", row, pixels);
}

#if PICO9918_MODE == PICO9918_MODE_F18A
static void diagGpuTime(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("GPU   : ", &gpuPctStr, "%", row, pixels);
}
#endif

#if PICO9918_DIAG_GPU_FRAME_COUNTER
static void diagGpuFrames(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("GPU FR: ", &gpuFrameStr, "", row, pixels);
}
#endif

static void diagFPS(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("FPS   : ", &fpsStr, "FPS", row, pixels);
}

static void diagTemp(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("TEMP  : ", &temperatureStr, "^C", row, pixels);
}

static void diagOutput(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("OUTPUT: ", &outputStr, outputUnitsStr, row, pixels);
}

static void diagClock(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("CLOCK : ", &clockMhzStr, "MHZ", row, pixels);
}

static void diagNameTab(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("NAME  : >", &nameTabStr, "", row, pixels);
}

static void diagColorTab(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("COLOR : >", &colorTabStr, "", row, pixels);
}

static void diagPattTab(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("PATT  : >", &pattTabStr, "", row, pixels);
}

static void diagSprAttrTab(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("SP ATR: >", &sprAttTabStr, "", row, pixels);
}

static void diagSprPattTab(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("SP PAT: >", &sprPattTabStr, "", row, pixels);
}

static void diagMode(uint16_t row, PICO9918_PIXEL_T* pixels)
{
  renderLeft("MODE  : ", &modeStr, "", row, pixels);
}

typedef void (*DiagPtr)(uint16_t, PICO9918_PIXEL_T*);

static DiagPtr const performanceDiags[] = {&diagHwVer,
                                           &diagFwVer,
                                           &diagClock,
                                           &diagOutput,
                                           &diagRenderTime,
                                           &diagScanlineRenderTime,
                                           &diagFPS,
#if PICO9918_MODE == PICO9918_MODE_F18A
                                           &diagGpuTime,
#endif
#if PICO9918_DIAG_GPU_FRAME_COUNTER
                                           &diagGpuFrames,
#endif
                                           &diagTemp};

static DiagPtr const addressDiags[] = {&diagMode,    &diagNameTab,    &diagColorTab,
                                       &diagPattTab, &diagSprAttrTab, &diagSprPattTab};

/* every row either group can claim, plus the blank one each leaves after it */
static DiagPtr leftDiags[sizeof(performanceDiags) / sizeof(performanceDiags[0]) +
                         sizeof(addressDiags) / sizeof(addressDiags[0]) + 2] = {0};
static int leftDiagRows                                                      = 0;

void pico9918_diag_config_updated(PICO9918_INST_ONLY_ARG)
{
  memset(leftDiags, 0, sizeof(leftDiags));

  leftDiagRows = 0;

  if (tms9918->config[PICO9918_CONF_DIAG_PERFORMANCE])
  {
    for (int j = 0; j < sizeof(performanceDiags) / sizeof(void*); ++j) leftDiags[leftDiagRows++] = performanceDiags[j];
    leftDiagRows++;
  }

  if (tms9918->config[PICO9918_CONF_DIAG_ADDRESS])
  {
    for (int j = 0; j < sizeof(addressDiags) / sizeof(void*); ++j) leftDiags[leftDiagRows++] = addressDiags[j];
    leftDiagRows++;
  }
}

static void renderPalette(PICO9918_INST_ARG int y, uint32_t vVirtualPixels, PICO9918_PIXEL_T* pixels)
{
  int row = y % 6;

  uint8_t palette = (y - (vVirtualPixels - 24)) / 6;
  if (palette < 4)
  {
    char buf[] = "PALETTE 0:";
    buf[8]     = '0' + palette;
    renderRow(row, buf, leftXPos, labelColor, pixels);
    uint32_t xPos = 32;
    if (row < 5)
    {
      for (int c = 0; c < 16; ++c)
      {
        /* Kept as the explicit transform rather than reading pico9918_palette_lut,
           which holds the same low 12 bits but with green replicated into bits
           15-12 by PICO9918_PIXEL_FROM_RGB12. Proven over all 65536 pram inputs:
           the low 12 bits always agree, the top nibble differs whenever green is
           non-zero. That nibble is dead at the pins, but it is LIVE on the
           RP2040 CRT-dim path - vga.c shifts the whole 32-bit pair right by one
           UNMASKED, so bit 12 lands in bit 11, blue's MSB, in both halves. The
           trailing `& 0xfff` here is what keeps it clear; using the LUT would
           reintroduce the bleed on every dimmed swatch row. */
        uint32_t color = tms9918->vram.map.pram[palette * 16 + c] & 0xFF0F;
        color |= (color & 0xf000) >> 8;
        color &= 0xfff;
#if PICO9918_BUILD_PIXEL_SIZE == 2
        /* Shipping path, preserved verbatim: two 16-bit pixels per 32-bit store,
         * so 15 stores cover the 30-pixel swatch. */
        color |= color << 16;
        uint32_t* pix32 = (uint32_t*)pixels;
        for (int i = 0; i < 15; ++i)
        {
          pix32[xPos++] = color;
        }
        xPos++;
#else
        /* Wider pixel: the pair trick does not apply (a 32-bit store would cover
         * one pixel, not two, and the strip would come out half width). Same
         * 30 pixels, written singly. xPos stays in PAIR units so the gap
         * arithmetic below is unchanged.
         *
         * GEOMETRY only. `color` is still a BGR12 word, so under a 32-bit RGBA
         * policy these swatches render near-black with a junk alpha - the
         * transform above has no wide-pixel form, for the same reason
         * platform/desktop's PICO9918_PIXEL_FROM_RGB12 is marked BROKEN. This branch
         * exists so the loop is not silently half-width and the assumption is
         * visible; it is not a claim that the panel is correct off-target. No
         * desktop consumer draws it today. */
        for (int i = 0; i < 30; ++i)
        {
          pixels[xPos * 2 + i] = (PICO9918_PIXEL_T)color;
        }
        xPos += 16;
#endif
      }
    }
  }
}


void pico9918_diag_render(PICO9918_INST_ARG uint16_t y, uint32_t vVirtualPixels, PICO9918_PIXEL_T* pixels)
{
  /* line 0 has no row above it, and the subtraction below would wrap it to 65535 */
  if (y == 0) return;
  y -= 1; // vertical border

  // palette
  if (tms9918->config[PICO9918_CONF_DIAG_PALETTE] && (y > ((int)vVirtualPixels - 27)))
    renderPalette(PICO9918_INST y + 2, vVirtualPixels, pixels);

  /* One divide, remainder by multiply-subtract. GCC does NOT merge a `/` and `%`
   * pair into a single __aeabi_uidivmod on Cortex-M0+ (verified on 15.2: it emits
   * uidiv AND uidivmod), so the plain pair would cost two calls into the hardware
   * divider where the divmod_u32u32 this replaced cost one. This form keeps it at
   * one and stays portable - no SDK helper, no inline asm. Exhaustively identical
   * to y % 6 over the whole uint16 range. */
  const unsigned diagRow6 = (unsigned)y / 6u;
  int diagRow             = (int)diagRow6;
  int row                 = (int)((unsigned)y - diagRow6 * 6u);

  int maxReg = 8;
  if (PICO9918_UNLOCKED(tms9918))
  {
    maxReg += sizeof(extReg) / sizeof(extReg[0]);
  }

  // left panels
  if (diagRow < leftDiagRows && leftDiags[diagRow] != NULL)
  {
    leftDiags[diagRow](row, pixels);
  }

  // registers
  if (tms9918->config[PICO9918_CONF_DIAG_REGISTERS] && (diagRow < maxReg))
  {
    if (diagRow >= 8)
    {
      diagRow = extReg[diagRow - 8];
    }

    int xPos = 636 - (PICO9918_DIAG_CHAR_WIDTH * 13);
    /* unsigned, and one divide (see the /6 note above). diagRow is non-negative
     * here - it indexes the register tables - so the unsigned form is exact, and
     * it avoids the signed __aeabi_idiv the int form would emit. */
    const unsigned regTens = (unsigned)diagRow / 10u;
    char buf[]             = "R00:";
    buf[1]                 = '0' + regTens;
    buf[2]                 = '0' + ((unsigned)diagRow - regTens * 10u);
    xPos                   = renderRow(row, buf, xPos, labelColor, pixels);
    xPos                   = backgroundPixels(xPos, 2, pixels);
    xPos = renderRow(row, nibbleBinStr[TMS_REGISTER(tms9918, diagRow) >> 4], xPos, valueColor, pixels);
    xPos = backgroundPixels(xPos, 2, pixels);
    xPos = renderRow(row, nibbleBinStr[TMS_REGISTER(tms9918, diagRow) & 0xf], xPos, valueColor, pixels);
  }
}
