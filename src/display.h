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

#pragma once

#if PICO9918_ENABLE_SCART
  // SCART autodetect: allocate worst-case buffer width, defer mode selection to runtime.
  #define RGB_PIXELS_X 642    // 640 + 2 guard pixels for PIO autopull
#else // VGA-only
  #define DISPLAY_MODE VGA_640_480_60HZ
  #define DISPLAY_YSCALE 2
  #define RGB_PIXELS_X 642    // 640 + 2 guard pixels for PIO autopull
#endif
