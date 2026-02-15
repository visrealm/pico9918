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
#include "config.h"
#include "bmp_font.h"  
#include "gpu.h"

#include "impl/vrEmuTms9918Priv.h"

#include "pico/divider.h"

#include <stdbool.h>
#include <string.h>

#define CHAR_WIDTH 6
#define CHAR_HEIGHT 6


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
IntString gpuPctStr = {0};
IntString clockMhzStr = {0};
IntString modeStr = {0};
IntString fpsStr = {0};
IntString hwVerStr = {0};
IntString fwVerStr = {0};

IntString nameTabStr = {0};
IntString colorTabStr = {0};
IntString pattTabStr = {0};
IntString sprAttTabStr = {0};
IntString sprPattTabStr = {0};

uint32_t accumulatedRenderTime = 0;
uint32_t accumulatedFrameTime = 0; 
uint32_t accumulatedScanlines = 0;
uint32_t lastUpdateTime = 0;

const uint16_t labelColor = 0x0ff7;
const uint16_t valueColor = 0x0fff;
const uint16_t unitsColor = 0x0888;

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
static void uint2Str(uint32_t number, int width, IntString *out)
{
  out->start = sizeof(out->digits) - 1;
  out->digits[out->start] = '\0';
  while (number && out->start)
  {
    divmod_result_t dmResult = divmod_u32u32(number, 10);
    number = to_quotient_u32(dmResult);
    out->digits[--out->start] = '0' + to_remainder_u32(dmResult);
    --width;
  }
  
  while (width-- > 0)
  {
    out->digits[--out->start] = '0';
  }
}


/* convert an integer to hex string */
static void uint2hexStr(uint32_t number, int width, IntString *out)
{
  out->start = sizeof(out->digits) - 1;
  out->digits[out->start] = '\0';
  while (number && out->start)
  {
    divmod_result_t dmResult = divmod_u32u32(number, 16);
    number = to_quotient_u32(dmResult);
    if (to_remainder_u32(dmResult) < 10)
      out->digits[--out->start] = '0' + to_remainder_u32(dmResult);
    else
      out->digits[--out->start] = 'A' - 10 + to_remainder_u32(dmResult);
    --width;
  }
  
  while (width-- > 0)
  {
    out->digits[--out->start] = '0';
  }
}

void initDiagnostics()
{
  clear(&renderTimeStr);
  clear(&frameTimeStr);
  clear(&gpuPctStr);
  clear(&renderTimePerScanlineStr);
  clear(&totalTimePerScanlineStr);
  clear(&temperatureStr);
  clear(&gpuPctStr);
  clear(&modeStr);
  clear(&fpsStr);
  clear(&hwVerStr);
  clear(&fwVerStr);

  clear(&nameTabStr);
  clear(&colorTabStr);
  clear(&pattTabStr);
  clear(&sprAttTabStr);
  clear(&sprPattTabStr);

  Pico9918HardwareVersion hwVersion = currentHwVersion();
  strcpy(hwVerStr.digits, hwVersion == HWVer_0_3 ? "V0.3" : "V1.0+");

  strncpy(fwVerStr.digits, PICO9918_VERSION, sizeof(fwVerStr.digits) - 1);
  fwVerStr.digits[sizeof(fwVerStr.digits) - 1] = '\0';
  fwVerStr.start = 0;
}

const char *modeNames[] = {
  "GFX I",
  "GFX II",
  "TEXT",
  "MULTI", 
  "80 COL",
};

/* set the temperature value to display */
void diagSetTemperature(float tempC)
{
  flt2Str(tempC, 2, &temperatureStr);
}

void diagSetClockHz(float clockHz)
{
  flt2Str(clockHz / 1000000.0f, 1, &clockMhzStr);
}

extern int droppedFramesCount;
static uint32_t cachedFrameCount = 0;

/* update diagnostics values */
void updateDiagnostics(uint32_t frameCount)
{
  cachedFrameCount = frameCount;
  const uint32_t framesPerUpdate = 1 << 2;
  if (tms9918->config[CONF_DIAG_PERFORMANCE])
  {
    if ((frameCount & (framesPerUpdate - 1)) == 0)
    {
      flt2Str((float)(accumulatedRenderTime / framesPerUpdate) / 1000.0f, 3, &renderTimeStr);
      flt2Str((float)(accumulatedFrameTime / framesPerUpdate) / 1000.0f, 3, &frameTimeStr);
      uint2Str(accumulatedRenderTime / accumulatedScanlines, 1, &renderTimePerScanlineStr);
      uint2Str(accumulatedFrameTime / accumulatedScanlines, 1, &totalTimePerScanlineStr);

      accumulatedRenderTime = accumulatedFrameTime = accumulatedScanlines = 0;

      uint32_t currentTime = time_us_32();
      uint32_t totalTime = lastUpdateTime - currentTime;

      float gpuPct = (gpuTime(totalTime) / (float)(lastUpdateTime - currentTime)) * 100.0f;
      flt2Str(gpuPct, 4, &gpuPctStr);
      resetGpuTime();

      lastUpdateTime = currentTime;
    }

    if ((++frameCount & (framesPerUpdate - 1)) == 0)
    {
      flt2Str((16.0f - droppedFramesCount) * 3.75f, 2, &fpsStr);
    }
  }

  if (tms9918->config[CONF_DIAG_ADDRESS])
  {
    if ((++frameCount & (framesPerUpdate - 1)) == 0)
    {
      uint2hexStr((TMS_REGISTER(tms9918, TMS_REG_NAME_TABLE) & 0x0f) << 10, 4, &nameTabStr);

      uint8_t mask = (vrEmuTms9918DisplayMode() == TMS_MODE_GRAPHICS_II) ? 0x80 : 0xff;
      uint2hexStr((TMS_REGISTER(tms9918, TMS_REG_COLOR_TABLE) & mask) << 6, 4, &colorTabStr);

      mask = (vrEmuTms9918DisplayMode() == TMS_MODE_GRAPHICS_II) ? 0x04 : 0x07;
      uint2hexStr(((TMS_REGISTER(tms9918, TMS_REG_PATTERN_TABLE) & mask) << 11) & 0xffff, 4, &pattTabStr);

      uint2hexStr((TMS_REGISTER(tms9918, TMS_REG_SPRITE_ATTR_TABLE) & 0x7f) << 7, 4, &sprAttTabStr);
      uint2hexStr((TMS_REGISTER(tms9918, TMS_REG_SPRITE_PATT_TABLE) & 0x07) << 11, 4, &sprPattTabStr);

      const char *s = modeNames[vrEmuTms9918DisplayMode()];
      char *d = modeStr.digits;
      while (*s)
      {
        *d++ = *s++;
      }
      *d = 0;
    }
  }
}

static inline int darken(int x, uint16_t* pixels)
{
  pixels[x++] = (pixels[x] >> 2) & 0x333;
  return x;
}


/* render a debug text scanline */
int renderText(uint16_t scanline, const char *text, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg, uint16_t* pixels)
{
  int fontY = scanline - y;
  if (fontY < 0 || fontY >= CHAR_HEIGHT) return x;

  fontY <<= 1;
  char c = 0;
  x = darken(x, pixels);
  while (c = *text)
  {
    c -= 32;
    char b = font[(((c & 0x30) + fontY) << 3) + (c & 0xf)];
    for (int i = 0; i < CHAR_WIDTH; ++i)
    {
      if (b & 0x80)
        pixels[x++] = fg;
      else
      x = darken(x, pixels);
      b <<= 1;
    }

    ++text;
  }
  return x;
}


/* render a bcd value scanline */
inline int renderNum(uint16_t scanline, IntString *str, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg, uint16_t* pixels)
{
  return renderText(scanline, str->digits + str->start, x, y, fg, bg, pixels);
}


void updateRenderTime(uint32_t renderTime, uint32_t frameTime)
{
  ++accumulatedScanlines;
  accumulatedRenderTime += renderTime;
  accumulatedFrameTime += frameTime;
}


static int backgroundPixels(int xPos, int count, uint16_t* pixels)
{
  for (int i = 0; i < count; ++i)
  {
    xPos = darken(xPos, pixels);
  }
  return xPos;
}


static const char *nibbleBinStr[] = 
{
  "((((", "((()", "(()(", "(())",
  "()((", "()()", "())(", "()))",
  ")(((", ")(()", ")()(", ")())",
  "))((", "))()", ")))(", "))))",
};

// register numbers to render
static const int extReg[] = { 10, 11, 15, 19, 24, 25, 26, 27,
                              28, 29, 30, 31, 32, 33, 34, 35,
                              36, 37, 38, 48, 49, 50, 51, 54, 55,
                              56, 57, 58, 59, 63 };

const uint32_t leftXPos = 2;

static void renderLeft(const char *label, IntString *val, const char *units, uint16_t row, uint16_t* pixels)
{
  uint32_t xPos = leftXPos;
  xPos = renderText(row, label, xPos, 0, labelColor, 0, pixels);
  xPos = renderNum(row, val, xPos, 0, valueColor, 0, pixels);
  xPos = renderText(row, units, xPos, 0, unitsColor, 0, pixels);
  xPos = backgroundPixels(xPos, 102 - xPos, pixels);
}

static void diagHwVer(uint16_t row, uint16_t* pixels)
{
  renderLeft("HWVER : ", &hwVerStr, "", row, pixels);
}

static void diagFwVer(uint16_t row, uint16_t* pixels)
{
  renderLeft("FWVER : ", &fwVerStr, "", row, pixels);
}

static void diagRenderTime(uint16_t row, uint16_t* pixels)
{
  renderLeft("FRAME : ", &frameTimeStr, "&S", row, pixels);
}

static void diagGpuTime(uint16_t row, uint16_t* pixels)
{
  renderLeft("GPU   : ", &gpuPctStr, "%", row, pixels);
}

static void diagFPS(uint16_t row, uint16_t* pixels)
{
  renderLeft("FPS   : ", &fpsStr, "FPS", row, pixels);
}

static void diagTemp(uint16_t row, uint16_t* pixels)
{
  renderLeft("TEMP  : ", &temperatureStr, "^C", row, pixels);
}

static void diagClock(uint16_t row, uint16_t* pixels)
{
  renderLeft("CLOCK : ", &clockMhzStr, "MHZ", row, pixels);
}

static void diagNameTab(uint16_t row, uint16_t* pixels)
{
  renderLeft("NAME  : >", &nameTabStr, "", row, pixels);
}
 
static void diagColorTab(uint16_t row, uint16_t* pixels)
{
  renderLeft("COLOR : >", &colorTabStr, "", row, pixels);
}

static void diagPattTab(uint16_t row, uint16_t* pixels)
{
  renderLeft("PATT  : >", &pattTabStr, "", row, pixels);
}
 
static void diagSprAttrTab(uint16_t row, uint16_t* pixels)
{
  renderLeft("SP ATR: >", &sprAttTabStr, "", row, pixels);
}

static void diagSprPattTab(uint16_t row, uint16_t* pixels)
{
  renderLeft("SP PAT: >", &sprPattTabStr, "", row, pixels);
}

static void diagMode(uint16_t row, uint16_t* pixels)
{
  renderLeft("MODE  : ", &modeStr, "", row, pixels);
}

static void diagSprite(int spriteId, uint16_t row, uint16_t* pixels)
{
  spriteId = spriteId + ((cachedFrameCount >> 4) & 0x18);

  static int lastSpriteId = -1;
  static IntString strSpriteId = {0};
  static IntString strSpriteY = {0};
  static IntString strSpriteX = {0};
  static IntString strPattId = {0};
  static IntString strColor = {0};
  if (spriteId != lastSpriteId)
  {
    uint8_t* spriteAttr = tms9918->vram.bytes +
      ((TMS_REGISTER(tms9918, TMS_REG_SPRITE_ATTR_TABLE) & 0x7f) << 7) 
      + (spriteId << 2);
    
    lastSpriteId = spriteId;
    uint2Str(spriteId, 2, &strSpriteId);
    uint2hexStr(spriteAttr[0], 2, &strSpriteY);
    uint2hexStr(spriteAttr[1], 2, &strSpriteX);
    uint2hexStr(spriteAttr[2], 2, &strPattId);
    uint2hexStr(spriteAttr[3], 2, &strColor);
  }
  uint32_t xPos = leftXPos;
  xPos = renderText(row, "SP#", xPos, 0, labelColor, 0, pixels);
  xPos = renderNum(row, &strSpriteId, xPos, 0, valueColor, 0, pixels);
  xPos = renderText(row, " $", xPos, 0, labelColor, 0, pixels);
  xPos = renderNum(row, &strSpriteY, xPos, 0, valueColor, 0, pixels);
  xPos = renderText(row, " $", xPos, 0, labelColor, 0, pixels);
  xPos = renderNum(row, &strSpriteX, xPos, 0, valueColor, 0, pixels);
  xPos = renderText(row, " $", xPos, 0, labelColor, 0, pixels);
  xPos = renderNum(row, &strPattId, xPos, 0, valueColor, 0, pixels);
  xPos = renderText(row, " $", xPos, 0, labelColor, 0, pixels);
  xPos = renderNum(row, &strColor, xPos, 0, valueColor, 0, pixels);
//  xPos = backgroundPixels(xPos, 102 - xPos, pixels);
}

static void diagSprite0(uint16_t row, uint16_t* pixels)
{
  diagSprite(0, row, pixels);
}
static void diagSprite1(uint16_t row, uint16_t* pixels)
{
  diagSprite(1, row, pixels);
}
static void diagSprite2(uint16_t row, uint16_t* pixels)
{
  diagSprite(2, row, pixels);
}
static void diagSprite3(uint16_t row, uint16_t* pixels)
{
  diagSprite(3, row, pixels);
}
static void diagSprite4(uint16_t row, uint16_t* pixels)
{
  diagSprite(4, row, pixels);
}
static void diagSprite5(uint16_t row, uint16_t* pixels)
{
  diagSprite(5, row, pixels);
}
static void diagSprite6(uint16_t row, uint16_t* pixels)
{
  diagSprite(6, row, pixels);
}
static void diagSprite7(uint16_t row, uint16_t* pixels)
{
  diagSprite(7, row, pixels);
}



typedef void (*DiagPtr)(uint16_t, uint16_t*);

DiagPtr leftDiags[32] = {0};
int leftDiagRows = 0;

DiagPtr performanceDiags[] = {
  &diagHwVer,
  &diagFwVer,
  &diagClock,
  &diagRenderTime,
  &diagFPS,
  &diagGpuTime,
  &diagTemp};

DiagPtr addressDiags[] = {
  &diagMode,
  &diagNameTab,
  &diagColorTab,
  &diagPattTab,
  &diagSprAttrTab,
  &diagSprPattTab};

DiagPtr spriteDiags[] = {
  &diagSprite0,
  &diagSprite1,
  &diagSprite2,
  &diagSprite3,
  &diagSprite4,
  &diagSprite5,
  &diagSprite6,
  &diagSprite7};

#if 0

#define PROGDATA_FLASH_OFFSET (0x100000)    // Top 1MB of flash
#define PROGDATA_FLASH_ADDR   (uint8_t*)(XIP_BASE + PROGDATA_FLASH_OFFSET)

void flashAddressDiag(uint16_t row, uint16_t* pixels)
{
  IntString tempStr = {0};
  uint8_t *addr = PROGDATA_FLASH_ADDR;
  int xPos = leftXPos;

  for (int i = 0; i < 40; ++i)
  {
    uint2hexStr(addr[i], 2, &tempStr);
    xPos = renderNum(row, &tempStr, xPos, 0, valueColor, 0, pixels);
    if (i == 3 || i == 19 || i == 35)
      xPos = backgroundPixels(xPos, 4, pixels);
  }
}

void flashAddress2Diag(uint16_t row, uint16_t* pixels)
{
  IntString tempStr = {0};
  uint8_t *addr = PROGDATA_FLASH_ADDR + 0x100;
  int xPos = leftXPos;

  for (int i = 0; i < 40; ++i)
  {
    uint2hexStr(addr[i], 2, &tempStr);
    xPos = renderNum(row, &tempStr, xPos, 0, valueColor, 0, pixels);
    if (i == 3 || i == 19 || i == 35)
      xPos = backgroundPixels(xPos, 4, pixels);
  }
}

void vramAddressDiag(uint16_t row, uint16_t* pixels)
{
  IntString tempStr = {0};
  uint8_t *addr = tms9918->vram.bytes + 0x1f00;
  int xPos = leftXPos;

  for (int i = 0; i < 40; ++i)
  {
    uint2hexStr(addr[i], 2, &tempStr);
    xPos = renderNum(row, &tempStr, xPos, 0, valueColor, 0, pixels);
    if (i == 15 || i == 31 || i == 35)
      xPos = backgroundPixels(xPos, 4, pixels);
  }
}

#endif

void diagnosticsConfigUpdated()
{
  memset(leftDiags, 0, sizeof(leftDiags));

  leftDiagRows= 0;

#if 0  
  leftDiags[leftDiagRows++] = flashAddressDiag;
  leftDiags[leftDiagRows++] = flashAddress2Diag;
  leftDiags[leftDiagRows++] = vramAddressDiag;
#endif

  if (tms9918->config[CONF_DIAG_PERFORMANCE])
  {
    for (int j = 0; j < sizeof(performanceDiags) / sizeof(void*); ++j)
      leftDiags[leftDiagRows++] = performanceDiags[j];
    leftDiagRows++;
  }

  if (tms9918->config[CONF_DIAG_ADDRESS])
  {
    for (int j = 0; j < sizeof(addressDiags) / sizeof(void*); ++j)
      leftDiags[leftDiagRows++] = addressDiags[j];
    leftDiagRows++;
  }



  //for (int j = 0; j < sizeof(spriteDiags) / sizeof(void*); ++j)
  //  leftDiags[leftDiagRows++] = spriteDiags[j];
  //leftDiagRows++;
}

static void renderPalette(int y, uint16_t *pixels)
{
  divmod_result_t dmResult = divmod_u32u32(y, 6);
  int row = to_remainder_u32(dmResult);

  uint8_t palette = (y - 216) / 6;
  if (palette < 4)
  {
    char buf[] = "PALETTE 0:"; buf[8] = '0' + palette;
    renderText(row, buf, leftXPos, 0, labelColor, 0, pixels);
    uint32_t xPos = 32;
    uint32_t *pix32 = (uint32_t *)pixels;
    if (row < 5)
    {
      for (int c = 0 ; c < 16; ++c)
      {
        uint32_t color = tms9918->vram.map.pram[palette * 16 + c] & 0xFF0F;
        color |= (color & 0xf000) >> 8;
        color &= 0xfff;
        color |= color << 16;
        for (int i = 0; i < 15; ++i)
        {
          pix32[xPos++] = color;
        }
        xPos++;
      }
    }
  }
}


void renderDiagnostics(uint16_t y, uint16_t* pixels)
{
  y -= 1; // vertical border

  // palette
  if (tms9918->config[CONF_DIAG_PALETTE] && (y > 213)) renderPalette(y + 2, pixels);

  divmod_result_t dmResult = divmod_u32u32(y, 6);
  int diagRow = to_quotient_u32(dmResult);
  int row = to_remainder_u32(dmResult);

  int maxReg = 8;
  if (tms9918->isUnlocked)
  {
    maxReg += sizeof(extReg) / sizeof(int);
  }
  
  // left panels
  if (diagRow < leftDiagRows && leftDiags[diagRow] != NULL)
  {
    leftDiags[diagRow](row, pixels);
  }

  // registers
  if (tms9918->config[CONF_DIAG_REGISTERS] && (diagRow < maxReg))
  {
    if (diagRow >= 8)
    {
      diagRow = extReg[diagRow - 8];
    }

    dmResult = divmod_u32u32(diagRow, 10);
    int xPos = 636 - (CHAR_WIDTH * 13);
    char buf[] = "R00:"; buf[1] = '0' + to_quotient_u32(dmResult); buf[2] = '0' + to_remainder_u32(dmResult);
    xPos = renderText(row, buf, xPos, 0, labelColor, 0, pixels);
    xPos = backgroundPixels(xPos, 2, pixels);
    xPos = renderText(row, nibbleBinStr[TMS_REGISTER(tms9918, diagRow) >> 4], xPos, 0, valueColor, 0, pixels);
    xPos = backgroundPixels(xPos, 2, pixels);
    xPos = renderText(row, nibbleBinStr[TMS_REGISTER(tms9918, diagRow) & 0xf], xPos, 0, valueColor, 0, pixels);
  }

}

