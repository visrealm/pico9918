/**
 * \file
 * \brief the known VGA and RGBs display modes and their pixel scaling
 *
 * Project: pico9918 - vga
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

#pragma once

#include "vga.h"

/** \brief the display modes vgaGetParams() knows */
typedef enum
{
  VGA_640_480_60HZ,
#if VGA_MODE_ADDITIONAL
  VGA_640_400_70HZ,
  VGA_800_600_60HZ,
  VGA_1024_768_60HZ,
  VGA_1280_1024_60HZ,
#endif
  RGBS_PAL_720_576i_50HZ,
  RGBS_NTSC_720_480i_60HZ,
} VgaMode;


/** \brief  the vga parameters for a known mode, at pixel scale 1
 *  \return the mode's parameters, or all zeroes if \p mode is not known
 */
VgaParams vgaGetParams(VgaMode mode);

/** \brief set both pixel scales, false if \p params is null or the scale is below 1 */
bool setVgaParamsScale(VgaParams* params, int pixelScale);

/** \brief set the horizontal pixel scale, which redefines hVirtualPixels */
bool setVgaParamsScaleX(VgaParams* params, int pixelScale);

/** \brief set the vertical pixel scale, which redefines vVirtualPixels */
bool setVgaParamsScaleY(VgaParams* params, int pixelScale);
