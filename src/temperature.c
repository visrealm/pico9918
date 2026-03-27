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

#include "hardware/adc.h"
#include "pico/divider.h"

#if !PICO_RP2040
#include "hardware/regs/sysinfo.h"
#include "hardware/regs/addressmap.h"
#endif

#include <stdbool.h>
#include <stdint.h>

#if PICO_RP2040
#define TEMP_SENSOR_CHANNEL 4
#else
// RP2350A (QFN-60, PACKAGE_SEL=0) uses channel 4.
// RP2350B (QFN-80, PACKAGE_SEL=1) uses channel 8.
// Read PACKAGE_SEL at runtime so one binary works on both variants.
static inline int tempSensorChannel()
{
  const volatile uint32_t *package_sel = (const volatile uint32_t *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET);
  return (*package_sel & SYSINFO_PACKAGE_SEL_BITS) ? 4 : 8;
}
#define TEMP_SENSOR_CHANNEL tempSensorChannel()
#endif

/*
 * initialise temperature hardware
 */
void initTemperature()
{
  adc_init();
  adc_set_temp_sensor_enabled(true);
  adc_select_input(TEMP_SENSOR_CHANNEL);
}

/*
 * read temperature (celsius)
 */
float coreTemperatureC()
{
  int v = adc_read();
  const float vref = 3.3f;
  float t = 27.0f - ((v * vref / 4096.0f) - 0.706f) / 0.001721f; // From the datasheet
  return t;
}

/*
 * read temperature (fahrenheit)
 */
float coreTemperatureF()
{
  return coreTemperatureC() * (9.0f / 5.0f) + 32.0f;
}