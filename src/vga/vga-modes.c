/*
 * Project: pico9918 - vga
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 */

#include "vga-modes.h"

void vgaUpdateTotalPixels(VgaSyncParams* params);

/*
 * populate VgaParams for known vga modes
 */
VgaParams vgaGetParams(VgaMode mode)
{
  VgaParams params = { 0 };

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

  case RGBS_PAL_720_576i_50HZ:
      params.pixelClockKHz = 13500;
      // Standard PAL: 864 pixels/line = 64us at 13.5MHz
      params.hSyncParams.displayPixels = 720;
      params.hSyncParams.frontPorchPixels = 12;
      params.hSyncParams.syncPixels = 64;
      params.hSyncParams.backPorchPixels = 68;    // total=864
      params.hSyncParams.syncHigh = false;

      // Interlaced: 312.5 lines/field, 625 lines/frame.
      // All DMA transfers are 4 words (one full line = 64us = 2 half-lines).
      //   F1 (313): LsLs LsLs LsEq EqEq EqEq [17 porch] [288 active] EqEq EqEq EqLs
      //   F2 (312): LsLs LsLs EqEq EqEq       [17 porch] [288 active] EqEq EqEq EqEq
      // F1 ends with EqLs — the interlace transition line (half-line offset).
      params.vSyncParams.displayPixels = 576 / 2; // for vVirtualPixels derivation
      params.vSyncParams.syncHigh = false;
      params.interlaced = true;
      params.numFields = 2;

      // EQ (short sync) pulse: 2us = 27 pixels at 13.5MHz
      // LS pulse = halfLine - EQ = 432 - 27 = 405 pixels (derived automatically)
      params.halfLineSync.shortPulsePixels = 27;

      // Field 1 (313 lines): starts at whole-line boundary
      //   Vsync:    LsLs LsLs LsEq EqEq EqEq  (5 lines)
      //   Porch:    17 lines (back porch, normal hsync)
      //   Active:   288 lines
      //   Trailing: Porch Porch EqLs             (3 lines — last line is interlace transition)
      // Hsyncs between LS: 17 porch + 288 active + 2 trailing porch = 307
      params.fields[0].vsyncLines = 5;
      params.fields[0].vsyncPattern[0] = VSYNC_LSLS;
      params.fields[0].vsyncPattern[1] = VSYNC_LSLS;
      params.fields[0].vsyncPattern[2] = VSYNC_LSEQ;
      params.fields[0].vsyncPattern[3] = VSYNC_EQEQ;
      params.fields[0].vsyncPattern[4] = VSYNC_EQEQ;
      params.fields[0].porchLines = 17;
      params.fields[0].activeLines = 288;
      params.fields[0].trailingLines = 3;
      params.fields[0].trailingPattern[0] = VSYNC_PORCH;
      params.fields[0].trailingPattern[1] = VSYNC_PORCH;
      params.fields[0].trailingPattern[2] = VSYNC_EQLS;  // interlace transition
      params.fields[0].totalLines = 313;

      // Field 2 (312 lines): starts at half-line boundary (after F1's EqLs)
      //   Vsync:    LsLs LsLs LsEq EqEq        (4 lines)
      //   Porch:    17 lines (back porch, normal hsync)
      //   Active:   288 lines
      //   Trailing: Porch Porch Porch            (3 lines — all normal porch)
      // Hsyncs between LS: 17 porch + 288 active + 3 trailing porch = 308
      params.fields[1].vsyncLines = 4;
      params.fields[1].vsyncPattern[0] = VSYNC_LSLS;
      params.fields[1].vsyncPattern[1] = VSYNC_LSLS;
      params.fields[1].vsyncPattern[2] = VSYNC_LSEQ;
      params.fields[1].vsyncPattern[3] = VSYNC_EQEQ;
      params.fields[1].porchLines = 17;
      params.fields[1].activeLines = 288;
      params.fields[1].trailingLines = 3;
      params.fields[1].trailingPattern[0] = VSYNC_PORCH;
      params.fields[1].trailingPattern[1] = VSYNC_PORCH;
      params.fields[1].trailingPattern[2] = VSYNC_PORCH;
      params.fields[1].totalLines = 312;
      break;

  case RGBS_NTSC_720_480i_60HZ:
      params.pixelClockKHz = 13500;
      // BT.601 NTSC: 858 pixels/line = 63.555us at 13.5MHz
      params.hSyncParams.displayPixels = 720;
      params.hSyncParams.frontPorchPixels = 16;
      params.hSyncParams.syncPixels = 64;
      params.hSyncParams.backPorchPixels = 58;    // total=858
      params.hSyncParams.syncHigh = false;

      // Interlaced: 262.5 lines/field, 525 lines/frame, 59.94Hz.
      // NTSC vsync: 6 LS half-lines = 3 LsLs lines per field.
      //   F1 (263): LsLs LsLs LsLs EqEq EqEq EqEq [15 porch] [240 active] porch EqLs
      //   F2 (262): LsLs LsLs LsLs EqEq EqEq EqEq [15 porch] [240 active] porch
      params.vSyncParams.displayPixels = 480 / 2;
      params.vSyncParams.syncHigh = false;
      params.interlaced = true;
      params.numFields = 2;

      // EQ (short sync) pulse: 2.3us = 31 pixels at 13.5MHz
      // LS pulse = halfLine - EQ = 429 - 31 = 398 pixels (derived automatically)
      params.halfLineSync.shortPulsePixels = 31;

      // Field 1 (263 lines)
      params.fields[0].vsyncLines = 6;
      params.fields[0].vsyncPattern[0] = VSYNC_LSLS;
      params.fields[0].vsyncPattern[1] = VSYNC_LSLS;
      params.fields[0].vsyncPattern[2] = VSYNC_LSLS;
      params.fields[0].vsyncPattern[3] = VSYNC_EQEQ;
      params.fields[0].vsyncPattern[4] = VSYNC_EQEQ;
      params.fields[0].vsyncPattern[5] = VSYNC_EQEQ;
      params.fields[0].porchLines = 15;
      params.fields[0].activeLines = 240;
      params.fields[0].trailingLines = 2;
      params.fields[0].trailingPattern[0] = VSYNC_EQEQ;
      params.fields[0].trailingPattern[1] = VSYNC_EQLS;  // interlace transition
      params.fields[0].totalLines = 263;

      // Field 2 (262 lines): starts at half-line boundary (after F1's EqLs)
      params.fields[1].vsyncLines = 6;
      params.fields[1].vsyncPattern[0] = VSYNC_LSLS;
      params.fields[1].vsyncPattern[1] = VSYNC_LSLS;
      params.fields[1].vsyncPattern[2] = VSYNC_LSLS;
      params.fields[1].vsyncPattern[3] = VSYNC_EQEQ;
      params.fields[1].vsyncPattern[4] = VSYNC_EQEQ;
      params.fields[1].vsyncPattern[5] = VSYNC_EQEQ;
      params.fields[1].porchLines = 15;
      params.fields[1].activeLines = 240;
      params.fields[1].trailingLines = 1;
      params.fields[1].trailingPattern[0] = VSYNC_PORCH;
      params.fields[1].totalLines = 262;
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

  setVgaParamsScale(&params, 1);

  if (params.interlaced)
  {
    // SCART: narrower virtual area within the 720px buffer for black side margins
    params.hVirtualPixels = 624;
    // SCART: reduce virtual height to create black top/bottom margins
    params.vVirtualPixels = params.vSyncParams.displayPixels - 24;
  }

  return params;
}

/*
 * set the scale/multiplier of virtual pixel size
 */
bool setVgaParamsScale(VgaParams* params, int pixelScale)
{
  return setVgaParamsScaleX(params, pixelScale) &&
    setVgaParamsScaleY(params, pixelScale);
}

bool setVgaParamsScaleXY(VgaParams* params, int pixelScaleX, int pixelScaleY)
{
  return setVgaParamsScaleX(params, pixelScaleX) &&
    setVgaParamsScaleY(params, pixelScaleY);
}

bool setVgaParamsScaleX(VgaParams* params, int pixelScale)
{
  if (!params || pixelScale < 1) return false;

  params->hPixelScale = pixelScale;
  params->hVirtualPixels = (params->hSyncParams.displayPixels / params->hPixelScale);
  return true;
}

bool setVgaParamsScaleY(VgaParams* params, int pixelScale)
{
  if (!params || pixelScale < 1) return false;

  params->vPixelScale = pixelScale;
  params->vVirtualPixels = (params->vSyncParams.displayPixels / params->vPixelScale);
  return true;
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