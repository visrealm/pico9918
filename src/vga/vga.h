/*
 * Project: pico9918 - vga
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico-56
 *
 */

#pragma once

#include <inttypes.h>
#include <stdbool.h>

typedef struct
{
  uint32_t displayPixels;
  uint32_t frontPorchPixels;
  uint32_t syncPixels;
  uint32_t backPorchPixels;
  uint32_t totalPixels;
  float freqHz;
  bool syncHigh;
} VgaSyncParams;

typedef struct
{
  uint32_t pixelClockKHz;
  VgaSyncParams hSyncParams;
  VgaSyncParams vSyncParams;
  uint32_t hPixelScale;
  uint32_t vPixelScale;
  uint32_t hVirtualPixels;
  uint32_t vVirtualPixels;
  float pioDivider;
  float pioFreqKHz;
  float pioClocksPerPixel;
  float pioClocksPerScaledPixel;
} VgaParams;


typedef void (*vgaScanlineRgbFn)(uint16_t y, VgaParams* params, uint16_t* pixels);
typedef void (*vgaEndOfFrameFn)(uint32_t frameNumber);
typedef void (*vgaInitFn)();
typedef void (*vgaEndOfScanlineFn)();

extern uint32_t vgaMinimumPioClockKHz(VgaParams* params);

typedef struct
{
  VgaParams params;
  vgaInitFn initFn;
  vgaScanlineRgbFn scanlineFn;
  vgaEndOfFrameFn endOfFrameFn;
  vgaEndOfScanlineFn endOfScanlineFn;
  bool scanlines;
} VgaInitParams;

void vgaLoop();

void vgaInit(VgaInitParams params);

VgaInitParams *vgaCurrentParams();
