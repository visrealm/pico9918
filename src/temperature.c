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
#include "hardware/regs/addressmap.h"
#endif


#include <stdbool.h>
#include <stdint.h>

/*
 * initialise temperature hardware
 */
void initTemperature()
{
  adc_init();
  adc_set_temp_sensor_enabled(true);
  
  /*
   * RP2040 and RP2350A use the same internal temperature channel.
   * RP2350B (QFN60, PACKAGE_SEL = 1) moved the sensor to a different channel.
   */
#if PICO_RP2040
  adc_select_input(4); // RP2040 internal temperature sensor
#else
  // SYSINFO offset 0x04 bit 0: PACKAGE_SEL (0 = QFN80 / RP2350A, 1 = QFN60 / RP2350B)
  const volatile uint32_t *package_sel = (uint32_t *)(SYSINFO_BASE + 0x04u);
  const bool is_rp2350b = (*package_sel & 0x1u) != 0;

  if (is_rp2350b)
  {
    adc_select_input(8); // RP2350B temperature sensor channel
  }
  else
  {
    adc_select_input(4); // RP2350A temperature sensor channel
  }
#endif
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