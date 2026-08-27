/**
 * \file
 * \brief palette cache - the host half of the library's pixel LUT
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

#include <stdint.h>

/** \brief mark the cache stale so the next rendered scanline rebuilds it */
void paletteInit(void);

/** \brief point the RP2040 interpolators at the LUT, from core 1, which owns that state */
void paletteInitCore1(void);
