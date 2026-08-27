/**
 * \file
 * \brief on-die temperature sensor
 *
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 */

#include "hardware/adc.h"
#include "pico/divider.h"

#include "xip.h"

#if !PICO_RP2040
#include "hardware/regs/sysinfo.h"
#include "hardware/regs/addressmap.h"
#endif

#include <stdbool.h>
#include <stdint.h>

#if PICO_RP2040
#define TEMP_SENSOR_CHANNEL 4
#else
/** \brief  ADC input the temperature sensor sits on, read at runtime so one binary serves both
 *  \return 4 for the QFN-60 RP2350A, 8 for the QFN-80 RP2350B
 */
static inline int tempSensorChannel(void)
{
  const volatile uint32_t* package_sel = (const volatile uint32_t*)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET);
  return (*package_sel & SYSINFO_PACKAGE_SEL_BITS) ? 4 : 8;
}
#define TEMP_SENSOR_CHANNEL tempSensorChannel()
#endif

/** \brief enable the temperature sensor and select its ADC input */
void __in_flash_func(initTemperature)(void)
{
  adc_init();
  adc_set_temp_sensor_enabled(true);
  adc_select_input(TEMP_SENSOR_CHANNEL);
}

/** \brief die temperature in celsius */
float coreTemperatureC(void)
{
  int v            = adc_read();
  const float vref = 3.3f;
  float t          = 27.0f - ((v * vref / 4096.0f) - 0.706f) / 0.001721f; // From the datasheet
  return t;
}
