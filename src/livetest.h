/**
 * \file
 * \brief live test capture: the rendered image, read back over SWD
 *
 * Project: pico9918
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

#pragma once

#include <stdint.h>
#include "impl/pico9918_priv.h"

/* Test builds only (PICO9918_LIVE_TEST). Reading the buffer over SWD takes far
   longer than a frame, so the host arms `request`, the firmware captures one window
   and clears it, and the buffer then holds still until the next arm. Cover a whole
   frame by advancing `start` and arming again. */

/** \brief tallest frame the renderer produces: 30 rows of 8 with R0 row doubling */
#define LIVE_TEST_ROWS 480

/** \brief rows the buffer holds at once; divides 192, 240, 384 and 480 evenly */
#define LIVE_TEST_WINDOW 48

/** \brief widest line any mode on this build renders */
#define LIVE_TEST_PIXELS_X SCANLINE_BYTES_MAX

/** \brief capture a window of pixels */
#define LIVE_TEST_REQUEST_WINDOW 1
/** \brief capture a CRC of the whole frame instead, which takes one armed frame */
#define LIVE_TEST_REQUEST_CRC 2

/** \brief capture buffer, laid out for a host reading it over SWD */
typedef struct
{
  volatile uint32_t request;                 ///< host writes LIVE_TEST_REQUEST_*; firmware clears it when done
  uint32_t frame;                            ///< frames rendered since boot
  uint32_t rows;                             ///< rows in the captured frame, the mode's own height
  uint32_t width;                            ///< bytes in each row, which the mode decides
  uint32_t window;                           ///< rows this buffer holds at once
  volatile uint32_t start;                   ///< first row of the window; host sets it before arming
  uint32_t crc;                              ///< CRC-32 of the captured bytes, as zlib computes it
  uint32_t seen[(LIVE_TEST_ROWS + 31) / 32]; ///< rows actually rendered; a clear bit is a dropped, stale row
  uint8_t pixels[LIVE_TEST_WINDOW * LIVE_TEST_PIXELS_X]; ///< the window, rows * width contiguous bytes
  uint32_t skipped;                                      ///< lines core 1 dropped, armed or not; host clears both
  uint32_t skippedRows[(LIVE_TEST_ROWS + 31) / 32];      ///< which rows those were
  uint8_t lineTimes[LIVE_TEST_ROWS]; ///< microseconds per row, saturating; an overrunning row shows only here
} LiveTestCapture;

/** \brief the one capture buffer, which the host finds by symbol */
extern LiveTestCapture liveTestCapture;

/* The three capture entry points are declared in pico9918HostOps.h, beside the
   PICO9918_LINE_* macros the library expands them through - one declaration site,
   and one this header cannot host without an include cycle. */
