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

#include <stdbool.h>

static bool tempInitialised = false;

/*
 * initialise temperature hardware
 */
static void analogReadTempSetup()
{
  adc_init();
  adc_set_temp_sensor_enabled(true);

#if PICO_RP2040
  adc_select_input(4); // Temperature sensor
#else
  adc_select_input(8); // RP2350 QFN80 package only... 
#endif
    
  tempInitialised = true;
}

/*
 * read temperature (celsius)
 */
float coreTemperatureC()
{
  if (!tempInitialised) analogReadTempSetup();

  const float vref = 3.3f;
  int v = adc_read();
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