/*
 * Project: pico-56 - vga
 *
 * Copyright (c) 2023 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico-56
 *
 */

#include "vga-modes.h"

void vgaUpdateTotalPixels(VgaSyncParams* params);

/*
 * populate VgaParams for known vga modes
 */
VgaParams vgaGetParams(VgaMode mode, int pixelScale)
{
  if (pixelScale < 1) pixelScale = 1;

  VgaParams params;

  switch (mode)
  {
    case VGA_640_480_60HZ:  // http://tinyvga.com/vga-timing/640x480@60Hz
      params.pixelClockKHz = 25175;
      params.hSyncParams.displayPixels = 640;
      params.hSyncParams.frontPorchPixels = 16;
      params.hSyncParams.syncPixels = 96;
      params.hSyncParams.backPorchPixels = 48;
      params.hSyncParams.syncHigh = false;

      params.vSyncParams.displayPixels = 480;
      params.vSyncParams.frontPorchPixels = 10;
      params.vSyncParams.syncPixels = 2;
      params.vSyncParams.backPorchPixels = 33;
      params.vSyncParams.syncHigh = false;
      break;

    case VGA_640_400_70HZ:  // http://tinyvga.com/vga-timing/640x400@70Hz
      params.pixelClockKHz = 25175;
      params.hSyncParams.displayPixels = 640;
      params.hSyncParams.frontPorchPixels = 16;
      params.hSyncParams.syncPixels = 96;
      params.hSyncParams.backPorchPixels = 48;
      params.hSyncParams.syncHigh = false;

      params.vSyncParams.displayPixels = 400;
      params.vSyncParams.frontPorchPixels = 12;
      params.vSyncParams.syncPixels = 2;
      params.vSyncParams.backPorchPixels = 35;
      params.vSyncParams.syncHigh = true;
      break;

    case VGA_800_600_60HZ:  // http://tinyvga.com/vga-timing/800x600@60Hz
      params.pixelClockKHz = 40000;
      params.hSyncParams.displayPixels = 800;
      params.hSyncParams.frontPorchPixels = 40;
      params.hSyncParams.syncPixels = 128;
      params.hSyncParams.backPorchPixels = 88;
      params.hSyncParams.syncHigh = true;

      params.vSyncParams.displayPixels = 600;
      params.vSyncParams.frontPorchPixels = 1;
      params.vSyncParams.syncPixels = 4;
      params.vSyncParams.backPorchPixels = 23;
      params.vSyncParams.syncHigh = true;
      break;

    case VGA_1024_768_60HZ:  // http://tinyvga.com/vga-timing/1024x768@60Hz
      params.pixelClockKHz = 65000;
      params.hSyncParams.displayPixels = 1024;
      params.hSyncParams.frontPorchPixels = 24;
      params.hSyncParams.syncPixels = 136;
      params.hSyncParams.backPorchPixels = 160;
      params.hSyncParams.syncHigh = false;

      params.vSyncParams.displayPixels = 768;
      params.vSyncParams.frontPorchPixels = 5;
      params.vSyncParams.syncPixels = 6;
      params.vSyncParams.backPorchPixels = 27;
      params.vSyncParams.syncHigh = false;
      break;

    case VGA_1280_1024_60HZ:  // http://tinyvga.com/vga-timing/1280x1024@60Hz
      params.pixelClockKHz = 108000;
      params.hSyncParams.displayPixels = 1280;
      params.hSyncParams.frontPorchPixels = 48;
      params.hSyncParams.syncPixels = 112;
      params.hSyncParams.backPorchPixels = 248;
      params.hSyncParams.syncHigh = true;

      params.vSyncParams.displayPixels = 1024;
      params.vSyncParams.frontPorchPixels = 1;
      params.vSyncParams.syncPixels = 3;
      params.vSyncParams.backPorchPixels = 38;
      params.vSyncParams.syncHigh = true;
      break;
  }

  if (params.pixelClockKHz && params.hSyncParams.displayPixels && params.vSyncParams.displayPixels)
  {
    vgaUpdateTotalPixels(&params.hSyncParams);
    vgaUpdateTotalPixels(&params.vSyncParams);

    float scanlineTimeSeconds = 1.0f / (params.pixelClockKHz * 1000.0f) * params.hSyncParams.totalPixels;
    float frameTimeSeconds = scanlineTimeSeconds * params.vSyncParams.totalPixels;

    params.hSyncParams.freqHz = 1.0f / scanlineTimeSeconds;
    params.vSyncParams.freqHz = 1.0f / frameTimeSeconds;
  }

  params.hPixelScale = pixelScale;
  params.vPixelScale = pixelScale;
  params.hVirtualPixels = (params.hSyncParams.displayPixels / params.hPixelScale);
  params.vVirtualPixels = (params.vSyncParams.displayPixels / params.vPixelScale);

  return params;
}

/*
 * update total number of pixels
 */
void vgaUpdateTotalPixels(VgaSyncParams* params)
{
  if (params)
  {
    params->totalPixels = params->displayPixels;
    params->totalPixels += params->frontPorchPixels;
    params->totalPixels += params->syncPixels;
    params->totalPixels += params->backPorchPixels;
  }
}