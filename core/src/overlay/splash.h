/**
 * \file
 * \brief pico9918-core - Splash overlay
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * The splash logo the device shows over the bottom border for its first few
 * hundred frames, before the host enables the display. Device behaviour, not
 * decoration - an emulator that omits it is not emulating the device.
 *
 * Geometry arrives per call. The overlay owns no display parameters, so this
 * header carries no host dependency; the caller supplies the row counts it
 * already has.
 *
 * PICO9918_NO_SPLASH compiles this module to nothing (the build also drops the
 * image asset), so the calls remain valid and cost nothing.
 */

#ifndef _PICO9918_SPLASH_H
#define _PICO9918_SPLASH_H

#include "impl/platform.h"
#include "pico9918_build_config.h"

#include <stdint.h>

#ifndef PICO9918_NO_SPLASH
/** \brief set to 1 to build without the splash overlay */
#define PICO9918_NO_SPLASH 0
#endif

/*
 * This is the library's first public API to hand out a PICO9918_PIXEL_T buffer,
 * and the pixel width is a per-build choice, so a consumer compiled against a
 * different policy than the library would stride every write wrongly - with a
 * clean compile and a clean link, then corrupt pixels. Assert UNCONDITIONALLY
 * against the width recorded when the library was built.
 *
 * The width comes from the generated pico9918_build_config.h rather than from
 * a compile definition on purpose: a definition set on the library's CMake
 * target reaches the library's own translation units and nobody else, so it
 * cannot catch the case that matters. Found by adversarial review, which showed
 * a uint32 consumer linking a uint16 library and writing at half stride.
 */
_Static_assert(sizeof(PICO9918_PIXEL_T) == PICO9918_BUILD_PIXEL_SIZE,
               "PICO9918_PIXEL_T does not match the width this library was built "
               "with (see pico9918_build_config.h). Select the same pixel "
               "policy the library used - one ships, and it is the default in "
               "platform/ on both platforms.");

#ifdef __cplusplus
extern "C"
{
#endif

  /** restart the splash animation (after... reset) */
  void pico9918_splash_reset(void);

  /** allow the splash to animate back out - the host calls this once the display
 * has been enabled */
  void pico9918_splash_allow_hide(void);

  /**
 * render the splash logo into the scanline buffer, if row `y` falls in the
 * logo band. Also advances the animation, on y == 0.
 */
  void pico9918_splash_render(uint16_t y, uint32_t frameCount, uint32_t vBorder, uint32_t vPixels,
                            uint32_t vVirtualPixels, PICO9918_PIXEL_T* pixels);

#ifdef __cplusplus
}
#endif

#endif // _PICO9918_SPLASH_H
