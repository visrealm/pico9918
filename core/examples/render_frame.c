/*
 * pico9918-core - render one frame to a PPM
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * \example render_frame.c
 *
 * The whole library in one file: set a mode up, write the three tables a
 * TMS9918A draws Graphics I from, then ask for each scanline in turn and colour
 * the indices it hands back.
 *
 * The shape here is the shape a display driver has. `pico9918_scan_line` renders
 * one line into an internal buffer and returns the status byte a host would have
 * read; `pico9918_line_source` and `pico9918_line_bytes` are how you get at what
 * it drew. Nothing allocates per line, and nothing is drawn until you ask - so a
 * driver calls this from its own line interrupt and a program like this one just
 * loops.
 *
 * Build it against an installed package:
 *
 *     cmake -S examples -B build-examples
 *     cmake --build build-examples
 *     ./build-examples/render_frame frame.ppm
 */

#include "pico9918.h"
#include "pico9918_util.h"

#include <stdio.h>
#include <string.h>

/* A TMS9918A's active display. TMS9918_PIXELS_Y is the buffer the F18A's taller
 * modes need, not what Graphics I draws. */
#define ROWS   192
#define COLS   TMS9918_PIXELS_X

/* Graphics I reads its tables from wherever the registers point. These are the
 * addresses pico9918_initialise_gfx_i programs, and they are what the writes
 * below assume. */
#define PATTERNS  TMS_DEFAULT_VRAM_PATT_ADDRESS
#define NAMES     TMS_DEFAULT_VRAM_NAME_ADDRESS
#define COLORS    TMS_DEFAULT_VRAM_COLOR_ADDRESS

/* One 8x8 glyph: a filled square with a hollow centre, so a frame that renders
 * correctly is obvious at a glance and a frame that does not is obviously wrong. */
static const uint8_t kTile[8] = {
  0xFF, 0x81, 0xBD, 0xA5, 0xA5, 0xBD, 0x81, 0xFF
};

/* PICO9918_INST_ARG in the signature and PICO9918_INST at the call is how a helper
 * compiles under either instance mode: both expand to nothing when the library was
 * built PICO9918_SINGLE_INSTANCE=1. */
static void writeVram(PICO9918_INST_ARG uint16_t addr, const uint8_t* data, size_t len)
{
  pico9918_write_addr(PICO9918_INST addr & 0xFF);
  pico9918_write_addr(PICO9918_INST 0x40 | (addr >> 8));
  pico9918_write_bytes(PICO9918_INST data, len);
}

static int writePpm(const char* path, const uint8_t* rgb)
{
  FILE* f = fopen(path, "wb");
  if (!f)
  {
    perror(path);
    return 1;
  }
  fprintf(f, "P6\n%d %d\n255\n", COLS, ROWS);
  fwrite(rgb, 3, (size_t)COLS * ROWS, f);
  fclose(f);
  return 0;
}

int main(int argc, char** argv)
{
  const char* out = argc > 1 ? argv[1] : "frame.ppm";
  static uint8_t rgb[ROWS * COLS * 3];
  uint8_t tiles[8 * 8];

#if PICO9918_SINGLE_INSTANCE
  pico9918_init();
#else
  pico9918_t* tms9918 = pico9918_new();
  if (!tms9918)
  {
    fprintf(stderr, "pico9918_new failed\n");
    return 1;
  }
#endif

  pico9918_reset(PICO9918_INST_ONLY);
  pico9918_initialise_gfx_i(PICO9918_INST_ONLY);

  /* After initialise_gfx_i, which clears VRAM last of all - so these writes go
   * after it, not before. Pattern 1 is the glyph; pattern 0 stays blank. */
  writeVram(PICO9918_INST PATTERNS + 8, kTile, sizeof kTile);

  /* Graphics I gives one colour byte per eight patterns, so this colours the
   * whole first group at once: white on dark blue. */
  memset(tiles, pico9918_fg_bg_color(TMS_WHITE, TMS_DK_BLUE), 32);
  writeVram(PICO9918_INST COLORS, tiles, 32);

  /* A checkerboard of it, so both the glyph and the backdrop are on screen. */
  for (int row = 0; row < ROWS / 8; ++row)
  {
    uint8_t names[COLS / 8];
    for (int col = 0; col < COLS / 8; ++col)
    {
      names[col] = (uint8_t)((row + col) & 1);
    }
    writeVram(PICO9918_INST(uint16_t)(NAMES + row * (COLS / 8)), names, sizeof names);
  }

  pico9918_write_register_value(PICO9918_INST TMS_REG_1,
                                TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE);

  for (uint16_t y = 0; y < ROWS; ++y)
  {
    pico9918_scan_line(PICO9918_INST y);

    const uint8_t* line = pico9918_line_source(PICO9918_INST_ONLY);
    uint8_t* px = rgb + (size_t)y * COLS * 3;
    for (int x = 0; x < COLS; ++x)
    {
      /* 0x0rgb, four bits a channel, so each one is scaled by 17 to fill a byte */
      const uint16_t argb = pico9918_default_palette(line[x] & 0x0F);
      *px++ = (uint8_t)(((argb >> 8) & 0xF) * 17);
      *px++ = (uint8_t)(((argb >> 4) & 0xF) * 17);
      *px++ = (uint8_t)(((argb) & 0xF) * 17);
    }
  }

  printf("%s: %dx%d, mode %d\n", out, COLS, ROWS,
         (int)pico9918_display_mode(PICO9918_INST_ONLY));

#if !PICO9918_SINGLE_INSTANCE
  pico9918_destroy(PICO9918_INST_ONLY);
#endif
  return writePpm(out, rgb);
}
