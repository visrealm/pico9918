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

// packed major(4) | minor(4) | patch(8) for configFields[].introducedIn
#define PICO9918_SW_VERSION_FULL                              \
  (((uint16_t)PICO9918_MAJOR_VER << 12) |                     \
   ((uint16_t)PICO9918_MINOR_VER <<  8) |                     \
   ((uint16_t)PICO9918_PATCH_VER))

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
  CONF_SCART_MODE       = 11,  // 0 = PAL 576i (default), 1 = NTSC 480i
  CONF_VDP_DEVICE       = 12,
  CONF_DISP_DRIVER_PREF = 13,  // 0 = AUTO (detect dongle), 1 = force VGA, 2 = force SCART
  CONF_VGA_MODE         = 14,  // 0 = 480p60 (extensible)

  CONF_DIAG             = 16,
  CONF_DIAG_REGISTERS   = 17,
  CONF_DIAG_PERFORMANCE = 18,
  CONF_DIAG_PALETTE     = 19,
  CONF_DIAG_ADDRESS     = 20,

  CONF_PALETTE_IDX_0    = 128,
  CONF_PALETTE_IDX_15   = CONF_PALETTE_IDX_0 + 32, // 16x 2 bytes

  // pending-block mirror (read by configurator)
  CONF_PENDING_STATE        = 200,
  CONF_PENDING_DRIVER_PREF  = 201,
  CONF_PENDING_VGA_MODE     = 202,
  CONF_PENDING_SCART_MODE   = 203,
  CONF_PENDING_CLOCK_PRESET = 204,

  // commands (configurator writes 1 to trigger)
  CONF_SAVE_FORCED      = 252,
  CONF_PENDING_CANCEL   = 253,
  CONF_PENDING_CONFIRM  = 254,
  CONF_SAVE_TO_FLASH    = 255,
} Pico9918Options;

typedef enum
{
  HWVer_0_3 = 0x03,
  HWVer_1_x = 0x10,
  HWVer_2_x = 0x20,
} Pico9918HardwareVersion;

typedef enum
{
  VDP_TMS9918A = 0,  // GROMCLK + CPUCLK (default)
  VDP_TMS992xA = 1,  // GROMCLK only
  VDP_TMS9118  = 2,  // CPUCLK only (on pin 38)
  VDP_TMS912x  = 3,  // CPUCLK on GROMCLK pin (pin 37)
  VDP_DEVICE_COUNT
} VdpDevice;

#define CONFIG_BYTES 256

/* get hardware version (v0.3 or v0.4/v1.0+) */
Pico9918HardwareVersion currentHwVersion();

/* detect SCART dongle by testing if sync pins are bridged (call before vgaInit) */
bool detectScartDongle();

/* true if a SCART dongle was detected at boot */
bool isScartConnected();

/* update CONF_DISP_DRIVER at runtime based on SCART detection */
void updateDispDriver();

/* true if boot-time clock should be SCART (270 MHz). Call after detectScartDongle(). */
bool shouldUseScartClock();

/* read configuration data from flash */
void readConfig(uint8_t config[CONFIG_BYTES]);

/* write configuration data to flash */
bool writeConfig(uint8_t config[CONFIG_BYTES]);

/* split save: tracked display fields -> pending block, others -> main config */
bool saveConfigSplitPending(uint8_t config[CONFIG_BYTES]);

/* apply configuration data to vdp instance */
void applyConfig();

// -----------------------------------------------------------------------------
// Display-change confirmation (separate 4 KB flash block).
//
// State: CONFIRMED -> PENDING (save) -> ARMED (boot) -> CONFIRMED (user accepts)
//                                                     | CONFIRMED (reboot reverts)
// -----------------------------------------------------------------------------

#define PENDING_STATE_CONFIRMED 0xC0
#define PENDING_STATE_PENDING   0x9E
#define PENDING_STATE_ARMED     0xA0

typedef struct
{
  uint8_t state;            // PENDING_STATE_*
  uint8_t dispDriverPref;
  uint8_t vgaMode;
  uint8_t scartMode;
  uint8_t clockPresetId;
} PendingDisplay;

void readPendingDisplay(PendingDisplay *p);
bool writePendingDisplay(const PendingDisplay *p);
bool erasePendingDisplay();

/* PENDING -> apply to config + advance to ARMED. ARMED -> revert + erase. */
void applyPendingDisplay(uint8_t config[CONFIG_BYTES]);

/* OSD banner state:
 *   0 = none
 *   1 = saved-pending (awaiting power cycle to test)
 *   2 = armed (booted with pending; awaiting confirmation in configurator) */
#define PENDING_BANNER_NONE      0
#define PENDING_BANNER_AWAIT_PC  1
#define PENDING_BANNER_AWAIT_OK  2
uint8_t pendingDisplayBanner();

/* copy live tracked fields into the in-RAM mirror with the given state */
void refreshPendingMirror(uint8_t config[CONFIG_BYTES], uint8_t state);