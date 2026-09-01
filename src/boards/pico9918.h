/**
 * \file
 * \brief PICO9918 board definition - a Pico that boots to a higher system clock,
 *        so the crystal is given longer to settle
 *
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#pragma once

// For board detection
#define PICO9918

#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 32
#endif

// Everything else is a stock Pico. Defined above so these win: the values in
// pico.h are all #ifndef-guarded defaults.
#include "boards/pico.h"
