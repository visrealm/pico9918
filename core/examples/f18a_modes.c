/*
 * pico9918-core - turn the F18A on
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * \example f18a_modes.c
 *
 * The F18A boots as a TMS9918A and stays one until a program unlocks it, so this draws
 * the same name table twice: once locked, then again after the unlock, with four
 * enhanced features turned on. Every cell holds the same tile in both frames - all the
 * difference is in the registers.
 *
 *     R49 bits 5:4   enhanced colour mode 2: two bitplanes, four colours a cell
 *     R49 bit 7      the second tile layer, composited over the first
 *     R50 bit 1      attributes by screen position instead of by tile name
 *     R29 bits 3:2   how far apart the bitplanes sit (512 bytes here)
 *     R10, R11       layer 2's name and colour tables
 *     R24, R25       layer 2's horizontal and vertical scroll
 *     R27, R28       layer 1's
 *
 * Above ECM0 a colour byte stops being an fg/bg pair and becomes an attribute:
 *
 *     bit 7 priority over sprites   bit 6 flip X   bit 5 flip Y
 *     bit 4 transparent             bits 3:0 sub-palette
 *
 * The sub-palette supplies the high bits of the palette index and the pixel's bitplanes
 * the low ones, so at ECM2 each sub-palette is four of the F18A's 64 colours. Position
 * attributes are what let one repeated tile take a different four in every cell.
 *
 * Requires a library built PICO9918_MODE=1:
 *
 *     cmake -S examples -B build-examples -DPICO9918_MODE=1
 *     cmake --build build-examples
 *     ./build-examples/f18a_modes locked.ppm f18a.ppm
 */

#include "pico9918.h"
#include "pico9918_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROWS    192
#define COLS    TMS9918_PIXELS_X
#define CELLS_X (COLS / 8)
#define CELLS_Y (ROWS / 8)
#define CELLS   (CELLS_X * CELLS_Y)

/* 16KB, every table on the boundary its register can express: name tables on 1KB with
   all four scroll pages free, colour tables on 64 bytes, patterns on 2KB. */
#define PATT        0x0000
#define PLANE2      0x0200 /* the stride R29 selects */
#define SPRITE_PATT 0x0800
#define SPRITE_ATTR 0x0C00
#define NAME1       0x1000
#define NAME2       0x2000
#define COLOR1      0x3000
#define COLOR2      0x3800

#define VRAM_SIZE 0x4000

#define TILE_FIELD  1 /* layer 1 draws this one everywhere */
#define TILE_STRIPE 2 /* layer 2's ribbon; tile 0 is blank, which is the rest of layer 2 */
#define NUM_TILES   (TILE_STRIPE + 1)

/* The public enum names R0-R7, the registers a TMS9918A has. The rest are by number. */
#define VR_NAME2      0x0A
#define VR_COLOR2     0x0B
#define VR_T2_HSCROLL 0x19
#define VR_T2_VSCROLL 0x1A
#define VR_T1_HSCROLL 0x1B
#define VR_T1_VSCROLL 0x1C
#define VR_ECM_STRIDE 0x1D
#define VR_TILE_MODE  0x31
#define VR_TILE_ATTR  0x32
#define VR_UNLOCK     0x39

static void writeReg(PICO9918_INST_ARG uint8_t reg, uint8_t value)
{
  pico9918_write_register_value(PICO9918_INST(pico9918_register_t) reg, value);
}

static void writeVram(PICO9918_INST_ARG uint16_t addr, const uint8_t* data, size_t len)
{
  pico9918_set_address_write(PICO9918_INST addr);
  pico9918_write_bytes(PICO9918_INST data, len);
}

/* Plane 1 is the pixel's low bit and plane 2 its high one, so a pixel is 0-3 and each
   tile carries four colours of whatever sub-palette its attribute names. TILE_FIELD is
   a bordered block with one highlight, which uses three of the four and leaves the
   fourth - value 0 - nowhere, so no cell is ever skipped as empty. Graphics I reads
   plane 1 alone, which is what the locked frame is a picture of. */
static void buildTiles(uint8_t* plane1, uint8_t* plane2, size_t len)
{
  memset(plane1, 0, len);
  memset(plane2, 0, len);
  for (int y = 0; y < 8; ++y)
  {
    for (int x = 0; x < 8; ++x)
    {
      const int edge      = (x == 0 || x == 7 || y == 0 || y == 7);
      const int highlight = (x >= 1 && x <= 2 && y >= 1 && y <= 2);
      const int field     = edge ? 1 : highlight ? 2 : 3;
      if (field & 1) plane1[TILE_FIELD * 8 + y] |= 0x80 >> x;
      if (field & 2) plane2[TILE_FIELD * 8 + y] |= 0x80 >> x;

      if (x & 4) /* four opaque pixels, four with every plane zero - so transparent */
      {
        plane1[TILE_STRIPE * 8 + y] |= 0x80 >> x;
        plane2[TILE_STRIPE * 8 + y] |= 0x80 >> x;
      }
    }
  }
}

static void renderPpm(PICO9918_INST_ARG const char* path)
{
  static uint8_t rgb[(size_t)COLS * ROWS * 3];
  for (uint16_t y = 0; y < ROWS; ++y)
  {
    pico9918_scan_line(PICO9918_INST y);

    const uint8_t* line = pico9918_line_source(PICO9918_INST_ONLY);
    uint8_t* px         = rgb + (size_t)y * COLS * 3;
    for (int x = 0; x < COLS; ++x)
    {
      /* six bits of index now: the F18A's palette is 64 entries, not 16 */
      const uint16_t argb = pico9918_default_palette(line[x] & 0x3F);
      *px++               = (uint8_t)(((argb >> 8) & 0xF) * 17);
      *px++               = (uint8_t)(((argb >> 4) & 0xF) * 17);
      *px++               = (uint8_t)((argb & 0xF) * 17);
    }
  }

  FILE* f = fopen(path, "wb");
  if (!f)
  {
    perror(path);
    return;
  }
  fprintf(f, "P6\n%d %d\n255\n", COLS, ROWS);
  fwrite(rgb, 3, (size_t)COLS * ROWS, f);
  fclose(f);
  printf("wrote %s\n", path);
}

int main(int argc, char** argv)
{
  const char* lockedOut = argc > 1 ? argv[1] : "locked.ppm";
  const char* f18aOut   = argc > 2 ? argv[2] : "f18a.ppm";

  static uint8_t zeros[VRAM_SIZE];
  uint8_t plane1[NUM_TILES * 8], plane2[NUM_TILES * 8];
  uint8_t cells[CELLS];

#if PICO9918_SINGLE_INSTANCE
  pico9918_init();
#else
  pico9918_t* tms9918 = pico9918_new();
  if (!tms9918) return 1;
#endif
  pico9918_reset(PICO9918_INST_ONLY);

  /* pico9918_reset leaves VRAM alone, as the chip does. */
  writeVram(PICO9918_INST 0, zeros, sizeof zeros);

  buildTiles(plane1, plane2, sizeof plane1);
  writeVram(PICO9918_INST PATT, plane1, sizeof plane1);
  writeVram(PICO9918_INST PLANE2, plane2, sizeof plane2);

  memset(cells, TILE_FIELD, sizeof cells);
  writeVram(PICO9918_INST NAME1, cells, sizeof cells);

  /* A Graphics I colour table, which is one fg/bg pair per eight names. */
  memset(cells, pico9918_fg_bg_color(TMS_CYAN, TMS_DK_BLUE), 32);
  writeVram(PICO9918_INST COLOR1, cells, 32);

  /* 0xD0 as a sprite's Y ends the list, so no sprites are drawn */
  writeVram(PICO9918_INST SPRITE_ATTR, (const uint8_t[]){0xD0}, 1);

  pico9918_set_name_table_addr(PICO9918_INST NAME1);
  pico9918_set_color_table_addr(PICO9918_INST COLOR1);
  pico9918_set_pattern_table_addr(PICO9918_INST PATT);
  pico9918_set_sprite_attr_table_addr(PICO9918_INST SPRITE_ATTR);
  pico9918_set_sprite_patt_table_addr(PICO9918_INST SPRITE_PATT);
  pico9918_set_fg_bg_color(PICO9918_INST TMS_WHITE, TMS_BLACK);
  writeReg(PICO9918_INST TMS_REG_1, TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE);

  /* Written now, while the chip is locked, and dropped: a locked F18A takes R0-R7 and
     nothing else. The two files are the proof - this one renders as plain Graphics I
     with R49 already asking for ECM2 and a second layer. */
  writeReg(PICO9918_INST VR_TILE_MODE, 0xA0);
  renderPpm(PICO9918_INST lockedOut);

  /* 0x1C twice into R57. It is the one register write a locked device honours. */
  writeReg(PICO9918_INST VR_UNLOCK, 0x1C);
  writeReg(PICO9918_INST VR_UNLOCK, 0x1C);

  /* Attributes by position, one per cell, laid out exactly like the name table: diamond
     bands out from the centre. The tile never changes, so every colour here comes from
     this table. Sub-palettes 1-3 are palette entries 4-15, the TMS9918A's own sixteen;
     0 is skipped because its first entry is the transparent one. */
  for (int row = 0; row < CELLS_Y; ++row)
  {
    for (int col = 0; col < CELLS_X; ++col)
    {
      const int ring = (abs(col - CELLS_X / 2) + abs(row - CELLS_Y / 2)) / 2;
      cells[row * CELLS_X + col] = (uint8_t)(1 + ring % 3);
    }
  }
  writeVram(PICO9918_INST COLOR1, cells, sizeof cells);

  /* Layer 2: a four-cell diagonal ribbon, blank everywhere else. Its attribute sets bit
     4, so the tile's zero pixels are transparent and layer 1 shows between the stripes. */
  for (int row = 0; row < CELLS_Y; ++row)
    for (int col = 0; col < CELLS_X; ++col)
      cells[row * CELLS_X + col] = (uint8_t)((((col - row) & 0x1F) < 4) ? TILE_STRIPE : 0);
  writeVram(PICO9918_INST NAME2, cells, sizeof cells);

  /* Transparent, on sub-palette 0 - the one layer 1 does not use, so the ribbon is a
     colour nothing underneath it can be. */
  memset(cells, 0x10, sizeof cells);
  writeVram(PICO9918_INST COLOR2, cells, sizeof cells);

  writeReg(PICO9918_INST VR_NAME2, NAME2 >> 10);
  writeReg(PICO9918_INST VR_COLOR2, COLOR2 >> 6);
  writeReg(PICO9918_INST VR_ECM_STRIDE, 0x88); /* 512 bytes a plane, tiles and sprites */
  writeReg(PICO9918_INST VR_TILE_ATTR, 0x02);  /* attributes by position */
  writeReg(PICO9918_INST VR_TILE_MODE, 0xA0);  /* ECM2 | layer 2 */

  /* A different amount on each axis of each layer, which is where a scroll taken from
     the wrong register shows. The low three bits are the fine offset, the rest cells. */
  writeReg(PICO9918_INST VR_T1_HSCROLL, 3);
  writeReg(PICO9918_INST VR_T1_VSCROLL, 100);
  writeReg(PICO9918_INST VR_T2_HSCROLL, 11);
  writeReg(PICO9918_INST VR_T2_VSCROLL, 37);

  printf("unlocked: R49 reads back 0x%02X\n",
         pico9918_reg_value(PICO9918_INST(pico9918_register_t) VR_TILE_MODE));

  renderPpm(PICO9918_INST f18aOut);

#if !PICO9918_SINGLE_INSTANCE
  pico9918_destroy(PICO9918_INST_ONLY);
#endif
  return 0;
}
