/**
 * \file
 * \brief macros keeping cold code and data resident in flash (XIP)
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

#include "pico.h"

/* Both macros below apply only to code and data last reached strictly before
   pico9918_gpu_loop() starts: it dispatches flashSector() and writeConfig(), which erase and
   program flash, and a fetch from flash while XIP is down hard faults either core. */
#if PICO9918_COLD_IN_FLASH

/** \brief keep a function resident in flash, in a .flashcode section named after
 *         it that xip_sections.ld collects
 *  \note  noinline stops the body folding back into a RAM-resident caller
 */
#define __in_flash_func(func_name) \
  __attribute__((section(".flashcode." __STRING(func_name)), noinline, aligned(8))) func_name

/** \brief keep 8-byte-aligned data resident in flash, under the same reachability rule */
#define __cold_in_flash(group) __aligned(8) __in_flash(group)

#else

#define __in_flash_func(func_name) func_name
#define __cold_in_flash(group)

#endif
