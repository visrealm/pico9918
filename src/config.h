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

// 16-bit packed major/minor/patch, used for tracking which release introduced
// each settable config field (see configFields[] in config.c).
// Layout: 4 bits major (0-15), 4 bits minor (0-15), 8 bits patch (0-255).
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

  // Pending-display state (read-only mirror of pending flash block).
  // Configurator reads these via VDP_REG(58) to detect ARMED state.
  CONF_PENDING_STATE        = 200,
  CONF_PENDING_DRIVER_PREF  = 201,
  CONF_PENDING_VGA_MODE     = 202,
  CONF_PENDING_SCART_MODE   = 203,
  CONF_PENDING_CLOCK_PRESET = 204,

  // Configurator -> firmware command bytes (write 1 to trigger).
  CONF_SAVE_FORCED      = 252,    // factory reset path: write whole config to main + erase pending
  CONF_PENDING_CANCEL   = 253,    // erase pending block; banner clears
  CONF_PENDING_CONFIRM  = 254,    // copy pending->main; erase pending; banner clears
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

/* true if the boot-time clock should configure for SCART (270 MHz). Peeks
 * both the pending-display block and the main config block in flash so a
 * forced-SCART preference (whether confirmed or pending) works without a
 * dongle. Must be called after detectScartDongle(). */
bool shouldUseScartClock();

/* read configuration data from flash */
void readConfig(uint8_t config[CONFIG_BYTES]);

/* write configuration data to flash */
bool writeConfig(uint8_t config[CONFIG_BYTES]);

/* save dispatcher: routes display-related field changes to the pending block
 * (state = PENDING) and untracked changes to the main config. Called by the
 * gpu.c handler when the configurator triggers CONF_SAVE_TO_FLASH = 1. */
bool saveConfigSplitPending(uint8_t config[CONFIG_BYTES]);

/* apply configuration data to vdp instance */
void applyConfig();

// -----------------------------------------------------------------------------
// Pending-display confirmation (separate 4 KB flash block).
//
// Tracked fields (driver, VGA mode, SCART mode, clock preset) only become
// permanent once the user has visually confirmed the change in the configurator
// after a reboot. If they reboot without confirming, last-confirmed values are
// restored from the main config block.
//
// State machine: CONFIRMED -> PENDING (configurator save) -> ARMED (firmware
// boot) -> CONFIRMED (user accepts in configurator) | CONFIRMED (next reboot
// without acceptance reverts).
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

/* read the pending block; treats erased/invalid contents as CONFIRMED. */
void readPendingDisplay(PendingDisplay *p);

/* write the pending block (erases the 4 KB sector). returns true on success. */
bool writePendingDisplay(const PendingDisplay *p);

/* erase the pending block (synonym for writing CONFIRMED state via erase). */
bool erasePendingDisplay();

/* called early at boot, after readConfig(). If the pending block is PENDING,
 * overlays pending values onto config[] and bumps state to ARMED in flash so
 * a subsequent reboot without confirmation reverts. If ARMED, erases the
 * pending block and leaves config[] untouched (revert path). Updates the
 * internal banner flag (see displayChangePending). */
void applyPendingDisplay(uint8_t config[CONFIG_BYTES]);

/* true while the firmware is running with pending (unconfirmed) display
 * settings; drives the OSD banner. */
bool displayChangePending();