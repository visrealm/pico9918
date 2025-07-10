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

#include <string.h>

#if PICO9918_SCART_RGBS // 0 = VGA, 1 = NTSC, 2 = PAL
  #define PICO9918_DISP_DRIVER (1 + PICO9918_SCART_PAL)
#else
  #define PICO9918_DISP_DRIVER 0
#endif

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

#if PICO_RP2040
  // check if RESET pin is being driven externally (on v0.4+, it is, on v0.3 it isn't since it's CPUCL)
  gpio_set_dir_masked(GPIO_RESET_MASK, 0 << GPIO_RESET);  // reset input
  gpio_pull_up(GPIO_RESET);
  sleep_us(100);  
  if (gpio_get(GPIO_RESET)) // following pull... ok
  {
    gpio_pull_down(GPIO_RESET);
    sleep_us(100);  
    if (!gpio_get(GPIO_RESET)) // still following pull... must be v0.3
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
      config[CONF_DISP_DRIVER] != PICO9918_DISP_DRIVER ||
      config[CONF_CLOCK_PRESET_ID] > 2 ||
      config[CONF_CRT_SCANLINES] > 1 ||
      config[CONF_SCANLINE_SPRITES] > 3 ||
      config[CONF_PALETTE_IDX_0] != 0x00 ||
      (config[CONF_PALETTE_IDX_0 + 2] & 0xf0) != 0xf0) // not initialised
  {
    memset(config, 0, CONFIG_BYTES);

    config[CONF_PICO_MODEL] = PICO_MODEL;
    config[CONF_SW_VERSION] = PICO9918_SW_VERSION;
    config[CONF_HW_VERSION] = currentHwVersion();
    config[CONF_DISP_DRIVER] = PICO9918_DISP_DRIVER;
    config[CONF_CLOCK_TESTED] = 0;

    config[CONF_CRT_SCANLINES] = 0;
    config[CONF_SCANLINE_SPRITES] = 0;
    config[CONF_CLOCK_PRESET_ID] = 0;

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