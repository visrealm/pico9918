/*
 * Project: pico-56 - tms9918
 *
 * Copyright (c) 2023 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico-56
 *
 */

#pragma once

#include "vrEmuTms9918.h"
#include "vga.h"

#include <inttypes.h>

VrEmuTms9918* tmsInit();
VrEmuTms9918* getTms9918();

void tmsSetFrameCallback(vgaEndOfFrameFn cb);
void tmsSetHsyncCallback(vgaEndOfScanlineFn cb);

int tmsGetHsyncFreq();

void tmsDestroy();