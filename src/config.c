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
 * update CONF_DISP_DRIVER based on detection result and SCART timing config.
 * call after both detectScartDongle() and readConfig() have run.
 */
void updateDispDriver()
{
  // DISP_DRIVER: 0=VGA, 1=NTSC, 2=PAL. SCART_TIMING: 0=PAL, 1=NTSC
  tms9918->config[CONF_DISP_DRIVER] = isScartConnected()
    ? (2 - tms9918->config[CONF_SCART_TIMING]) : 0;
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

#define CONFIG_FLASH_OFFSET (0x200000 - 0x1000) // in the top 4kB of a 2MB flash
#define CONFIG_FLASH_ADDR   (uint8_t*)(XIP_BASE + CONFIG_FLASH_OFFSET)

/*
 * read current configuration from flash
 */
void readConfig(uint8_t config[CONFIG_BYTES])
{
  memcpy(config, CONFIG_FLASH_ADDR, CONFIG_BYTES);

  if (config[CONF_PICO_MODEL] != PICO_MODEL ||
      config[CONF_CLOCK_PRESET_ID] > 2 ||
      config[CONF_VDP_DEVICE] >= VDP_DEVICE_COUNT ||
      config[CONF_CRT_SCANLINES] > 1 ||
      config[CONF_SCANLINE_SPRITES] > 3 ||
      config[CONF_SCART_TIMING] > 1 ||
      config[CONF_PALETTE_IDX_0] != 0x00 ||
      (config[CONF_PALETTE_IDX_0 + 2] & 0xf0) != 0xf0) // not initialised
  {
    memset(config, 0, CONFIG_BYTES);

    config[CONF_PICO_MODEL] = PICO_MODEL;
    config[CONF_SW_VERSION] = PICO9918_SW_VERSION;
    config[CONF_HW_VERSION] = currentHwVersion();
    config[CONF_CLOCK_TESTED] = 0;

    config[CONF_CRT_SCANLINES] = 0;
    config[CONF_SCANLINE_SPRITES] = 0;
    config[CONF_CLOCK_PRESET_ID] = 0;
    config[CONF_VDP_DEVICE] = VDP_TMS9918A;
    config[CONF_SCART_TIMING] = 0;  // default PAL

    config[CONF_PALETTE_IDX_0] = 0;
    config[CONF_PALETTE_IDX_0 + 1] = 0;
    for (int i = 1; i < 16; ++i)
    {
      uint16_t rgb = 0xf000 | vrEmuTms9918DefaultPalette(i);
      config[CONF_PALETTE_IDX_0 + (i * 2)] = rgb >> 8;
      config[CONF_PALETTE_IDX_0 + (i * 2) + 1] = rgb & 0xff;
    }
  }

  config[CONF_SAVE_TO_FLASH] = 0;

    // looks like we've just upgraded
  if (config[CONF_SW_VERSION] != PICO9918_SW_VERSION ||
      config[CONF_SW_PATCH_VERSION] != PICO9918_PATCH_VER)
  {
    config[CONF_SW_VERSION] = PICO9918_SW_VERSION;
    config[CONF_SW_PATCH_VERSION] = PICO9918_PATCH_VER;
    config[CONF_SAVE_TO_FLASH] = 1;
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