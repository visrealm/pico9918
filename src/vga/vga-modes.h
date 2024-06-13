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

#pragma once

#include "vga.h"

typedef enum
{
  VGA_640_480_60HZ,
  VGA_640_400_70HZ,
  VGA_800_600_60HZ,
  VGA_1024_768_60HZ,
  VGA_1280_1024_60HZ,
} VgaMode;


/*
 * get the vga parameters for known modes
 */
VgaParams vgaGetParams(VgaMode mode);

/*
 * set the scale/multiplier of virtual pixel size
 */
bool setVgaParamsScale(VgaParams* params, int pixelScale);
bool setVgaParamsScaleXY(VgaParams* params, int pixelScaleX, int pixelScaleY);
bool setVgaParamsScaleX(VgaParams* params, int pixelScale);
bool setVgaParamsScaleY(VgaParams* params, int pixelScale);