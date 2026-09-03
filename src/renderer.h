/**
 * \file
 * \brief renderer - the VGA callbacks that turn VDP scanlines into pixels
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
#include "vga.h"

/** \brief install the renderer's scanline, frame and porch callbacks into \p params, before vgaInit() */
void rendererConfigureVga(VgaInitParams* params);
