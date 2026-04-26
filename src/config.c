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

#include "impl/vrEmuTms9918Priv.h"

#include "gpio.h"
#include "vga.h"
#include "config.h"

#include "hardware/flash.h"
#include "hardware/gpio.h"

#include "pico/time.h"

#include <string.h>

#if PICO_RP2040
  #define PICO_MODEL 1
#elif PICO_RP2350
  #define PICO_MODEL 2
#endif

static Pico9918HardwareVersion hwVersion = HWVer_1_x;
static bool hwVersionDetected = false;

/*
 * detect the hardware version (v0.3 vs v0.4+)
 */
static Pico9918HardwareVersion detectHardwareVersion()
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

/*
 * current (detected) hardware version
 */
Pico9918HardwareVersion currentHwVersion()
{
  if (!hwVersionDetected)
  {
    hwVersion = detectHardwareVersion();
    hwVersionDetected = true;
  }
  return hwVersion;
}

static bool scartConnected = false;

/*
 * detect a SCART dongle by checking if the two sync pins are bridged.
 * The SCART dongle connects hsync (GPIO 0) and vsync (GPIO 1) via 1k resistor.
 * Drive one high, pull-down the other and read it. Must be called before vgaInit().
 */
bool detectScartDongle()
{
#if PICO9918_ENABLE_SCART
  const uint syncMask = 0x03 << VGA_SYNC_PINS_START;  // GPIO 0 and 1
  const uint driveMask = 0x01 << VGA_SYNC_PINS_START; // GPIO 0

  gpio_init_mask(syncMask);
  gpio_set_drive_strength(VGA_SYNC_PINS_START, GPIO_DRIVE_STRENGTH_12MA);
  gpio_set_dir_masked(syncMask, driveMask);  // GPIO 0 output, GPIO 1 input
  gpio_pull_down(VGA_SYNC_PINS_START + 1);

  gpio_set_mask(driveMask);
  sleep_ms(1);
  scartConnected = gpio_get(VGA_SYNC_PINS_START + 1);

  gpio_clr_mask(driveMask);
  gpio_set_drive_strength(VGA_SYNC_PINS_START, GPIO_DRIVE_STRENGTH_4MA);
  gpio_disable_pulls(VGA_SYNC_PINS_START + 1);
  gpio_set_dir_masked(syncMask, 0);  // both inputs
  // PIO will re-claim these pins during vgaInit()
#endif
  return scartConnected;
}

/*
 * true if a SCART dongle was detected at boot
 */
bool isScartConnected()
{
  return scartConnected;
}

/*
 * update CONF_DISP_DRIVER based on user preference, dongle detection, and
 * SCART mode config. call after both detectScartDongle() and readConfig() have run.
 *
 *   CONF_DISP_DRIVER_PREF: 0 = AUTO (detect dongle), 1 = force VGA, 2 = force SCART
 *   CONF_DISP_DRIVER (read-only result): 0 = VGA, 1 = NTSC, 2 = PAL
 *   CONF_SCART_MODE: 0 = PAL, 1 = NTSC
 */
void updateDispDriver()
{
  uint8_t pref = tms9918->config[CONF_DISP_DRIVER_PREF];
  bool useScart = (pref == 2) || (pref == 0 && isScartConnected());
  tms9918->config[CONF_DISP_DRIVER] = useScart
    ? (2 - tms9918->config[CONF_SCART_MODE]) : 0;
}

/*
 * apply current configuration to the VDP
 */
void applyConfig()
{
  vgaCurrentParams()->scanlines = tms9918->config[CONF_CRT_SCANLINES];

  if (tms9918->config[CONF_CRT_SCANLINES])
    TMS_REGISTER(tms9918, 50) |= 0x04;
  else
    TMS_REGISTER(tms9918, 50) &= ~0x04;

  TMS_REGISTER(tms9918, 30) = 1 << (tms9918->config[CONF_SCANLINE_SPRITES] + 2);

  // apply default palette
  for (int i = 0; i < 16; ++i)
  {
    uint16_t rgb = (tms9918->config[CONF_PALETTE_IDX_0 + (i * 2)] << 8) |
                    tms9918->config[CONF_PALETTE_IDX_0 + (i * 2) + 1];
    tms9918->vram.map.pram[i] = __builtin_bswap16(rgb);
  }
  
  tms9918->config[CONF_DIAG] = tms9918->config[CONF_DIAG_ADDRESS] ||
                               tms9918->config[CONF_DIAG_PALETTE] ||
                               tms9918->config[CONF_DIAG_PERFORMANCE] ||
                               tms9918->config[CONF_DIAG_REGISTERS];
}

#define CONFIG_FLASH_OFFSET  (0x200000 - 0x1000) // in the top 4kB of a 2MB flash
#define CONFIG_FLASH_ADDR    (uint8_t*)(XIP_BASE + CONFIG_FLASH_OFFSET)

// Pending-display block: 4 KB sector immediately below the main config block.
#define PENDING_FLASH_OFFSET (CONFIG_FLASH_OFFSET - 0x1000)
#define PENDING_FLASH_ADDR   (uint8_t*)(XIP_BASE + PENDING_FLASH_OFFSET)

static bool pendingActive = false;

/*
 * Decide the boot-time system clock. Peeks the pending block first (so a
 * pending driver change influences the initial clock) and falls back to the
 * main config block.
 *
 * Called before readConfig() / applyPendingDisplay() because the initial
 * vreg/PLL setup happens earlier than those.
 */
bool shouldUseScartClock()
{
  // Pending block takes precedence: a pending driver=SCART user expects the
  // SCART clock on the very first boot after their change.
  const uint8_t *pendingFlash = PENDING_FLASH_ADDR;
  uint8_t pendingState = pendingFlash[0];
  if (pendingState == PENDING_STATE_PENDING || pendingState == PENDING_STATE_ARMED)
  {
    uint8_t pref = pendingFlash[1];
    if (pref == 1) return false;                  // forced VGA
    if (pref == 2) return true;                   // forced SCART
    // pref == 0 (AUTO) or invalid: fall through to main config / dongle
  }

  const uint8_t *mainFlash = CONFIG_FLASH_ADDR;
  uint8_t pref = mainFlash[CONF_DISP_DRIVER_PREF];
  if (pref == 1) return false;
  if (pref == 2) return true;
  return isScartConnected();
}

/*
 * Read the pending-display block. Any value other than the three known state
 * markers (CONFIRMED/PENDING/ARMED) means the block is erased or corrupt;
 * treat as CONFIRMED so first boot on a fresh chip is a no-op.
 */
void readPendingDisplay(PendingDisplay *p)
{
  const uint8_t *src = PENDING_FLASH_ADDR;
  p->state          = src[0];
  p->dispDriverPref = src[1];
  p->vgaMode        = src[2];
  p->scartMode      = src[3];
  p->clockPresetId  = src[4];

  if (p->state != PENDING_STATE_PENDING && p->state != PENDING_STATE_ARMED)
  {
    p->state = PENDING_STATE_CONFIRMED;
  }
}

/*
 * Write the pending-display block (erases the 4 KB sector first). Mirrors
 * writeConfig()'s retry + verify flush.
 */
bool writePendingDisplay(const PendingDisplay *p)
{
  flash_range_erase(PENDING_FLASH_OFFSET, 0x1000);

  uint8_t buf[16] = { 0 };
  buf[0] = p->state;
  buf[1] = p->dispDriverPref;
  buf[2] = p->vgaMode;
  buf[3] = p->scartMode;
  buf[4] = p->clockPresetId;

  bool success = false;
  int attempts = 5;
  while (attempts--)
  {
    flash_range_program(PENDING_FLASH_OFFSET, buf, sizeof(buf));

    // flush XIP
    int i = 0;
    while (i < (int)sizeof(buf)) {
      *((volatile uint32_t *)(PENDING_FLASH_ADDR) + (i / sizeof(uint32_t))) = 0;
      i += sizeof(uint32_t);
    }

    if (memcmp(PENDING_FLASH_ADDR, buf, sizeof(buf)) == 0)
    {
      success = true;
      break;
    }
  }
  return success;
}

bool erasePendingDisplay()
{
  flash_range_erase(PENDING_FLASH_OFFSET, 0x1000);
  pendingActive = false;
  return true;
}

bool displayChangePending()
{
  return pendingActive;
}

/*
 * Update the in-RAM mirror at config[200..204] so the configurator can read
 * pending block state via VDP_REG(58)=200..204.
 */
static void mirrorPendingToConfig(uint8_t config[CONFIG_BYTES], const PendingDisplay *p)
{
  config[CONF_PENDING_STATE]        = p->state;
  config[CONF_PENDING_DRIVER_PREF]  = p->dispDriverPref;
  config[CONF_PENDING_VGA_MODE]     = p->vgaMode;
  config[CONF_PENDING_SCART_MODE]   = p->scartMode;
  config[CONF_PENDING_CLOCK_PRESET] = p->clockPresetId;
}

/*
 * Boot-time pending-display arbitration. Called after readConfig().
 *
 *   PENDING -> overlay pending values onto config[], advance state to ARMED
 *              in flash. Any unconfirmed reboot from here will revert.
 *   ARMED   -> previous boot was already armed and we're rebooting without a
 *              confirmation. Erase the pending block and leave the
 *              last-confirmed config[] in place (the revert path).
 *   else    -> no pending change; leave config[] alone.
 *
 * Updates the in-RAM mirror at config[CONF_PENDING_*] in all cases so the
 * configurator's VDP_REG(58)=200..204 reads return the live state, and sets
 * `pendingActive` for the OSD banner.
 */
void applyPendingDisplay(uint8_t config[CONFIG_BYTES])
{
  PendingDisplay p;
  readPendingDisplay(&p);

  if (p.state == PENDING_STATE_PENDING)
  {
    // Apply pending values to in-RAM config and advance to ARMED.
    config[CONF_DISP_DRIVER_PREF] = p.dispDriverPref;
    config[CONF_VGA_MODE]         = p.vgaMode;
    config[CONF_SCART_MODE]       = p.scartMode;
    config[CONF_CLOCK_PRESET_ID]  = p.clockPresetId;

    p.state = PENDING_STATE_ARMED;
    writePendingDisplay(&p);

    pendingActive = true;
  }
  else if (p.state == PENDING_STATE_ARMED)
  {
    // Booted again without confirmation -> revert. Erase the pending block;
    // config[] already holds last-confirmed values from readConfig().
    erasePendingDisplay();
    p.state = PENDING_STATE_CONFIRMED;
    p.dispDriverPref = config[CONF_DISP_DRIVER_PREF];
    p.vgaMode        = config[CONF_VGA_MODE];
    p.scartMode      = config[CONF_SCART_MODE];
    p.clockPresetId  = config[CONF_CLOCK_PRESET_ID];
    pendingActive = false;
  }
  else
  {
    pendingActive = false;
  }

  mirrorPendingToConfig(config, &p);
}

/*
 * Settable config field descriptor. Drives readConfig() validation, defaults,
 * and per-version migration. Adding a new field is a one-row append:
 *   { offset, max-value, default, version-introduced }.
 * `introducedIn` is a packed major/minor/patch (see PICO9918_SW_VERSION_FULL);
 * on version upgrade, fields with introducedIn > stored version are reset to
 * their defaults so users get sane values for newly-added options.
 */
typedef struct
{
  uint8_t  offset;
  uint8_t  max;          // inclusive upper bound; bounds-check is value > max
  uint8_t  defaultValue;
  uint16_t introducedIn;
} ConfigField;

static const ConfigField configFields[] =
{
  { CONF_CRT_SCANLINES,    1,                    0,            0x0000 },
  { CONF_SCANLINE_SPRITES, 3,                    0,            0x0000 },
  { CONF_CLOCK_PRESET_ID,  2,                    0,            0x0000 },
  { CONF_SCART_MODE,       1,                    0,            0x0000 },
  { CONF_VDP_DEVICE,       VDP_DEVICE_COUNT - 1, VDP_TMS9918A, 0x0000 },
  { CONF_DISP_DRIVER_PREF, 2,                    0,            0x1200 },  // 1.2.0
  { CONF_VGA_MODE,         0,                    0,            0x1200 },  // bump max as VGA modes are added
  { CONF_DIAG_REGISTERS,   1,                    0,            0x0000 },
  { CONF_DIAG_PERFORMANCE, 1,                    0,            0x0000 },
  { CONF_DIAG_PALETTE,     1,                    0,            0x0000 },
  { CONF_DIAG_ADDRESS,     1,                    0,            0x0000 },
};

#define CONFIG_FIELD_COUNT (sizeof(configFields) / sizeof(configFields[0]))

static inline uint16_t configStoredVersion(const uint8_t *config)
{
  return ((uint16_t)config[CONF_SW_VERSION] << 8) | config[CONF_SW_PATCH_VERSION];
}

static bool configOutOfRange(const uint8_t *config)
{
  for (size_t i = 0; i < CONFIG_FIELD_COUNT; ++i)
  {
    if (config[configFields[i].offset] > configFields[i].max) return true;
  }
  return false;
}

static void applyConfigDefaults(uint8_t *config)
{
  for (size_t i = 0; i < CONFIG_FIELD_COUNT; ++i)
  {
    config[configFields[i].offset] = configFields[i].defaultValue;
  }
}

// apply defaults only for fields introduced after storedVer
static void migrateNewFields(uint8_t *config, uint16_t storedVer)
{
  for (size_t i = 0; i < CONFIG_FIELD_COUNT; ++i)
  {
    if (configFields[i].introducedIn > storedVer)
    {
      config[configFields[i].offset] = configFields[i].defaultValue;
    }
  }
}

/*
 * read current configuration from flash
 */
void readConfig(uint8_t config[CONFIG_BYTES])
{
  memcpy(config, CONFIG_FLASH_ADDR, CONFIG_BYTES);

  uint16_t storedVer = configStoredVersion(config);

  if (config[CONF_PICO_MODEL] != PICO_MODEL ||
      config[CONF_PALETTE_IDX_0] != 0x00 ||
      (config[CONF_PALETTE_IDX_0 + 2] & 0xf0) != 0xf0 || // not initialised
      configOutOfRange(config))
  {
    memset(config, 0, CONFIG_BYTES);

    config[CONF_PICO_MODEL] = PICO_MODEL;
    config[CONF_HW_VERSION] = currentHwVersion();
    config[CONF_CLOCK_TESTED] = 0;

    applyConfigDefaults(config);

    config[CONF_PALETTE_IDX_0] = 0;
    config[CONF_PALETTE_IDX_0 + 1] = 0;
    for (int i = 1; i < 16; ++i)
    {
      uint16_t rgb = 0xf000 | vrEmuTms9918DefaultPalette(i);
      config[CONF_PALETTE_IDX_0 + (i * 2)] = rgb >> 8;
      config[CONF_PALETTE_IDX_0 + (i * 2) + 1] = rgb & 0xff;
    }

    storedVer = 0;  // force version stamp + save below
  }

  config[CONF_SAVE_TO_FLASH] = 0;

  // version upgrade: apply defaults for any newly-introduced fields and stamp.
  if (storedVer != PICO9918_SW_VERSION_FULL)
  {
    migrateNewFields(config, storedVer);
    config[CONF_SW_VERSION]       = PICO9918_SW_VERSION;
    config[CONF_SW_PATCH_VERSION] = PICO9918_PATCH_VER;
    config[CONF_SAVE_TO_FLASH]    = 1;
  }

  tms9918->configDirty = true;  // so we apply it
}

/*
 * write configuration to flash
 */
bool writeConfig(uint8_t config[CONFIG_BYTES])
{
  flash_range_erase(CONFIG_FLASH_OFFSET, 0x1000);

  config[CONF_PICO_MODEL] = PICO_MODEL;
  config[CONF_HW_VERSION] = currentHwVersion();
  config[CONF_SW_VERSION] = PICO9918_SW_VERSION;

  // sanity checking the palette 0 always 0, others always alpha 0xf
  config[CONF_PALETTE_IDX_0] = 0;
  config[CONF_PALETTE_IDX_0 + 1] = 0;
  for (int i = 1; i < 16; ++i)
  {
    config[CONF_PALETTE_IDX_0 + (i * 2)] |= 0xf0;
  }

  bool success = false;

  int attempts = 5;  
  while (attempts--)
  {
    flash_range_program(CONFIG_FLASH_OFFSET, (const void*)tms9918->config, 256);

    // flush
    int i = 0;
    while (i < 256) {
        *((volatile uint32_t *)(CONFIG_FLASH_ADDR) + i) = 0;
        i += sizeof(uint32_t);
    }

    if (memcmp(CONFIG_FLASH_ADDR, (const void*)tms9918->config, 256) == 0)
    {
      success = true;
      break;
    }
  }

  return success;
}

/*
 * Save dispatcher invoked on CONF_SAVE_TO_FLASH = 1. Splits the save:
 *   - Display-related fields (driver pref, VGA mode, SCART mode, clock preset)
 *     that differ from the last-confirmed flash values go into the pending
 *     block (state = PENDING). They will only become permanent after the user
 *     confirms in the configurator post-reboot.
 *   - All other fields go to the main config block as usual.
 *
 * If any tracked field is pending, the in-RAM config has those tracked fields
 * temporarily reverted to last-confirmed flash values for the main-config
 * write, so the main block continues to hold a last-known-good display
 * configuration. After the writes, the in-RAM tracked fields are restored to
 * the user's chosen values so the running firmware keeps using them and the
 * configurator's mirror at config[CONF_PENDING_*] reflects PENDING state.
 */
bool saveConfigSplitPending(uint8_t config[CONFIG_BYTES])
{
  static const uint8_t trackedFields[] = {
    CONF_DISP_DRIVER_PREF,
    CONF_VGA_MODE,
    CONF_SCART_MODE,
    CONF_CLOCK_PRESET_ID,
  };
  const size_t TRACKED_COUNT = sizeof(trackedFields) / sizeof(trackedFields[0]);

  const uint8_t *flashConfig = CONFIG_FLASH_ADDR;
  bool anyTrackedChanged = false;
  uint8_t pendingValues[sizeof(trackedFields) / sizeof(trackedFields[0])];
  uint8_t lastConfirmed[sizeof(trackedFields) / sizeof(trackedFields[0])];

  for (size_t i = 0; i < TRACKED_COUNT; ++i)
  {
    uint8_t off = trackedFields[i];
    pendingValues[i] = config[off];
    lastConfirmed[i] = flashConfig[off];
    if (pendingValues[i] != lastConfirmed[i]) anyTrackedChanged = true;
  }

  bool ok = true;

  if (anyTrackedChanged)
  {
    // Write pending block first - if power dies between this and the main
    // config write, we still revert cleanly on next boot.
    PendingDisplay p = {
      .state          = PENDING_STATE_PENDING,
      .dispDriverPref = pendingValues[0],
      .vgaMode        = pendingValues[1],
      .scartMode      = pendingValues[2],
      .clockPresetId  = pendingValues[3],
    };
    ok = writePendingDisplay(&p);

    // Temporarily restore last-confirmed values in the main config buffer so
    // the main block keeps the safe values.
    for (size_t i = 0; i < TRACKED_COUNT; ++i)
    {
      config[trackedFields[i]] = lastConfirmed[i];
    }
  }

  ok = writeConfig(config) && ok;

  if (anyTrackedChanged)
  {
    // Restore the user-chosen values in RAM so the firmware keeps using them
    // (until reboot or revert) and the configurator's pending mirror reflects
    // PENDING state for next read.
    for (size_t i = 0; i < TRACKED_COUNT; ++i)
    {
      config[trackedFields[i]] = pendingValues[i];
    }
    PendingDisplay p;
    readPendingDisplay(&p);
    config[CONF_PENDING_STATE]        = p.state;
    config[CONF_PENDING_DRIVER_PREF]  = p.dispDriverPref;
    config[CONF_PENDING_VGA_MODE]     = p.vgaMode;
    config[CONF_PENDING_SCART_MODE]   = p.scartMode;
    config[CONF_PENDING_CLOCK_PRESET] = p.clockPresetId;
    pendingActive = true;
  }

  return ok;
}