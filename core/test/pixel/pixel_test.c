/**
 * \file
 * \brief pico9918-core - the post-palette pixel path
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Everything between a palette index and a host's framebuffer, which nothing else
 * covers: the goldens render a 256-byte line and digest it, and the scene suite
 * compares palette INDEXES. Neither looks at a wide row's colours, and neither looks
 * at where in the scanline the picture lands.
 *
 * Both are load-bearing for a host. The LUT has two layouts - a pixel pair per entry
 * for every mode that doubles, and a nibble pair for the 4bpp 80-column line - and a
 * row that picks the wrong one still renders, in the wrong colours. The geometry is
 * denominated in 32-bit words holding two pixels, so a host that reads the window at
 * the wrong offset gets the border fill through the middle of its picture.
 */

#include "impl/pico9918_priv.h"
#include "pico9918_frame.h"

#include <stdio.h>
#include <string.h>

#define H_VIRTUAL 640u
#define H_BORDER  ((H_VIRTUAL - TMS9918_PIXELS_X * 2u) / 2u) /* 64 pixels each side */
#define SENTINEL  0xbeefu

static pico9918_scanline_params_t params = {H_VIRTUAL, 240, false, 0};
static PICO9918_PIXEL_T           line[H_VIRTUAL + 16];

static int failures;

static void fail(const char* what, unsigned where, unsigned wanted, unsigned got)
{
  if (++failures <= 8) printf("  FAIL %s at %u: want %04x got %04x\n", what, where, wanted, got);
}

static void regWrite(uint8_t reg, uint8_t value)
{
  pico9918_write_reg_value(PICO9918_INST 0x80 | reg, value);
}

/* Two writes of 0x1c to R57, which is what an F18A answers to. */
static void unlock(void)
{
  regWrite(0x39, 0x1c);
  regWrite(0x39, 0x1c);
}

/* A palette no two entries of which share a colour, so an entry reached through the
   wrong layout cannot accidentally match the right one. */
static void distinctPalette(void)
{
  for (int i = 0; i < 64; ++i) tms9918->vram.map.pram[i] = (uint16_t)(0xf000 | (i * 0x111));
}

static uint16_t want(int index)
{
  return (uint16_t)PICO9918_PIXEL_FROM_RGB12(tms9918->vram.map.pram[index]);
}

static void renderLines(uint16_t from, uint16_t to)
{
  for (uint16_t y = from; y < to; ++y) pico9918_frame_scanline(PICO9918_INST y, &params, line);
}

/* A line inside the picture, which is where the rebuild-then-render order applies.
   Border lines rebuild the LUT on one specific line and render nothing. */
static uint16_t activeLine(void)
{
  return (uint16_t)(pico9918_v_border_impl(PICO9918_INST_ONLY) + 10);
}

/* ONE line, so a rebuild that arrives a line late is a failure rather than something
   the next 19 lines paper over. pico9918_frame_scanline rebuilds a dirty LUT BEFORE
   it renders, so a state change that only becomes visible inside pico9918_scan_line
   has already missed this line - and this line is a wide row either way, because
   pico9918_line_bytes() reads the unlock live. */
static void renderOneActiveLine(void)
{
  const uint16_t y = activeLine();
  renderLines(y, y + 1);
}

static void text80(void)
{
  regWrite(0, 0x04); /* TEXT80 */
  regWrite(1, 0xd0); /* text, display active */
  regWrite(2, 0x0c);
  regWrite(4, 0x01);
  regWrite(7, 0xf4);
}

static void checkLineBytes(const char* stage, uint32_t wanted)
{
  const uint32_t got = pico9918_line_bytes(PICO9918_INST_ONLY);
  if (got != wanted) fail(stage, 0, wanted, got);
}

/*
 * The doubled layout: 64 entries, each the same pixel in both halves of the word. A
 * mode that doubles stores one entry per output pair; a wide 80-column row indexes
 * the same entries with a whole byte and takes one half. So every one of the 64
 * palette addresses must carry its own colour, twice.
 *
 * Indexes 0-15 come out right under EITHER layout - a nibble-pair build writes them
 * first - so a check that stopped there would pass on the wrong LUT. It is 16-63, the
 * palette SELECT the 8bpp tier exists to provide, that tells the two apart.
 */
static void checkDoubledLayout(const char* stage)
{
  for (int i = 0; i < 64; ++i)
  {
    const uint32_t entry = pico9918_palette_lut[i];
    if ((uint16_t)entry != want(i)) fail(stage, (unsigned)i, want(i), (uint16_t)entry);
    if ((uint16_t)(entry >> 16) != want(i)) fail(stage, (unsigned)i, want(i), (uint16_t)(entry >> 16));
  }
}

/*
 * The 4bpp layout: one byte is two pixels, so entry (high << 4) | low holds two
 * DIFFERENT colours. The high nibble is the left pixel and lands in the LOW half of
 * the word, which is where a little-endian pair writer puts the earlier pixel.
 *
 * Entries below 16 are the exception: an index under 16 still means one colour, so
 * they are doubled like any other mode's. Same rule the golden reference states.
 */
static void checkPackedLayout(const char* stage)
{
  for (unsigned index = 0; index < 256; ++index)
  {
    const int high = index < 16 ? (int)index : (int)(index >> 4);
    const int low  = index < 16 ? (int)index : (int)(index & 0x0f);

    const uint32_t entry = pico9918_palette_lut[index];
    if ((uint16_t)entry != want(high)) fail(stage, index, want(high), (uint16_t)entry);
    if ((uint16_t)(entry >> 16) != want(low)) fail(stage, index, want(low), (uint16_t)(entry >> 16));
  }
}

/*
 * Where the picture lands. Every mode fills the same window - 512 pixels between two
 * 64-pixel borders - because a 256-wide mode doubles into it and a wide row already
 * fills it. Nothing crashes when a host gets this wrong, so only a check like this
 * says so: the sentinel sweep pins the fill counts, the border sweep pins the offset,
 * and the pair sweep pins the doubling itself.
 */
static void checkGeometry(const char* stage, int doubled)
{
  for (unsigned x = 0; x < H_VIRTUAL + 16; ++x) line[x] = SENTINEL;
  renderOneActiveLine();

  for (unsigned x = 0; x < H_VIRTUAL; ++x)
    if (line[x] == SENTINEL) fail(stage, x, 0, SENTINEL);

  const uint16_t bg = (uint16_t)(pico9918_border_bg & 0xffff);
  for (unsigned x = 0; x < H_BORDER; ++x)
  {
    if (line[x] != bg) fail(stage, x, bg, line[x]);
    if (line[H_VIRTUAL - 1 - x] != bg) fail(stage, H_VIRTUAL - 1 - x, bg, line[H_VIRTUAL - 1 - x]);
  }

  if (doubled)
    for (unsigned x = 0; x < TMS9918_PIXELS_X * 2u; x += 2)
      if (line[H_BORDER + x] != line[H_BORDER + x + 1])
        fail(stage, H_BORDER + x, line[H_BORDER + x], line[H_BORDER + x + 1]);
}

/*
 * The CRT-scanline dim. Off, it must not touch a pixel; on, every channel drops one
 * stop and none of them bleeds - not into the channel below it, and not into the next
 * pixel's top bit, which is the whole reason the transform carries a mask.
 *
 * Reconstructed a channel at a time, so a wrong PICO9918_PIXEL_PAIR_DIM cannot agree
 * with itself.
 */
static uint16_t dimmed(uint16_t src)
{
  return (uint16_t)(((src & 0x00f) >> 1) | (((src & 0x0f0) >> 1) & 0x070) |
                    (((src & 0xf00) >> 1) & 0x700));
}

/* The setting and the output-line parity gate independently. An odd line at scale 2 is a
   dim with no render behind it, so the maths is checkable against a seeded buffer. */
static void checkDimMaths(void)
{
  static const uint16_t sample[8] = {0x0000, 0x0fff, 0x0111, 0x0f0f, 0x0777, 0x0888,
                                     0x0001, 0x0f00};

  pico9918_v_scale = 2;
  for (int on = 0; on < 2; ++on)
  {
    tms9918->config[PICO9918_CONF_CRT_SCANLINES] = (uint8_t)on;

    for (uint32_t out = 1; out < 4; out += 2)
    {
      for (int i = 0; i < 8; ++i) line[i] = sample[i];

      const bool changed = pico9918_frame_output_line(PICO9918_INST out, &params, line);
      if (changed != (on != 0)) fail("dim-return", out, (unsigned)(on != 0), changed);

      for (int i = 0; i < 8; ++i)
      {
        const uint16_t wanted = on ? dimmed(sample[i]) : sample[i];
        if (line[i] != wanted)
          fail(on ? "dim-on" : "dim-off", out * 8u + (unsigned)i, wanted, line[i]);
      }
    }
  }
  tms9918->config[PICO9918_CONF_CRT_SCANLINES] = 0;
}

/* Double rows: vPixelScale is 1, nothing repeats, and a rule keyed on the repeat index
   dims nothing at all. Render each line with the setting off and again with it on - the
   odd ones must come back a stop down, the even ones untouched. */
static void checkDimScale1(void)
{
  static PICO9918_PIXEL_T off[H_VIRTUAL];
  const uint32_t          base = (activeLine() + 1u) & ~1u;

  pico9918_v_scale = 1;
  for (uint32_t out = base; out < base + 4; ++out)
  {
    tms9918->config[PICO9918_CONF_CRT_SCANLINES] = 0;
    pico9918_frame_output_line(PICO9918_INST out, &params, line);
    memcpy(off, line, sizeof(off));

    tms9918->config[PICO9918_CONF_CRT_SCANLINES] = 1;
    pico9918_frame_output_line(PICO9918_INST out, &params, line);

    for (uint32_t i = 0; i < H_VIRTUAL; ++i)
    {
      const uint16_t wanted = (out & 1) ? dimmed(off[i]) : off[i];
      if (line[i] != wanted)
      {
        fail((out & 1) ? "scale1-dim" : "scale1-keep", out * 1000u + i, wanted, line[i]);
        break;
      }
    }
  }
  tms9918->config[PICO9918_CONF_CRT_SCANLINES] = 0;
  pico9918_v_scale                             = 2;
}

int main(void)
{
  const int tier = PICO9918_BUILD_TEXT80_8BPP;

  pico9918_init();
  distinctPalette();

  /* 1. locked 80 columns is the 4bpp line, tier or no tier */
  text80();
  renderLines(0, 120);
  checkLineBytes("locked-t80", TMS9918_PIXELS_X);
  checkPackedLayout("locked-t80");

  /* 2. unlocking does not change the MODE, so on a build with the tier nothing but
        the unlock itself can tell the LUT that its layout has moved */
  unlock();
  renderOneActiveLine();
  checkLineBytes("unlocked-t80", tier ? TMS9918_PIXELS_X * 2u : TMS9918_PIXELS_X);
  if (tier)
  {
    checkDoubledLayout("unlock-mid-frame");
    checkGeometry("t80-wide-geometry", 0);
  }
  else
  {
    checkPackedLayout("unlocked-t80-4bpp");
  }

  /* 3. and a mode that doubles, which is every other one */
  regWrite(0, 0x00); /* graphics I, still unlocked */
  regWrite(1, 0xc0);
  renderLines(0, 120);
  checkLineBytes("graphics-i", TMS9918_PIXELS_X);
  checkDoubledLayout("graphics-i");
  checkGeometry("graphics-i-geometry", 1);

  /* 4. and the CRT-scanline dim a host applies to the repeat of each line */
  checkDimMaths();
  checkDimScale1();

  printf("%s: post-palette pixel path, %s 8bpp tier, %d failure(s)\n", failures ? "FAIL" : "PASS",
         tier ? "with" : "without", failures);
  return failures != 0;
}
