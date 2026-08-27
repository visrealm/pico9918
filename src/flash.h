/**
 * \file
 * \brief host-driven flash access: firmware update and program data storage
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

/** \brief perform the flash operation VDP register 0x3f selects
 *  \note  bit 7 writes rather than reads, bit 6 selects the firmware UF2 path over
 *         program data, and bits 5-0 are the VRAM page holding the block; progress
 *         and errors are reported in status register 2
 */
void flashSector(void);
