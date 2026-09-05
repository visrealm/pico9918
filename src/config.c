/**
 * \file
 * \brief flash-backed configuration storage and the display-change confirmation flow
 *
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

#include "impl/pico9918_priv.h"

#include "gpio.h"
#include "vga.h"
#include "config.h"

#include "hardware/flash.h"
#include "hardware/gpio.h"

#include "pico/time.h"

#include "xip.h"

#include <string.h>

#if PICO_RP2040
#define PICO_MODEL 1
#elif PICO_RP2350
#define PICO_MODEL 2
#endif

/* VdpDevice is host policy (pin behaviour), so the library's field table carries
   only byte 12's range as literals. Keep the two in step. */
_Static_assert(VDP_DEVICE_COUNT - 1 == 3,
               "VdpDevice range changed - update PICO9918_CONF_VDP_DEVICE in pico9918_config_fields[]");
_Static_assert(VDP_TMS9918A == 0,
               "VdpDevice default changed - update PICO9918_CONF_VDP_DEVICE in pico9918_config_fields[]");

static Pico9918HardwareVersion hwVersion = HWVer_1_x;
static bool hwVersionDetected            = false;

/** \brief detect the hardware version (v0.3 vs v0.4+) */
static Pico9918HardwareVersion detectHardwareVersion(void)
{
  Pico9918HardwareVersion version = HWVer_1_x;

#if PICO_RP2350
  version = HWVer_2_x;
#elif PICO_RP2040
  // check if RESET pin is being driven externally (on v0.4+, it is, on v0.3 it isn't since it's CPUCL)
  gpio_pull_down(GPIO_RESET);
  sleep_ms(1);
  if (!gpio_get(GPIO_RESET)) // following pull... ok
  {
    gpio_pull_up(GPIO_RESET);
    sleep_ms(1);
    if (gpio_get(GPIO_RESET)) // still following pull... must be v0.3
    {
      version = HWVer_0_3;
    }
  }
#endif

  return version;
}

/** \brief cached board revision; the first call detects it, driving GPIO_RESET */
Pico9918HardwareVersion currentHwVersion(void)
{
  if (!hwVersionDetected)
  {
    hwVersion         = detectHardwareVersion();
    hwVersionDetected = true;
  }
  return hwVersion;
}

static bool scartConnected = false;

/** \brief probe for a SCART dongle, which bridges hsync and vsync through 1k */
bool __in_flash_func(detectScartDongle)(void)
{
#if PICO9918_ENABLE_SCART
  const uint syncMask  = 0x03 << VGA_SYNC_PINS_START; // GPIO 0 and 1
  const uint driveMask = 0x01 << VGA_SYNC_PINS_START; // GPIO 0

  gpio_init_mask(syncMask);
  gpio_set_drive_strength(VGA_SYNC_PINS_START, GPIO_DRIVE_STRENGTH_12MA);
  gpio_set_dir_masked(syncMask, driveMask); // GPIO 0 output, GPIO 1 input
  gpio_pull_down(VGA_SYNC_PINS_START + 1);

  gpio_set_mask(driveMask);
  sleep_ms(1);
  scartConnected = gpio_get(VGA_SYNC_PINS_START + 1);

  gpio_clr_mask(driveMask);
  gpio_set_drive_strength(VGA_SYNC_PINS_START, GPIO_DRIVE_STRENGTH_4MA);
  gpio_disable_pulls(VGA_SYNC_PINS_START + 1);
  gpio_set_dir_masked(syncMask, 0); // both inputs
  // PIO will re-claim these pins during vgaInit()
#endif
  return scartConnected;
}

/** \brief true if a SCART dongle was detected at boot */
bool isScartConnected(void)
{
  return scartConnected;
}

/** \brief derive PICO9918_CONF_DISP_DRIVER from PICO9918_CONF_DISP_DRIVER_PREF and dongle detection */
void updateDispDriver(void)
{
  uint8_t pref                      = tms9918->config[PICO9918_CONF_DISP_DRIVER_PREF];
  bool useScart                     = (pref == 2) || (pref == 0 && isScartConnected());
  tms9918->config[PICO9918_CONF_DISP_DRIVER] = useScart ? (2 - tms9918->config[PICO9918_CONF_SCART_MODE]) : 0;
}

/** \brief the VGA-side half of "config applied" - see the header */
void applyConfigHostEffects(pico9918_t* tms9918, void* userdata)
{
  (void)userdata;
  vgaCurrentParams()->scanlines = tms9918->config[PICO9918_CONF_CRT_SCANLINES];
}

/** \brief the frame module's late-config-reload hook - see the header
 *  \note  a wrapper rather than registering readConfig directly, because the hook takes
 *         no argument: the block it reloads into is always the live one, and the
 *         library already knows where that is
 */
void reloadStoredConfig(pico9918_t* tms9918, void* userdata)
{
  (void)userdata;
  readConfig(tms9918->config);
}

#define CONFIG_FLASH_OFFSET (0x200000 - 0x1000) ///< in the top 4kB of a 2MB flash
#define CONFIG_FLASH_ADDR   (uint8_t*)(XIP_BASE + CONFIG_FLASH_OFFSET)

/** \brief the 4 KB sector immediately below the main config block */
#define PENDING_FLASH_OFFSET (CONFIG_FLASH_OFFSET - 0x1000)
#define PENDING_FLASH_ADDR   (uint8_t*)(XIP_BASE + PENDING_FLASH_OFFSET)

static uint8_t pendingBannerState = PENDING_BANNER_NONE;

/** \brief true if the boot clock should be the SCART preset; the pending block wins */
bool __in_flash_func(shouldUseScartClock)(void)
{
  const uint8_t* pendingFlash = PENDING_FLASH_ADDR;
  uint8_t pendingState        = pendingFlash[0];
  if (pendingState == PENDING_STATE_PENDING || pendingState == PENDING_STATE_ARMED)
  {
    uint8_t pref = pendingFlash[1];
    if (pref == 1) return false;
    if (pref == 2) return true;
    // AUTO or invalid: fall through
  }

  const uint8_t* mainFlash = CONFIG_FLASH_ADDR;
  uint8_t pref             = mainFlash[PICO9918_CONF_DISP_DRIVER_PREF];
  if (pref == 1) return false;
  if (pref == 2) return true;
  return isScartConnected();
}

/** \brief read the pending block; an erased or unrecognised state reads as CONFIRMED */
void readPendingDisplay(PendingDisplay* p)
{
  memcpy(p, PENDING_FLASH_ADDR, sizeof(*p));

  if (p->state != PENDING_STATE_PENDING && p->state != PENDING_STATE_ARMED)
  {
    p->state = PENDING_STATE_CONFIRMED;
  }
}

/** \brief erase and rewrite the pending block, verifying and retrying */
bool writePendingDisplay(const PendingDisplay* p)
{
  flash_range_erase(PENDING_FLASH_OFFSET, 0x1000);

  /* flash_range_program takes whole 256-byte pages, so the record is staged in one, the
     rest left at the erased value. Static rather than a local: core 0's stack is small,
     main holds most of it for the run, and the bus IRQs push onto the same stack. */
  static uint8_t page[FLASH_PAGE_SIZE];
  memset(page, 0xff, sizeof(page));
  memcpy(page, p, sizeof(*p));

  int attempts = 5;
  while (attempts--)
  {
    flash_range_program(PENDING_FLASH_OFFSET, page, sizeof(page));

    if (memcmp(PENDING_FLASH_ADDR, p, sizeof(*p)) == 0) return true;
  }
  return false;
}

/** \brief erase the pending block and clear the banner */
bool erasePendingDisplay(void)
{
  flash_range_erase(PENDING_FLASH_OFFSET, 0x1000);
  pendingBannerState = PENDING_BANNER_NONE;
  return true;
}

/** \brief current OSD banner state */
uint8_t pendingDisplayBanner(void)
{
  return pendingBannerState;
}

/** \brief advance the pending state machine: PENDING applies and arms, ARMED reverts and erases */
void __in_flash_func(applyPendingDisplay)(uint8_t config[CONFIG_BYTES])
{
  PendingDisplay p;
  readPendingDisplay(&p);

  if (p.state == PENDING_STATE_PENDING)
  {
    config[PICO9918_CONF_DISP_DRIVER_PREF] = p.dispDriverPref;
    config[PICO9918_CONF_VGA_MODE]         = p.vgaMode;
    config[PICO9918_CONF_SCART_MODE]       = p.scartMode;
    config[PICO9918_CONF_CLOCK_PRESET_ID]  = p.clockPresetId;

    p.state = PENDING_STATE_ARMED;
    writePendingDisplay(&p);

    pendingBannerState = PENDING_BANNER_AWAIT_OK;
    pico9918_config_refresh_pending_mirror(config, PENDING_STATE_ARMED);
  }
  else if (p.state == PENDING_STATE_ARMED)
  {
    erasePendingDisplay();
    pendingBannerState = PENDING_BANNER_NONE;
    pico9918_config_refresh_pending_mirror(config, PENDING_STATE_CONFIRMED);
  }
  else
  {
    pendingBannerState = PENDING_BANNER_NONE;
    pico9918_config_refresh_pending_mirror(config, PENDING_STATE_CONFIRMED);
  }
}

/** \brief read the configuration from flash, validating, defaulting and migrating it */
void readConfig(uint8_t config[CONFIG_BYTES])
{
  memcpy(config, CONFIG_FLASH_ADDR, CONFIG_BYTES);

  // library owns validation, defaults and per-version migration
  bool wasReset = false;
  bool stampVersion = pico9918_config_validate(config,
                                             config[PICO9918_CONF_PICO_MODEL] == PICO_MODEL,
                                             PICO9918_SW_VERSION_FULL, &wasReset);

  if (wasReset) // the block was zeroed, so the host identity bytes need restoring
  {
    config[PICO9918_CONF_PICO_MODEL] = PICO_MODEL;
    config[PICO9918_CONF_HW_VERSION] = currentHwVersion();
  }
  if (stampVersion)
  {
    config[PICO9918_CONF_SW_VERSION]       = PICO9918_SW_VERSION;
    config[PICO9918_CONF_SW_PATCH_VERSION] = PICO9918_PATCH_VER;

    // forced path, not pending-split: reset/migrated values are not a user
    // display change
    config[PICO9918_CONF_SAVE_FORCED] = 1;
  }

  tms9918->configDirty    = true; // so we apply it
  tms9918->configVdpDirty = true;
}

/** \brief erase and rewrite the whole config sector, verifying and retrying */
bool writeConfig(uint8_t config[CONFIG_BYTES])
{
  flash_range_erase(CONFIG_FLASH_OFFSET, 0x1000);

  config[PICO9918_CONF_PICO_MODEL] = PICO_MODEL;
  config[PICO9918_CONF_HW_VERSION] = currentHwVersion();
  config[PICO9918_CONF_SW_VERSION] = PICO9918_SW_VERSION;

  // sanity checking the palette 0 always 0, others always alpha 0xf
  config[PICO9918_CONF_PALETTE_IDX_0]     = 0;
  config[PICO9918_CONF_PALETTE_IDX_0 + 1] = 0;
  for (int i = 1; i < 16; ++i)
  {
    config[PICO9918_CONF_PALETTE_IDX_0 + (i * 2)] |= 0xf0;
  }

  bool success = false;

  int attempts = 5;
  while (attempts--)
  {
    flash_range_program(CONFIG_FLASH_OFFSET, config, CONFIG_BYTES);

    if (memcmp(CONFIG_FLASH_ADDR, config, CONFIG_BYTES) == 0)
    {
      success = true;
      break;
    }
  }

  return success;
}

/** \brief PICO9918_CONF_SAVE_TO_FLASH handler: changed tracked fields go to the pending block,
 *         the main config keeps its last-confirmed values
 */
bool saveConfigSplitPending(uint8_t config[CONFIG_BYTES])
{
  const uint8_t* flashConfig = CONFIG_FLASH_ADDR;
  bool anyTrackedChanged     = false;

  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    if (pico9918_config_fields[i].pendingMirror == PENDING_MIRROR_NONE) continue;
    uint8_t off = pico9918_config_fields[i].offset;
    if (config[off] != flashConfig[off])
    {
      anyTrackedChanged = true;
      break;
    }
  }

  bool ok = true;
  // doubles as scratch for the user-chosen values across the writeConfig() call
  PendingDisplay p = {
    .state          = PENDING_STATE_PENDING,
    .dispDriverPref = config[PICO9918_CONF_DISP_DRIVER_PREF],
    .vgaMode        = config[PICO9918_CONF_VGA_MODE],
    .scartMode      = config[PICO9918_CONF_SCART_MODE],
    .clockPresetId  = config[PICO9918_CONF_CLOCK_PRESET_ID],
  };

  if (anyTrackedChanged)
  {
    // pending block first: power loss between writes still reverts cleanly
    ok = writePendingDisplay(&p);

    // revert tracked fields to last-confirmed; clamp out-of-range flash bytes
    // so an uninitialised block can't invalidate the main config
    for (size_t i = 0; i < pico9918_config_field_count; ++i)
    {
      if (pico9918_config_fields[i].pendingMirror == PENDING_MIRROR_NONE) continue;
      uint8_t off  = pico9918_config_fields[i].offset;
      uint8_t last = flashConfig[off];
      config[off]  = (last > pico9918_config_fields[i].max) ? pico9918_config_fields[i].defaultValue : last;
    }
  }

  ok = writeConfig(config) && ok;

  if (anyTrackedChanged)
  {
    // restore user values for the running firmware, then refresh the mirror
    config[PICO9918_CONF_DISP_DRIVER_PREF] = p.dispDriverPref;
    config[PICO9918_CONF_VGA_MODE]         = p.vgaMode;
    config[PICO9918_CONF_SCART_MODE]       = p.scartMode;
    config[PICO9918_CONF_CLOCK_PRESET_ID]  = p.clockPresetId;
    pico9918_config_refresh_pending_mirror(config, PENDING_STATE_PENDING);
    pendingBannerState = PENDING_BANNER_AWAIT_PC; // power cycle to test
  }

  return ok;
}
