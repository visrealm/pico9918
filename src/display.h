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

#if PICO9918_SCART_AUTODETECT
  // Autodetect: allocate worst-case buffer width, defer mode selection to runtime
  #define RGB_PIXELS_X 642    // 640 + 2 guard pixels for PIO autopull
  // DISPLAY_MODE and DISPLAY_YSCALE are not defined — use runtime functions instead
#elif PICO9918_SCART_RGBS
  #if PICO9918_SCART_PAL
    #define DISPLAY_MODE RGBS_PAL_720_576i_50HZ
  #else
    #define DISPLAY_MODE RGBS_NTSC_720_480i_60HZ
  #endif
  #define DISPLAY_YSCALE 1
  #define RGB_PIXELS_X 638    // 636 + 2 guard pixels for PIO autopull
#else // VGA
  #define DISPLAY_MODE VGA_640_480_60HZ
  #define DISPLAY_YSCALE 2
  #define RGB_PIXELS_X 642    // 640 + 2 guard pixels for PIO autopull
#endif
