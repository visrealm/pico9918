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

#include <stdbool.h>
#include <stdint.h>

#define PICO9918_SW_VERSION ((PICO9918_MAJOR_VER << 4) | PICO9918_MINOR_VER)

typedef enum 
{
  // not settable via registers
  CONF_PICO_MODEL       = 0,
  CONF_HW_VERSION       = 1,
  CONF_SW_VERSION       = 2,
  CONF_SW_PATCH_VERSION = 3,
  CONF_CLOCK_TESTED     = 4,
  CONF_DISP_DRIVER      = 5,
  CONF_FLASH_STATUS     = 6,

  // settable via registers
  CONF_CRT_SCANLINES    = 8,
  CONF_SCANLINE_SPRITES = 9,
  CONF_CLOCK_PRESET_ID  = 10,

  CONF_DIAG             = 64,
  CONF_DIAG_REGISTERS   = 65,
  CONF_DIAG_PERFORMANCE = 66,
  CONF_DIAG_TEMPERATURE = 67,
  CONF_DIAG_PALETTE     = 68,

  CONF_PALETTE_IDX_0    = 128,
  CONF_PALETTE_IDX_15   = CONF_PALETTE_IDX_0 + 32, // 16x 2 bytes

  CONF_SAVE_TO_FLASH    = 255,
} Pico9918Options;

typedef enum
{
  HWVer_0_3 = 0x03,
  HWVer_1_x = 0x10,
} Pico9918HardwareVersion;

#define CONFIG_BYTES 256

/* get hardware version (v0.3 or v0.4/v1.0+) */
Pico9918HardwareVersion currentHwVersion();

/* read configuration data from flash */
void readConfig(uint8_t config[CONFIG_BYTES]);

/* write configuration data to flash */
bool writeConfig(uint8_t config[CONFIG_BYTES]);

/* apply configuration data to vdp instance */
void applyConfig();