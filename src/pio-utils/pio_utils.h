/*
 * Project: pico-56 - pio utilities
 *
 * Copyright (c) 2023 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico-56
 *
 */

#pragma once

#include "hardware/pio.h"

void pio_set_x(PIO pio, uint sm, uint32_t y);
void pio_set_y(PIO pio, uint sm, uint32_t y);