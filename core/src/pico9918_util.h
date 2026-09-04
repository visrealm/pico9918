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

#ifndef _PICO9918_UTIL_H
#define _PICO9918_UTIL_H

#include "pico9918.h"

#include <stddef.h>
#include <string.h>

/** \brief the VRAM table layout pico9918_initialise_gfx_i() and GfxII() program */
#define TMS_DEFAULT_VRAM_NAME_ADDRESS        0x3800 /**< name table (R2) */
#define TMS_DEFAULT_VRAM_COLOR_ADDRESS       0x0000 /**< colour table (R3) */
#define TMS_DEFAULT_VRAM_PATT_ADDRESS        0x2000 /**< pattern table (R4) */
#define TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS 0x3B00 /**< sprite attribute table (R5) */
#define TMS_DEFAULT_VRAM_SPRITE_PATT_ADDRESS 0x1800 /**< sprite pattern table (R6) */

/** \brief the sixteen TMS9918 colours, each packed as 0xrrggbbaa */
PICO9918_DLLEXPORT_CONST uint32_t pico9918_palette[];

/** \brief write a VDP register as a host would, value byte first */
inline static void pico9918_write_register_value(PICO9918_INST_ARG pico9918_register_t reg, uint8_t value)
{
  pico9918_write_addr(PICO9918_INST value);
  pico9918_write_addr(PICO9918_INST 0x80 | (uint8_t)reg);
}

/** \brief point the VRAM address register at \p addr for reading */
inline static void pico9918_set_address_read(PICO9918_INST_ARG uint16_t addr)
{
  pico9918_write_addr(PICO9918_INST addr & 0x00ff);
  pico9918_write_addr(PICO9918_INST((addr & 0xff00) >> 8));
}

/** \brief point the VRAM address register at \p addr for writing, ie. with bit 14 set */
inline static void pico9918_set_address_write(PICO9918_INST_ARG uint16_t addr)
{
  pico9918_set_address_read(PICO9918_INST addr | 0x4000);
}

/** \brief write a block of bytes to VRAM from the current address */
inline static void pico9918_write_bytes(PICO9918_INST_ARG const uint8_t* bytes, size_t numBytes)
{
  for (size_t i = 0; i < numBytes; ++i)
  {
    pico9918_write_data(PICO9918_INST bytes[i]);
  }
}

/** \brief write \p byte to VRAM \p rpt times from the current address */
inline static void pico9918_write_byte_rpt(PICO9918_INST_ARG uint8_t byte, size_t rpt)
{
  for (size_t i = 0; i < rpt; ++i)
  {
    pico9918_write_data(PICO9918_INST byte);
  }
}


/** \brief write a string to VRAM, its terminator excluded */
inline static void pico9918_write_string(PICO9918_INST_ARG const char* str)
{
  size_t len = strlen(str);
  for (size_t i = 0; i < len; ++i)
  {
    pico9918_write_data(PICO9918_INST str[i]);
  }
}

/** \brief write a string to VRAM with \p offset added to each character's pattern index */
inline static void pico9918_write_string_offset(PICO9918_INST_ARG const char* str, uint8_t offset)
{
  size_t len = strlen(str);
  for (size_t i = 0; i < len; ++i)
  {
    pico9918_write_data(PICO9918_INST str[i] + offset);
  }
}

/** \brief pack \p fg into the high nibble and \p bg into the low nibble of one colour byte */
inline static uint8_t pico9918_fg_bg_color(pico9918_color_t fg, pico9918_color_t bg)
{
  return (uint8_t)((uint8_t)fg << 4) | (uint8_t)bg;
}

/** \brief set the name table address, which register 2 holds in 1KB units */
inline static void pico9918_set_name_table_addr(PICO9918_INST_ARG uint16_t addr)
{
  pico9918_write_register_value(PICO9918_INST TMS_REG_NAME_TABLE, addr >> 10);
}

/** \brief set the colour table address, which register 3 holds in 64-byte units */
inline static void pico9918_set_color_table_addr(PICO9918_INST_ARG uint16_t addr)
{
  pico9918_write_register_value(PICO9918_INST TMS_REG_COLOR_TABLE, (uint8_t)(addr >> 6));
}

/** \brief set the pattern table address, which register 4 holds in 2KB units */
inline static void pico9918_set_pattern_table_addr(PICO9918_INST_ARG uint16_t addr)
{
  pico9918_write_register_value(PICO9918_INST TMS_REG_PATTERN_TABLE, addr >> 11);
}

/** \brief set the sprite attribute table address, which register 5 holds in 128-byte units */
inline static void pico9918_set_sprite_attr_table_addr(PICO9918_INST_ARG uint16_t addr)
{
  pico9918_write_register_value(PICO9918_INST TMS_REG_SPRITE_ATTR_TABLE, (uint8_t)(addr >> 7));
}

/** \brief set the sprite pattern table address, which register 6 holds in 2KB units */
inline static void pico9918_set_sprite_patt_table_addr(PICO9918_INST_ARG uint16_t addr)
{
  pico9918_write_register_value(PICO9918_INST TMS_REG_SPRITE_PATT_TABLE, addr >> 11);
}

/** \brief set register 7, the text-mode foreground colour and the backdrop */
inline static void pico9918_set_fg_bg_color(PICO9918_INST_ARG pico9918_color_t fg, pico9918_color_t bg)
{
  pico9918_write_register_value(PICO9918_INST TMS_REG_FG_BG_COLOR, pico9918_fg_bg_color(fg, bg));
}


/** \brief set up Graphics I with the default table addresses and a cleared VRAM */
PICO9918_DLLEXPORT
void pico9918_initialise_gfx_i(PICO9918_INST_ONLY_ARG);

/** \brief set up Graphics II with the default table addresses and a cleared VRAM */
PICO9918_DLLEXPORT
void pico9918_initialise_gfx_ii(PICO9918_INST_ONLY_ARG);

#endif // _PICO9918_UTIL_H
