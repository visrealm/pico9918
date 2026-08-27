/**
 * \file
 * \brief PIO state machine register helpers
 *
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

/** \brief  set the pio state machine y register
 *  \note   runs from flash, so it must stay init-only - see xip.h
 */
void pio_set_y(PIO pio, uint sm, uint32_t y);
