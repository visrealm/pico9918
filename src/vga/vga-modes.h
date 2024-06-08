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

extern VgaParams vgaGetParams(VgaMode mode, int pixelScale);
