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

#pragma once


/* initialise temperature sensor */
void initTemperature();

/* read temperature (celsius) */
float coreTemperatureC();

/* read temperature (fahrenheit) */
float coreTemperatureF();