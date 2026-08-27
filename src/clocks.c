/**
 * \file
 * \brief system clock presets and the VDP GROMCLK / CPUCLK outputs
 *
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

#include "clocks.h"

#include "clocks.pio.h"
#include "config.h"
#include "xip.h"
#include "overlay/diag.h"
#include "display.h"
#include "gpio.h"
#include "impl/pico9918_priv.h"

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

#define TMS_CRYSTAL_FREQ_HZ 10738635.0f
#define TMS_GROMCLK_FREQ_HZ (TMS_CRYSTAL_FREQ_HZ / 24.0f)
#define TMS_CPUCLK_FREQ_HZ  (TMS_CRYSTAL_FREQ_HZ / 3.0f)
#define TMS_CLK_OFF         0.0f

#define CLOCK_PIO pio1

/** \brief one system clock preset: the PLL and dividers to reach it, and the core voltage it needs */
typedef struct
{
  int pll;     ///< VCO frequency in Hz
  int pllDiv1; ///< first post-divider
  int pllDiv2; ///< second post-divider
  int voltage; ///< VREG_VOLTAGE_* the resulting clock is stable at
  int clockHz; ///< pll / pllDiv1 / pllDiv2, filled in by CLOCK_PRESET
} ClockSettings;

#define CLOCK_PRESET(PLL, PD1, PD2, VOL) {PLL, PD1, PD2, VOL, PLL / PD1 / PD2}

#if PICO9918_ENABLE_SCART
/** \brief SCART system clock presets: 270, 324, 324 MHz */
static const ClockSettings __cold_in_flash("boot") scartClockPresets[] = {
  CLOCK_PRESET(1080000000, 4, 1, VREG_VOLTAGE_1_15), CLOCK_PRESET(1296000000, 4, 1, VREG_VOLTAGE_1_20),
  CLOCK_PRESET(1296000000, 4, 1, VREG_VOLTAGE_1_20)};
#endif

/** \brief VGA system clock presets: 252, 302.4, 352 MHz */
static const ClockSettings __cold_in_flash("boot") vgaClockPresets[] = {
  CLOCK_PRESET(1512000000, 6, 1, VREG_VOLTAGE_1_15), CLOCK_PRESET(1512000000, 5, 1, VREG_VOLTAGE_1_20),
  CLOCK_PRESET(1056000000, 3, 1, VREG_VOLTAGE_1_30)};

static const ClockSettings* clockPresets = vgaClockPresets;
static int clockPresetIndex              = 0;

/** \brief clock outputs for one VdpDevice, TMS_CLK_OFF where that device drives nothing */
typedef struct
{
  float pin37freq; ///< GROMCLK pin
  float pin38freq; ///< CPUCLK pin
} VdpClockConfig;

/** \brief output frequency for TMS9918A pins 37 and 38, indexed by VdpDevice */
static const VdpClockConfig __cold_in_flash("boot") vdpClockConfigs[] = {
  {TMS_GROMCLK_FREQ_HZ, TMS_CPUCLK_FREQ_HZ},
  {TMS_GROMCLK_FREQ_HZ, TMS_CLK_OFF},
  {TMS_CLK_OFF, TMS_CPUCLK_FREQ_HZ},
  {TMS_CPUCLK_FREQ_HZ, TMS_CLK_OFF},
};

/** \brief raise the core voltage for the preset, then switch the PLL to it */
static void applySystemClock(int presetIndex)
{
  const ClockSettings settings = clockPresets[presetIndex];
  vreg_set_voltage(settings.voltage);
  sleep_ms(1);
  set_sys_clock_pll(settings.pll, settings.pllDiv1, settings.pllDiv2);
}

/** \brief apply the boot preset, using the SCART set if a dongle was detected */
void systemClockInit(void)
{
#if PICO9918_ENABLE_SCART
  if (shouldUseScartClock()) clockPresets = scartClockPresets;
#endif
  applySystemClock(clockPresetIndex);
}

/** \brief switch to the configured preset if it differs from the running one */
void systemClockApplyConfig(void)
{
  const int configuredPreset = tms9918->config[PICO9918_CONF_CLOCK_PRESET_ID];
  if (configuredPreset != clockPresetIndex)
  {
    clockPresetIndex = configuredPreset;
    applySystemClock(clockPresetIndex);
  }
}

#ifndef PICO9918_NO_CLOCKS
/** \brief  set a clock SM's divider for freqHz and start it
 *  \note   the SM drives one edge per cycle, so an output period is two SM cycles
 */
static void __in_flash_func(updateClock)(uint pioSm, float freqHz)
{
  float clockDiv = ((float)clockPresets[clockPresetIndex].clockHz) / (freqHz * 2.0f);
  pio_sm_set_clkdiv(CLOCK_PIO, pioSm, clockDiv);
  pio_sm_set_enabled(CLOCK_PIO, pioSm, true);
  pico9918_diag_set_clock_hz(clockPresets[clockPresetIndex].clockHz);
}

/** \brief load the clock program once, then run it on pioSm driving gpio at freqHz */
static void __in_flash_func(initClock)(uint gpio, uint pioSm, float freqHz)
{
  static uint clocksPioOffset = -1;

  if (clocksPioOffset == -1) clocksPioOffset = pio_add_program(CLOCK_PIO, &clock_program);

  pio_gpio_init(CLOCK_PIO, gpio);
  pio_sm_set_consecutive_pindirs(CLOCK_PIO, pioSm, gpio, 1, true);
  pio_sm_config c = clock_program_get_default_config(clocksPioOffset);
  sm_config_set_set_pins(&c, gpio, 1);
  pio_sm_init(CLOCK_PIO, pioSm, clocksPioOffset, &c);
  updateClock(pioSm, freqHz);
}

/** \brief start the clock, or hold the pin low when this device has no clock on it */
static void __in_flash_func(initClockOrPullLow)(uint gpio, uint pioSm, float freqHz)
{
  if (freqHz > 0.0f)
  {
    initClock(gpio, pioSm, freqHz);
  }
  else
  {
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_OUT);
    gpio_put(gpio, 0);
  }
}
#endif

/** \brief bring up GROMCLK and CPUCLK on the pins this hardware revision uses */
void __in_flash_func(vdpClocksInit)(void)
{
#ifndef PICO9918_NO_CLOCKS
  const Pico9918HardwareVersion hwVersion = currentHwVersion();
  const VdpClockConfig* config            = &vdpClockConfigs[tms9918->config[PICO9918_CONF_VDP_DEVICE]];
  const uint gromClkGpio                  = (hwVersion == HWVer_0_3) ? GPIO_GROMCL_V03 : GPIO_GROMCL;
  const uint cpuClkGpio                   = (hwVersion == HWVer_0_3) ? GPIO_CPUCL_V03 : GPIO_CPUCL;

  initClockOrPullLow(gromClkGpio, 2, config->pin37freq);
  initClockOrPullLow(cpuClkGpio, 3, config->pin38freq);
#endif
}
