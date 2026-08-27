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

#pragma once


/** \brief enable the temperature sensor and select its ADC input */
void initTemperature(void);

/** \brief die temperature in celsius */
float coreTemperatureC(void);
