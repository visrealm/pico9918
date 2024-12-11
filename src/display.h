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

#if PICO9918_SCART_RGBS
  #if PICO9918_SCART_PAL
    #define DISPLAY_MODE RGBS_PAL_720_576i_50HZ
    #define VIRTUAL_PIXELS_Y (576 / 2)
  #else
    #define DISPLAY_MODE RGBS_NTSC_720_480i_60HZ
    #define VIRTUAL_PIXELS_Y (480 / 2)
  #endif
  #define DISPLAY_YSCALE 1
  #define VIRTUAL_PIXELS_X 720
#else // VGA
  #define DISPLAY_MODE VGA_640_480_60HZ
  #define DISPLAY_YSCALE 2
  #define VIRTUAL_PIXELS_X 640
  #define VIRTUAL_PIXELS_Y (480 / DISPLAY_YSCALE)
#endif
