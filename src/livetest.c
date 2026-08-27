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

#include "livetest.h"

#include "pico/stdlib.h"
#include "hardware/dma.h"

#include <string.h>

/** \brief the capture buffer; no initialiser, or its zeros land in the image */
LiveTestCapture liveTestCapture;

/* vga.c claims 0/1 and the library claims 2, 4, 5, 6 and 7, leaving this the one free
   channel on both parts. Claimed at the first arm, so an overlap panics instead of
   silently fighting the library's layer copy for it. */
#define LIVE_TEST_DMA 3
static bool captureDmaReady = false;

/** \brief block until the capture DMA has finished with the line buffer */
void __time_critical_func(liveTestCaptureWait)(void)
{
  if (captureDmaReady) dma_channel_wait_for_finish_blocking(LIVE_TEST_DMA);
}

/** \brief record what a row cost, and count the rows that never arrived */
void __time_critical_func(liveTestNoteRow)(uint16_t y, uint32_t us)
{
  static uint16_t expected = 0;

  if (y < expected) /* the frame wrapped, so the count starts again at its first row */
    expected = 0;

  while (expected < y)
  {
    ++liveTestCapture.skipped;
    if (expected < LIVE_TEST_ROWS) liveTestCapture.skippedRows[expected >> 5] |= 1u << (expected & 31);
    ++expected;
  }
  expected = y + 1;

  if (y < LIVE_TEST_ROWS) liveTestCapture.lineTimes[y] = us > 255 ? 255 : us;
}

/** \brief copy a scanline into the window while a capture is armed */
void __time_critical_func(liveTestCaptureRow)(uint16_t y, uint16_t height, uint16_t width, const uint8_t* indices)
{
  static bool capturing = false;
  static bool crcOnly   = false;
  static uint32_t from  = 0;

  if (y == 0)
  {
    ++liveTestCapture.frame;

    if (capturing)
    {
      capturing = false;
      /* the CRC before the flag, or a reader that sees the flag first reads the
         previous window's */
      dma_channel_wait_for_finish_blocking(LIVE_TEST_DMA);
      liveTestCapture.crc = dma_sniffer_get_data_accumulator();
      dma_sniffer_disable();
      liveTestCapture.request = 0;
    }
    else if (liveTestCapture.request)
    {
      if (!captureDmaReady) dma_channel_claim(LIVE_TEST_DMA);
      capturing       = true;
      captureDmaReady = true;
      /* a whole-frame CRC needs every row through the sniffer, so this mode copies them
         all onto the same slot: only the accumulator is wanted */
      crcOnly = liveTestCapture.request == LIVE_TEST_REQUEST_CRC;
      /* latched for the whole frame, so a host that moves on while this one runs
         cannot slide the window under the copy */
      from                   = liveTestCapture.start;
      liveTestCapture.window = LIVE_TEST_WINDOW;
      /* the frame's own height, not the last row reached: a line that overruns is never
         rendered, so counting rows as they arrive reads a drop back as a short capture */
      liveTestCapture.rows = height;
      /* a line's width is the mode's, so the reader is told rather than assuming: a
         reference taken at one width cannot be compared against a capture at another */
      liveTestCapture.width = width;
      /* seed, reverse and invert are what make the sniffer's result the CRC-32 zlib
         computes, so the host needs no CRC of its own */
      dma_sniffer_enable(LIVE_TEST_DMA, DMA_SNIFF_CTRL_CALC_VALUE_CRC32R, true);
      dma_sniffer_set_data_accumulator(0xFFFFFFFF);
      dma_sniffer_set_output_reverse_enabled(true);
      dma_sniffer_set_output_invert_enabled(true);
      memset(liveTestCapture.seen, 0, sizeof(liveTestCapture.seen));
    }
  }

  if (!capturing) return;

  uint32_t row = 0;
  if (!crcOnly)
  {
    /* one unsigned compare covers both ends: a row before the window wraps large */
    row = (uint32_t)y - from;
    if (row >= LIVE_TEST_WINDOW) return;
  }

  /* Word-wide is safe: `indices` is always word-aligned and the destination a multiple of
     the line width from a word-aligned buffer. The config goes in every row rather than
     once at arm time - the channel is not claimed, so anything else may have dirtied it. */
  dma_channel_config cfg = dma_channel_get_default_config(LIVE_TEST_DMA);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&cfg, true);
  channel_config_set_write_increment(&cfg, true);
  channel_config_set_sniff_enable(&cfg, true);

  dma_channel_wait_for_finish_blocking(LIVE_TEST_DMA);
  dma_channel_set_config(LIVE_TEST_DMA, &cfg, false);
  dma_channel_set_read_addr(LIVE_TEST_DMA, indices, false);
  dma_channel_set_write_addr(LIVE_TEST_DMA, liveTestCapture.pixels + row * width, false);
  dma_channel_set_trans_count(LIVE_TEST_DMA, width >> 2, true);
  /* `seen` stays whole-frame: every row is still rendered, only the copy is windowed */
  liveTestCapture.seen[y >> 5] |= 1u << (y & 31);
}
