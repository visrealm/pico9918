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

#pragma once

/** \brief  apply the boot system clock preset, using the SCART set if a dongle was found
 *  \note   call after detectScartDongle() and before core 1 is launched
 */
void systemClockInit(void);

/** \brief switch to the configured clock preset if it differs from the running one */
void systemClockApplyConfig(void);

/** \brief  start GROMCLK and CPUCLK for the configured VDP device, holding an
 *          unused clock pin low
 *  \note   GROMCLK is the 10.738635 MHz crystal / 24, CPUCLK is / 3
 */
void vdpClocksInit(void);
