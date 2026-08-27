/**
 * \file
 * \brief persisted configuration: flash layout, defaults and the display-change confirmation flow
 *
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** \brief running version, packed major(4) | minor(4) */
#define PICO9918_SW_VERSION ((PICO9918_MAJOR_VER << 4) | PICO9918_MINOR_VER)

/** \brief running version, packed major(4) | minor(4) | patch(8) as configFields[].introducedIn stores it */
#define PICO9918_SW_VERSION_FULL \
  (((uint16_t)PICO9918_MAJOR_VER << 12) | ((uint16_t)PICO9918_MINOR_VER << 8) | ((uint16_t)PICO9918_PATCH_VER))

/* The config byte layout (the PICO9918_CONF_* indices and CONFIG_BYTES) is owned by the
   library, which is what makes byte 15 - the render base - a single
   declaration rather than two that must agree. See
   core/src/pico9918_config.h */
#include "pico9918_config.h"

/** \brief board revision, as stored in PICO9918_CONF_HW_VERSION */
typedef enum
{
  HWVer_0_3 = 0x03,
  HWVer_1_x = 0x10,
  HWVer_2_x = 0x20,
} Pico9918HardwareVersion;

/** \brief which host clocks to drive, as stored in PICO9918_CONF_VDP_DEVICE */
typedef enum
{
  VDP_TMS9918A = 0, ///< GROMCLK + CPUCLK (default)
  VDP_TMS992xA = 1, ///< GROMCLK only
  VDP_TMS9118  = 2, ///< CPUCLK only (on pin 38)
  VDP_TMS912x  = 3, ///< CPUCLK on GROMCLK pin (pin 37)
  VDP_DEVICE_COUNT
} VdpDevice;

/** \brief cached board revision; the first call detects it, driving GPIO_RESET */
Pico9918HardwareVersion currentHwVersion(void);

/** \brief probe for a SCART dongle, which bridges hsync (GPIO 0) and vsync (GPIO 1)
 *  \note  must run before vgaInit() claims those pins
 */
bool detectScartDongle(void);

/** \brief true if a SCART dongle was detected at boot */
bool isScartConnected(void);

/** \brief derive PICO9918_CONF_DISP_DRIVER (0=VGA, 1=NTSC, 2=PAL) from PICO9918_CONF_DISP_DRIVER_PREF
 *         (0=AUTO, 1=VGA, 2=SCART) and dongle detection
 */
void updateDispDriver(void);

/** \brief true if the boot clock should be the SCART 270 MHz preset
 *  \note  call after detectScartDongle() and before readConfig(); it reads flash
 *         directly, and a pending display change outranks the stored config
 */
bool shouldUseScartClock(void);

/** \brief read the configuration from flash, resetting to defaults if it fails
 *         validation and defaulting fields introduced since the stored version
 */
void readConfig(uint8_t config[CONFIG_BYTES]);

/** \brief erase and rewrite the whole config sector, verifying and retrying */
bool writeConfig(uint8_t config[CONFIG_BYTES]);

/** \brief PICO9918_CONF_SAVE_TO_FLASH handler: changed display fields go to the pending block,
 *         the main config keeps its last-confirmed values
 */
bool saveConfigSplitPending(uint8_t config[CONFIG_BYTES]);

/** \brief the VGA-side half of "config applied", which the library cannot own
 *  \note  registered with pico9918_config_set_applied_callback(), so it fires from inside
 *         the library's apply; never called directly by the firmware
 */
void applyConfigHostEffects(void);

/** \brief re-read the stored block once the display is finally enabled after the
 *         startup diagnostics screen
 *  \note  registered with pico9918_frame_set_config_reload_callback(); it is flash I/O, so
 *         it cannot expand into a library TU
 */
void reloadStoredConfig(void);

/** \brief display-change confirmation state, held in a 4 KB flash block of its own
 *  CONFIRMED -> PENDING (save) -> ARMED (boot) -> CONFIRMED (accepted, or reverted on reboot)
 */
#define PENDING_STATE_CONFIRMED 0xC0
#define PENDING_STATE_PENDING   0x9E
#define PENDING_STATE_ARMED     0xA0

/** \brief the 16-byte pending flash slot; reserved[] must be zero */
typedef struct
{
  uint8_t state; ///< PENDING_STATE_*
  uint8_t dispDriverPref;
  uint8_t vgaMode;
  uint8_t scartMode;
  uint8_t clockPresetId;
  uint8_t reserved[11];
} PendingDisplay;

_Static_assert(sizeof(PendingDisplay) == 16, "PendingDisplay must match the 16-byte flash slot - adjust reserved[]");

/** \brief read the pending block; an erased or unrecognised state reads as CONFIRMED */
void readPendingDisplay(PendingDisplay* p);

/** \brief erase and rewrite the pending block, verifying and retrying */
bool writePendingDisplay(const PendingDisplay* p);

/** \brief erase the pending block and clear the banner */
bool erasePendingDisplay(void);

/** \brief advance the pending state machine, after readConfig(): PENDING applies to
 *         \p config and becomes ARMED, ARMED reverts and erases
 */
void applyPendingDisplay(uint8_t config[CONFIG_BYTES]);

#define PENDING_BANNER_NONE     0 ///< no banner
#define PENDING_BANNER_AWAIT_PC 1 ///< saved pending; awaiting a power cycle to test it
#define PENDING_BANNER_AWAIT_OK 2 ///< booted with pending; awaiting confirmation in the configurator
/** \brief current OSD banner state, one of PENDING_BANNER_* */
uint8_t pendingDisplayBanner(void);
