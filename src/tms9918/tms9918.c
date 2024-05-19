/*
 * Project: pico-56 - tms9918
 *
 * Copyright (c) 2023 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico-56
 *
 */

#include "tms9918.h"
#include "vrEmuTms9918Util.h"

#include "vga.h"
#include "vga-modes.h"

#include "pico/stdlib.h"

#include "pico/divider.h"

static VrEmuTms9918* tms = NULL;
static vgaEndOfFrameFn eofCallback = NULL;
static vgaEndOfScanlineFn scanlineCallback = NULL;

static uint16_t __aligned(4) tmsPal[16];
static uint8_t __aligned(4) tmsScanlineBuffer[TMS9918_PIXELS_X];

/*
 * convert 48-bit rgb to 12-bit bgr
 */
static uint16_t colorFromRgb(uint16_t r, uint16_t g, uint16_t b)
{
  return
    ((uint16_t)(r / 16.0f) & 0x0f) |
    (((uint16_t)(g / 16.0f) & 0x0f) << 4) |
    (((uint16_t)(b / 16.0f) & 0x0f) << 8);
}

/*
 * vga scanline callback for tms9918
 */
static void tmsScanline(uint16_t y, VgaParams* params, uint16_t* pixels)
{
  const uint32_t vBorder = (params->vVirtualPixels - TMS9918_PIXELS_Y) / 2;
  const uint32_t hBorder = (params->hVirtualPixels - TMS9918_PIXELS_X) / 2;

  uint16_t bg = tmsPal[vrEmuTms9918RegValue(tms, TMS_REG_FG_BG_COLOR) & 0x0f];

  // top or bottom border
  if (y < vBorder || y >= (vBorder + TMS9918_PIXELS_Y))
  {
    for (int x = 0; x < params->hVirtualPixels; ++x)
    {
      pixels[x] = bg;
    }
    return;
  }

  y -= vBorder;

  // left border
  for (int x = 0; x < hBorder; ++x)
  {
    pixels[x] = bg;
  }

  // get scanline data from the tms9918
  vrEmuTms9918ScanLine(tms, y, tmsScanlineBuffer);

  // convert to our 12-bit palette and output to pixels array
  int tmsX = 0;
  for (int x = hBorder; x < hBorder + TMS9918_PIXELS_X; ++x, ++tmsX)
  {
    pixels[x] = tmsPal[tmsScanlineBuffer[tmsX]];
  }

  // right border
  for (int x = hBorder + TMS9918_PIXELS_X; x < params->hVirtualPixels; ++x)
  {
    pixels[x] = bg;
  }

  // interrupt?
  if (y == TMS9918_PIXELS_Y - 1)
  {
    if ((vrEmuTms9918RegValue(tms, TMS_REG_1) & 0x20))
    {
      //raiseInterrupt(1);
    }
  }
}

/*
 * set callback for end of frame events
 */
void tmsSetFrameCallback(vgaEndOfFrameFn cb)
{
  eofCallback = cb;
}

/*
 * set callback for end of scanline events
 */
void tmsSetHsyncCallback(vgaEndOfScanlineFn cb)
{
  scanlineCallback = cb;
}

/*
 * get vga horizontal frequency in Hz
 */
int tmsGetHsyncFreq()
{
  return vgaCurrentParams().params.hSyncParams.freqHz;
}

/*
 * vga end-of-frame callback for tms9918
 */
static void tmsEndOfFrame(uint64_t frameNumber)
{
  if (eofCallback) eofCallback(frameNumber);
}

/*
 * vga end-of-scanline callback for tms9918
 */
static void tmsEndOfScanline()
{
  if (scanlineCallback) scanlineCallback();
}

static const uint32_t MAX_CLOCK = 270000;

void setClosestClockFreqKhz(uint32_t clockKHz)
{
  if (clockKHz > MAX_CLOCK) clockKHz = MAX_CLOCK;

  clockKHz /= 10;
  clockKHz *= 10;

  uint32_t offset = 0;
  while (!set_sys_clock_khz(clockKHz + offset, false)) {
    offset += 10;
    if (set_sys_clock_khz(clockKHz - offset, false)) break;
  }
}

/*
 * tms9918 initialisation
 */
VrEmuTms9918* tmsInit()
{
  tms = vrEmuTms9918New();

  // build up tms9918 palette optimized for 12-bit vga
  for (int c = 0; c < 16; ++c)
  {
    uint32_t rgba8 = vrEmuTms9918Palette[c];
    tmsPal[c] = colorFromRgb(
      (rgba8 & 0xff000000) >> 24,
      (rgba8 & 0x00ff0000) >> 16,
      (rgba8 & 0x0000ff00) >> 8);
  }

  VgaInitParams params;
  params.params = vgaGetParams(VGA_800_600_60HZ, 3);
  params.scanlineFn = tmsScanline;
  params.endOfFrameFn = tmsEndOfFrame;
  params.endOfScanlineFn = tmsEndOfScanline;

  uint32_t minSysClockFreq = vgaMinimumPioClockKHz(&params.params);
  uint32_t sysClockFreq = minSysClockFreq;
  while (sysClockFreq + minSysClockFreq < MAX_CLOCK)
  {
    sysClockFreq += minSysClockFreq;
  }

  setClosestClockFreqKhz(sysClockFreq);

  vgaInit(params);

  return tms;
}

VrEmuTms9918* getTms9918()
{
  return tms;
}

/*
 * tms9918 destruction
 */
void tmsDestroy()
{
  if (tms)
  {
    vrEmuTms9918Destroy(tms);
    tms = NULL;
  }
}
