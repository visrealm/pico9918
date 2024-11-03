/*
 * Project: pico9918
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 * 
 * Purpose: TMS9900 GPU glue code
 * 
 * Credits: JasonACT (AtariAge)
 *
 */

#pragma once

#include "impl/vrEmuTms9918Priv.h"

/* initialize the TMS9900 GPU */
void gpuInit();

/* TMS9900 GPU main loop */
void gpuLoop();

/* trigger the GPU to run */
inline void gpuTrigger()
{
  tms9918->restart = 1;
}