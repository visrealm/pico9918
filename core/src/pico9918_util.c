/**
 * \file
 * \brief pico9918-core - utility / helper functions
 *
 * Copyright (c) 2022 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 */

#include "pico9918_util.h"

#ifndef WIN32
#undef PICO9918_DLLEXPORT
#define PICO9918_DLLEXPORT
#endif

#undef PICO9918_DLLEXPORT_CONST
#define PICO9918_DLLEXPORT_CONST


/** \brief the sprite Y value that terminates the sprite list */
#define LAST_SPRITE_YPOS 0xD0

/** \brief the sixteen TMS9918 colours, each packed as 0xrrggbbaa */
PICO9918_DLLEXPORT_CONST uint32_t pico9918_palette[] = {
  0x00000000, /* transparent */
  0x000000ff, /* black */
  0x21c942ff, /* medium green */
  0x5edc78ff, /* light green */
  0x5455edff, /* dark blue */
  0x7d75fcff, /* light blue */
  0xd3524dff, /* dark red */
  0x43ebf6ff, /* cyan */
  0xfd5554ff, /* medium red */
  0xff7978ff, /* light red */
  0xd3c153ff, /* dark yellow */
  0xe5ce80ff, /* light yellow */
  0x21b03cff, /* dark green */
  0xc95bbaff, /* magenta */
  0xccccccff, /* grey */
  0xffffffff  /* white */
};

/** \brief zero the 16KB of VRAM, then park all 32 sprites past the end of the list */
static void clearTmsRam(PICO9918_INST_ONLY_ARG)
{
  pico9918_set_address_write(PICO9918_INST 0x0000);
  pico9918_write_byte_rpt(PICO9918_INST 0x00, 0x4000);

  pico9918_set_address_write(PICO9918_INST TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS);
  for (int i = 0; i < 32; ++i)
  {
    pico9918_write_data(PICO9918_INST LAST_SPRITE_YPOS);
    pico9918_write_data(PICO9918_INST 0);
    pico9918_write_data(PICO9918_INST 0);
    pico9918_write_data(PICO9918_INST 0);
  }
}

/** \brief program Graphics I, the default table addresses and black on cyan, then clear VRAM */
PICO9918_DLLEXPORT
void pico9918_initialise_gfx_i(PICO9918_INST_ONLY_ARG)
{
  pico9918_write_register_value(PICO9918_INST TMS_REG_0, TMS_R0_EXT_VDP_DISABLE | TMS_R0_MODE_GRAPHICS_I);
  pico9918_write_register_value(PICO9918_INST TMS_REG_1, TMS_R1_RAM_16K | TMS_R1_MODE_GRAPHICS_I |
                                                          TMS_R1_DISP_ACTIVE | TMS_R1_INT_ENABLE);
  pico9918_set_name_table_addr(PICO9918_INST TMS_DEFAULT_VRAM_NAME_ADDRESS);
  pico9918_set_color_table_addr(PICO9918_INST TMS_DEFAULT_VRAM_COLOR_ADDRESS);
  pico9918_set_pattern_table_addr(PICO9918_INST TMS_DEFAULT_VRAM_PATT_ADDRESS);
  pico9918_set_sprite_attr_table_addr(PICO9918_INST TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS);
  pico9918_set_sprite_patt_table_addr(PICO9918_INST TMS_DEFAULT_VRAM_SPRITE_PATT_ADDRESS);
  pico9918_set_fg_bg_color(PICO9918_INST TMS_BLACK, TMS_CYAN);

  clearTmsRam(PICO9918_INST_ONLY);
}


/** \brief as pico9918_initialise_gfx_i(), but for Graphics II and with the name table
 *         filled with a rising pattern index
 */
PICO9918_DLLEXPORT
void pico9918_initialise_gfx_ii(PICO9918_INST_ONLY_ARG)
{
  pico9918_write_register_value(PICO9918_INST TMS_REG_0, TMS_R0_EXT_VDP_DISABLE | TMS_R0_MODE_GRAPHICS_II);
  pico9918_write_register_value(PICO9918_INST TMS_REG_1, TMS_R1_RAM_16K | TMS_R1_MODE_GRAPHICS_II |
                                                          TMS_R1_DISP_ACTIVE | TMS_R1_INT_ENABLE);
  pico9918_set_name_table_addr(PICO9918_INST TMS_DEFAULT_VRAM_NAME_ADDRESS);

  /* in Graphics II registers 3 and 4 pick an 8KB half rather than an address:
     reg3 0x7f/0xff and reg4 0x03/0x07 mean 0x0000/0x2000 */

  pico9918_write_register_value(PICO9918_INST TMS_REG_COLOR_TABLE, 0x7f);
  pico9918_write_register_value(PICO9918_INST TMS_REG_PATTERN_TABLE, 0x07);

  pico9918_set_sprite_attr_table_addr(PICO9918_INST TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS);
  pico9918_set_sprite_patt_table_addr(PICO9918_INST TMS_DEFAULT_VRAM_SPRITE_PATT_ADDRESS);
  pico9918_set_fg_bg_color(PICO9918_INST TMS_BLACK, TMS_CYAN);

  clearTmsRam(PICO9918_INST_ONLY);

  pico9918_set_address_write(PICO9918_INST TMS_DEFAULT_VRAM_NAME_ADDRESS);
  for (int i = 0; i < 768; ++i)
  {
    pico9918_write_data(PICO9918_INST i & 0xff);
  }
}
