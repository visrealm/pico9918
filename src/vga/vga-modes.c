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

#include <string.h>

void vgaUpdateTotalPixels(VgaSyncParams* params);

/*
 * Base timing parameters for all modes (progressive and interlaced)
 * References: http://tinyvga.com/vga-timing
 */
typedef struct {
  uint32_t pixelClockKHz;
  VgaSyncParams hSyncParams;
  VgaSyncParams vSyncParams;
  float frameRateHz;
} VgaModeBase;


/*
 * Interlaced extension parameters (PAL/NTSC only)
 */
typedef struct {
  uint8_t interlacedFieldOrder;
  uint8_t shortPulsePixels;
  VgaFieldParams fields[VGA_MAX_FIELDS];
} VgaModeInterlaced;

#define VGA_MODE_COUNT (RGBS_NTSC_720_480i_60HZ + 1)
#define INTERLACED_MODE_COUNT (RGBS_NTSC_720_480i_60HZ - RGBS_PAL_720_576i_50HZ + 1)
#define FIRST_INTERLACED_MODE RGBS_PAL_720_576i_50HZ

// References:
// http://tinyvga.com/vga-timing/640x480@60Hz
// http://tinyvga.com/vga-timing/640x400@70Hz
// http://tinyvga.com/vga-timing/800x600@60Hz
// http://tinyvga.com/vga-timing/1024x768@60Hz
// http://tinyvga.com/vga-timing/1280x1024@60Hz

static const VgaModeBase vgaModeBase[VGA_MODE_COUNT] = {
  [VGA_640_480_60HZ] = {
    .pixelClockKHz = 25175,
    .hSyncParams = {
      .displayPixels    = 640,
      .frontPorchPixels = 16,
      .syncPixels       = 96,
      .backPorchPixels  = 48,
      .syncHigh         = false
    },
    .vSyncParams = {
      .displayPixels    = 480,
      .frontPorchPixels = 10,
      .syncPixels       = 2,
      .backPorchPixels  = 33,
      .syncHigh         = false
    },
    .frameRateHz = 60.0f
  },

#if VGA_MODE_ADDITIONAL
  [VGA_640_400_70HZ] = {
    .pixelClockKHz = 25175,
    .hSyncParams = {
      .displayPixels    = 640,
      .frontPorchPixels = 16,
      .syncPixels       = 96,
      .backPorchPixels  = 48,
      .syncHigh         = false
    },
    .vSyncParams = {
      .displayPixels    = 400,
      .frontPorchPixels = 12,
      .syncPixels       = 2,
      .backPorchPixels  = 35,
      .syncHigh         = true
    },
    .frameRateHz = 70.0f
  },

  [VGA_800_600_60HZ] = {
    .pixelClockKHz = 40000,
    .hSyncParams = {
      .displayPixels    = 800,
      .frontPorchPixels = 40,
      .syncPixels       = 128,
      .backPorchPixels  = 88,
      .syncHigh         = true
    },
    .vSyncParams = {
      .displayPixels    = 600,
      .frontPorchPixels = 1,
      .syncPixels       = 4,
      .backPorchPixels  = 23,
      .syncHigh         = true
    },
    .frameRateHz = 60.0f
  },

  [VGA_1024_768_60HZ] = {
    .pixelClockKHz = 65000,
    .hSyncParams = {
      .displayPixels    = 1024,
      .frontPorchPixels = 24,
      .syncPixels       = 136,
      .backPorchPixels  = 160,
      .syncHigh         = false
    },
    .vSyncParams = {
      .displayPixels    = 768,
      .frontPorchPixels = 5,
      .syncPixels       = 6,
      .backPorchPixels  = 27,
      .syncHigh         = false
    },
    .frameRateHz = 60.0f
  },

  [VGA_1280_1024_60HZ] = {
    .pixelClockKHz = 108000,
    .hSyncParams = {
      .displayPixels    = 1280,
      .frontPorchPixels = 48,
      .syncPixels       = 112,
      .backPorchPixels  = 248,
      .syncHigh         = true
    },
    .vSyncParams = {
      .displayPixels    = 1024,
      .frontPorchPixels = 1,
      .syncPixels       = 3,
      .backPorchPixels  = 38,
      .syncHigh         = true
    },
    .frameRateHz = 60.0f
  },
#endif

  // Overscan borders baked into timing: 42px horizontal, 10 lines vertical per side
  // Display area reduced, porches grown by same amount. Totals unchanged.
  #define SCART_H_BORDER 42
  #define SCART_V_BORDER 10

  [RGBS_PAL_720_576i_50HZ] = {
    .pixelClockKHz = 13500,
    // Standard PAL: 864 pixels/line = 64us at 13.5MHz
    .hSyncParams = {
      .displayPixels    = 720 - SCART_H_BORDER * 2,  // 636
      .frontPorchPixels = 12  + SCART_H_BORDER,      // 54
      .syncPixels       = 64,
      .backPorchPixels  = 68  + SCART_H_BORDER,      // 110  (total = 864)
      .syncHigh         = false
    },
    // Interlaced: 312.5 lines/field, 625 lines/frame
    .vSyncParams = {
      .displayPixels    = 576 / 2 - SCART_V_BORDER * 2,  // 268
      .syncHigh         = false
    },
    .frameRateHz = 50.0f
  },

  [RGBS_NTSC_720_480i_60HZ] = {
    .pixelClockKHz = 13500,
    // BT.601 NTSC: 858 pixels/line = 63.555us at 13.5MHz
    .hSyncParams = {
      .displayPixels    = 720 - SCART_H_BORDER * 2,  // 636
      .frontPorchPixels = 16  + SCART_H_BORDER,      // 58
      .syncPixels       = 64,
      .backPorchPixels  = 58  + SCART_H_BORDER,      // 100  (total = 858)
      .syncHigh         = false
    },
    // Interlaced: 262.5 lines/field, 525 lines/frame, 59.94Hz
    .vSyncParams = {
      .displayPixels    = 480 / 2 - SCART_V_BORDER * 2,  // 220
      .syncHigh         = false
    },
    .frameRateHz = 60.0f
  },
};

/*
 * Interlaced mode extensions
 * Indexed by (mode - FIRST_INTERLACED_MODE)
 *
 * All DMA transfers are 4 words (one full line = 2 half-lines).
 * EQ (short sync) pulse: ~2us
 * LS (long sync) pulse: halfLine - EQ (derived automatically)
 */
static const VgaModeInterlaced vgaModeInterlaced[INTERLACED_MODE_COUNT] = {
  [RGBS_PAL_720_576i_50HZ - FIRST_INTERLACED_MODE] = {
    // PAL: field 0 is lower raster position
    .interlacedFieldOrder = 1,
    // EQ pulse: 2us = 27 pixels at 13.5MHz
    .shortPulsePixels = 27,
    .fields = {
      // Field 1 (313 lines): starts at whole-line boundary
      //   Vsync:    LsLs LsLs LsEq EqEq EqEq  (5 lines)
      //   Porch:    31 lines (back porch + top border, normal hsync)
      //   Active:   268 lines
      //   Trailing: 9 lines (bottom border + EqLs interlace transition)
      [0] = {
        .vsyncLines     = 5,
        .vsyncPattern   = { VSYNC_LSLS, VSYNC_LSLS, VSYNC_LSEQ, VSYNC_EQEQ, VSYNC_EQEQ },
        .porchLines     = 31,
        .activeLines    = 268,
        .trailingLines  = 9,
        .trailingPattern = { VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_EQLS },
        .totalLines     = 313
      },
      // Field 2 (312 lines): starts at half-line boundary (after F1's EqLs)
      //   Vsync:    LsLs LsLs LsEq EqEq        (4 lines)
      //   Porch:    31 lines (back porch + top border, normal hsync)
      //   Active:   268 lines
      //   Trailing: 9 lines (bottom border, all normal porch)
      [1] = {
        .vsyncLines     = 4,
        .vsyncPattern   = { VSYNC_LSLS, VSYNC_LSLS, VSYNC_LSEQ, VSYNC_EQEQ },
        .porchLines     = 31,
        .activeLines    = 268,
        .trailingLines  = 9,
        .trailingPattern = { VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH },
        .totalLines     = 312
      },
    }
  },

  [RGBS_NTSC_720_480i_60HZ - FIRST_INTERLACED_MODE] = {
    .interlacedFieldOrder = 0,
    // EQ pulse: 2.3us = 31 pixels at 13.5MHz
    .shortPulsePixels = 31,
    .fields = {
      // Field 1 (263 lines)
      //   Vsync:    LsLs LsLs LsLs EqEq EqEq EqEq  (6 lines)
      //   Porch:    25 lines (back porch + top border)
      //   Active:   220 lines
      //   Trailing: 12 lines (bottom border + EqEq EqLs interlace transition)
      [0] = {
        .vsyncLines     = 6,
        .vsyncPattern   = { VSYNC_LSLS, VSYNC_LSLS, VSYNC_LSLS, VSYNC_EQEQ, VSYNC_EQEQ, VSYNC_EQEQ },
        .porchLines     = 25,
        .activeLines    = 220,
        .trailingLines  = 12,
        .trailingPattern = { VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_EQEQ, VSYNC_EQLS },
        .totalLines     = 263
      },
      // Field 2 (262 lines): starts at half-line boundary (after F1's EqLs)
      //   Vsync:    LsLs LsLs LsLs EqEq EqEq EqEq  (6 lines)
      //   Porch:    25 lines (back porch + top border)
      //   Active:   220 lines
      //   Trailing: 11 lines (bottom border, all normal porch)
      [1] = {
        .vsyncLines     = 6,
        .vsyncPattern   = { VSYNC_LSLS, VSYNC_LSLS, VSYNC_LSLS, VSYNC_EQEQ, VSYNC_EQEQ, VSYNC_EQEQ },
        .porchLines     = 25,
        .activeLines    = 220,
        .trailingLines  = 11,
        .trailingPattern = { VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH, VSYNC_PORCH },
        .totalLines     = 262
      },
    }
  },
};

/*
 * populate VgaParams for known vga modes
 */
VgaParams vgaGetParams(VgaMode mode)
{
  VgaParams params = { 0 };

  if (mode >= VGA_MODE_COUNT)
    return params;

  const VgaModeBase* base = &vgaModeBase[mode];
  params.pixelClockKHz = base->pixelClockKHz;
  params.hSyncParams   = base->hSyncParams;
  params.vSyncParams   = base->vSyncParams;
  params.frameRateHz   = base->frameRateHz;

  if (mode >= FIRST_INTERLACED_MODE)
  {
    const VgaModeInterlaced* ilc = &vgaModeInterlaced[mode - FIRST_INTERLACED_MODE];
    params.interlaced          = true;
    params.numFields           = 2;
    params.interlacedFieldOrder = ilc->interlacedFieldOrder;
    params.shortPulsePixels    = ilc->shortPulsePixels;
    memcpy(params.fields, ilc->fields, sizeof(ilc->fields));
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
