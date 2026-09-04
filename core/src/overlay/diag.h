/**
 * \file
 * \brief pico9918-core - Diagnostics overlay
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * The diagnostics panels the device draws over the borders when the PICO9918_CONF_DIAG*
 * config bytes are set: render timings, frame rate, GPU load, temperature, the
 * register dump, the table addresses and the palette strip. Device behaviour,
 * not decoration - the panels are driven entirely by library-owned config bytes
 * rendering library-owned state.
 *
 * Geometry arrives per call, like the splash overlay. Everything the library
 * cannot know - the host's frame rate, its dropped-frame accounting, its board
 * revision strings and its display-mode labels - arrives through push setters,
 * called per frame or rarer. The library never reaches back into the host.
 *
 * pico9918_diag_render_text is public because the host also draws its own text over
 * the border (the pending-display banner); the font and the glyph blitter live
 * here, so there is one text path rather than two.
 */

#ifndef _PICO9918_DIAG_H
#define _PICO9918_DIAG_H

#include "impl/platform.h"
#include "pico9918.h"
#include "pico9918_build_config.h"

#include <stdint.h>

/*
 * Optional GPU-frames row. The count is host-pushed (the host's frame hook sees
 * the F18A status bit), so the row and its setter appear together or not at all.
 */
#ifndef PICO9918_DIAG_GPU_FRAME_COUNTER
/** \brief set to 1 to build the GPU-frames row and its setter */
#define PICO9918_DIAG_GPU_FRAME_COUNTER 0
#endif

/* glyph cell size of the built-in font, for callers that centre text */
#define PICO9918_DIAG_CHAR_WIDTH  6 /**< glyph cell width, pixels */
#define PICO9918_DIAG_CHAR_HEIGHT 6 /**< glyph cell height, pixels */

/*
 * This API hands out a PICO9918_PIXEL_T buffer and the pixel width is a per-build
 * choice, so a consumer compiled against a different policy than the library
 * would stride every write wrongly - with a clean compile and a clean link, then
 * corrupt pixels. Assert UNCONDITIONALLY against the width recorded when the
 * library was built. (Same reasoning as overlay/splash.h; see the note
 * there on why a compile definition cannot do this job.)
 */
PICO9918_STATIC_ASSERT(sizeof(PICO9918_PIXEL_T) == PICO9918_BUILD_PIXEL_SIZE,
                       "PICO9918_PIXEL_T does not match the width this library was built "
                       "with (see pico9918_build_config.h). Select the same pixel "
                       "policy the library used - one ships, and it is the default in "
                       "platform/ on both platforms.");

#ifdef __cplusplus
extern "C"
{
#endif

  /** one-time initialisation of the panel value strings */
  void pico9918_diag_init(void);

  /** rebuild the panel row table - call whenever the PICO9918_CONF_DIAG* bytes change */
  void pico9918_diag_config_updated(PICO9918_INST_ONLY_ARG);

  /** core temperature, degrees C */
  void pico9918_diag_set_temperature(float tempC);

  /** system clock, Hz */
  void pico9918_diag_set_clock_hz(float clockHz);

  /**
 * Host display timing, Hz. The library has no clock of its own, so the FPS row is
 * (16 - droppedFrames) * (frameRate / 16); only this term is pushed. The dropped
 * frames come from the frame module, which owns that accounting and is read
 * directly.
 */
  void pico9918_diag_set_frame_rate(float frameRateHz);

  /**
 * Version identity for the HWVER / FWVER rows. The strings are host policy: only
 * the host knows its board revisions and its own firmware version, and the
 * library must not carry PICO9918 revision knowledge. Both are copied into the
 * panel buffers, so the caller keeps no lifetime obligation. Either may be NULL
 * to leave that row's current text alone.
 */
  void pico9918_diag_set_version_info(const char* hwVersion, const char* fwVersion);

  /**
 * Display-mode label for the OUTPUT row, e.g. "480P " + "@60". The encoding of
 * PICO9918_CONF_DISP_DRIVER is host policy (which timings a board supports), so the host
 * supplies the label rather than the library carrying board-specific strings.
 * `name` is copied; `units` is retained by pointer, so it must have static
 * storage duration. Either may be NULL to leave that part alone.
 */
  void pico9918_diag_set_output_name(const char* name, const char* units);

  /** accumulate one scanline's render and total time, in microseconds */
  void pico9918_diag_update_render_time(uint32_t renderTime, uint32_t frameTime);

  /** recompute the panel values - call once per frame */
  void pico9918_diag_update(PICO9918_INST_ARG uint32_t frameCount);

  /**
 * render text into the scanline buffer, if row `scanline` falls in the glyph
 * band starting at `y`. Returns the x position just past the last pixel written,
 * so calls chain. A cell's unlit pixels are darkened, not left untouched.
 * `x` must be a whole number of ink words - cells are written a word at a time.
 */
  int pico9918_diag_render_text(uint16_t scanline, const char* text, uint16_t x, uint16_t y, PICO9918_PIXEL_T fg,
                             PICO9918_PIXEL_T* pixels);

  /** render the diagnostics panels for border row `y` */
  void pico9918_diag_render(PICO9918_INST_ARG uint16_t y, uint32_t vVirtualPixels,
                            PICO9918_PIXEL_T* pixels);

#ifdef __cplusplus
}
#endif

#endif // _PICO9918_DIAG_H
