/**
 * \file
 * \brief pico9918-core - a consumer of the installed package
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Drives the API the way a host does - create, reset, set a mode up, render a
 * line, read it back - so the exported package is checked for the things an
 * archive cannot show: a header the install rules missed, an include path that
 * only resolved in the build tree, a PUBLIC definition lost on the way out.
 *
 * Written for either instance mode, since which one the library was built with
 * is a property of the installed package rather than of this file.
 */

#include "pico9918.h"
#include "pico9918_util.h"

#include <stdio.h>

int main(void)
{
#if PICO9918_SINGLE_INSTANCE
  pico9918_init();
#else
  pico9918_t* tms9918 = pico9918_new();
  if (!tms9918)
  {
    printf("pico9918_new failed\n");
    return 1;
  }
#endif

  pico9918_reset(PICO9918_INST_ONLY);
  pico9918_initialise_gfx_i(PICO9918_INST_ONLY);
  pico9918_set_fg_bg_color(PICO9918_INST TMS_WHITE, TMS_DK_BLUE);
  pico9918_write_register_value(PICO9918_INST TMS_REG_1, TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE);

  const uint8_t status = pico9918_scan_line(PICO9918_INST 0);
  const uint32_t bytes = pico9918_line_bytes(PICO9918_INST_ONLY);
  const uint8_t* line  = pico9918_line_source(PICO9918_INST_ONLY);

  if (!line || bytes < TMS9918_PIXELS_X || bytes > PICO9918_SCANLINE_BUFFER_SIZE)
  {
    printf("implausible line: %p, %u bytes\n", (const void*)line, (unsigned)bytes);
    return 1;
  }
  if (pico9918_display_mode(PICO9918_INST_ONLY) != TMS_MODE_GRAPHICS_I)
  {
    printf("mode is not Graphics I\n");
    return 1;
  }

  printf("pico9918-core: %u-byte line, status 0x%02x, palette entry 1 0x%08x\n",
         (unsigned)bytes, status, pico9918_palette[1]);

#if !PICO9918_SINGLE_INSTANCE
  /* Two VDPs at once is what this mode is for, and no other test can check it: every
     harness in the library holds exactly one. Cleared VRAM makes every tile pixel
     transparent, so each line is its own instance's backdrop and the two must differ. */
  pico9918_t* second = pico9918_new();
  if (!second)
  {
    printf("second pico9918_new failed\n");
    return 1;
  }
  pico9918_reset(second);
  pico9918_initialise_gfx_i(second);
  pico9918_set_fg_bg_color(second, TMS_WHITE, TMS_DK_RED);
  pico9918_write_register_value(second, TMS_REG_1, TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE);

  /* the line is one module-level buffer, so read each instance's out before the other renders */
  pico9918_scan_line(tms9918, 0);
  const uint8_t firstPixel = pico9918_line_source(tms9918)[0];
  pico9918_scan_line(second, 0);
  const uint8_t secondPixel = pico9918_line_source(second)[0];

  if (firstPixel == secondPixel)
  {
    printf("two instances rendered the same line: 0x%02x\n", firstPixel);
    return 1;
  }
  printf("pico9918-core: two instances, lines 0x%02x and 0x%02x\n", firstPixel, secondPixel);

  pico9918_destroy(second);
  pico9918_destroy(PICO9918_INST_ONLY);
#endif
  return 0;
}
