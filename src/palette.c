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

#include "palette.h"

#include "impl/pico9918_priv.h"

/* The LUT and its rebuild are the library's (pico9918_palette_lut,
   pico9918_palette_regenerate), driven off palDirty from the scanline path. Two things
   cannot follow them: the RP2040 interpolators are per-core state and have to be
   pointed at the LUT from the core that expands lines, and the initial dirty flag is
   raised from core 0 before that core is running. */

void paletteInitCore1(void)
{
  PICO9918_EXPAND_INIT(pico9918_palette_lut);
}

void paletteInit(void)
{
  tms9918->palDirty = 1;
}
