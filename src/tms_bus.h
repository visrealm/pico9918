/**
 * \file
 * \brief TMS9918A host bus interface - PIO state machines and their IRQ handlers
 *
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** \brief  start the read and write state machines, plus the host reset IRQ on
 *          hardware later than v0.3
 *  \note   both run on pio1 with clkdiv 1.0, so the PIO clock is the system clock
 */
void tmsBusInit(void);
