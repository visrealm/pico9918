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
 * is a property of the installed package rather than of this file. Same for the
 * runtime chip switch, which only a PICO9918_RUNTIME_CHIP=ON package exports.
 */

#include "pico9918.h"
#include "pico9918_config.h"
#include "pico9918_util.h"

#include <stdio.h>

#if PICO9918_BUILD_RUNTIME_CHIP
/* VR15 selects which status register a read returns, so it is how a host reads any of
   them. Above R7, so it needs an unlocked device. */
static uint8_t statusReg(PICO9918_INST_ARG uint8_t index)
{
  pico9918_write_register_value(PICO9918_INST 15, index);
  return pico9918_read_status(PICO9918_INST_ONLY);
}
#endif

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

#if PICO9918_BUILD_RUNTIME_CHIP
  /* The entry points exist only where the generated header says the library was built
     with them, so reaching them at all is half of what this proves. The other half is
     that the choice does something: a TMS9918A has no answer to the F18A unlock write, so
     its register file stays eight wide. Runs before the setup below, which then leaves
     the instance in the state the rest of the file expects. */
  if (pico9918_chip(PICO9918_INST_ONLY) != PICO9918_CHIP_MAX)
  {
    printf("a new instance is not PICO9918_CHIP_MAX\n");
    return 1;
  }

  pico9918_set_chip(PICO9918_INST PICO9918_CHIP_TMS9918A);
  if (pico9918_chip(PICO9918_INST_ONLY) != PICO9918_CHIP_TMS9918A)
  {
    printf("the chip did not step down\n");
    return 1;
  }

  /* Locked, a write above R7 is ignored and a read masks to the low three bits, so VR33
     both fails to take and reads back as the VR1 it aliases. Unlocked it is itself. That
     pair is the register file's width, which is what the unlock actually buys. */
  pico9918_write_register_value(PICO9918_INST 57, 0x1c);
  pico9918_write_register_value(PICO9918_INST 57, 0x1c);
  pico9918_write_register_value(PICO9918_INST 0x21, 0x5a);
  if (pico9918_reg_value(PICO9918_INST 0x21) != pico9918_reg_value(PICO9918_INST 0x01))
  {
    printf("a TMS9918A honoured the unlock\n");
    return 1;
  }

  pico9918_set_chip(PICO9918_INST PICO9918_CHIP_PICO9918);
  pico9918_write_register_value(PICO9918_INST 57, 0x1c);
  pico9918_write_register_value(PICO9918_INST 57, 0x1c);
  pico9918_write_register_value(PICO9918_INST 0x21, 0x5a);
  if (pico9918_reg_value(PICO9918_INST 0x21) != 0x5a)
  {
    printf("a PICO9918 refused the unlock: VR33 0x%02x\n",
           pico9918_reg_value(PICO9918_INST 0x21));
    return 1;
  }

  /* VR58 selects a config option and VR59 writes it, with SR12 reading back what landed.
     A real F18A has no such port, so neither register may reach it - and VR59 is the one
     that writes, so gating VR58 alone would leave the board's config open. */
  pico9918_set_chip(PICO9918_INST PICO9918_CHIP_F18A);
  pico9918_write_register_value(PICO9918_INST 58, 8);
  pico9918_write_register_value(PICO9918_INST 59, 0x5a);
  if (statusReg(PICO9918_INST 12) == 0x5a)
  {
    printf("an F18A reached the config port\n");
    return 1;
  }

  pico9918_set_chip(PICO9918_INST PICO9918_CHIP_PICO9918);
  pico9918_write_register_value(PICO9918_INST 58, 8);
  pico9918_write_register_value(PICO9918_INST 59, 0x5a);
  if (statusReg(PICO9918_INST 12) != 0x5a)
  {
    printf("a PICO9918 could not reach the config port\n");
    return 1;
  }

  /* M4 is F18A-only and honoured there even while locked, so the gate is the personality.
     The mode is cached at render time, hence the scan_line before each read. */
  pico9918_write_register_value(PICO9918_INST 0, 0x04); /* M4 - F18A-only, so no public name */
  pico9918_write_register_value(PICO9918_INST 1, TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE | TMS_R1_MODE_TEXT);

  pico9918_set_chip(PICO9918_INST PICO9918_CHIP_TMS9918A);
  pico9918_scan_line(PICO9918_INST 0);
  if (pico9918_display_mode(PICO9918_INST_ONLY) != TMS_MODE_TEXT)
  {
    printf("a TMS9918A with M4 and the text bit is not in 40-column text: mode %d\n",
           (int)pico9918_display_mode(PICO9918_INST_ONLY));
    return 1;
  }

  /* Mode comes from M1/M2/M3 alone, so the bit selects nothing. */
  pico9918_write_register_value(PICO9918_INST 1, TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE);
  pico9918_scan_line(PICO9918_INST 0);
  if (pico9918_display_mode(PICO9918_INST_ONLY) != TMS_MODE_GRAPHICS_I)
  {
    printf("a TMS9918A with R0 bit 2 set is not in Graphics I: mode %d\n",
           (int)pico9918_display_mode(PICO9918_INST_ONLY));
    return 1;
  }

  /* Three address bits, so VR8 is VR0 and the write lands. */
  pico9918_write_register_value(PICO9918_INST 0, 0x00);
  pico9918_write_register_value(PICO9918_INST 8, TMS_R0_MODE_GRAPHICS_II);
  if (pico9918_reg_value(PICO9918_INST 0) != TMS_R0_MODE_GRAPHICS_II)
  {
    printf("a TMS9918A dropped a VR8 write instead of masking it into VR0: VR0 0x%02x\n",
           pico9918_reg_value(PICO9918_INST 0));
    return 1;
  }
  pico9918_write_register_value(PICO9918_INST 0, 0x04);

  pico9918_write_register_value(PICO9918_INST 1, TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE | TMS_R1_MODE_TEXT);
  pico9918_set_chip(PICO9918_INST PICO9918_CHIP_F18A);
  pico9918_scan_line(PICO9918_INST 0);
  if (pico9918_display_mode(PICO9918_INST_ONLY) != TMS_MODE_TEXT80)
  {
    printf("a locked F18A refused 80-column text, which TurboForth needs\n");
    return 1;
  }

  /* With M4 set it ignores them instead, so VR0-15 setup writes cannot reach VR0-7. */
  pico9918_write_register_value(PICO9918_INST 8, 0x3f);
  if (pico9918_reg_value(PICO9918_INST 0) != 0x04)
  {
    printf("a locked F18A in 80 columns masked a VR8 write into VR0: VR0 0x%02x\n",
           pico9918_reg_value(PICO9918_INST 0));
    return 1;
  }

  /* M4 clear and it latches three bits like a 9918A again. */
  pico9918_write_register_value(PICO9918_INST 0, 0x00);
  pico9918_write_register_value(PICO9918_INST 8, TMS_R0_MODE_GRAPHICS_II);
  if (pico9918_reg_value(PICO9918_INST 0) != TMS_R0_MODE_GRAPHICS_II)
  {
    printf("a locked F18A with M4 clear dropped a VR8 write: VR0 0x%02x\n",
           pico9918_reg_value(PICO9918_INST 0));
    return 1;
  }

  /* VR57 stays reachable while locked; three address bits would put it in VR1. */
  pico9918_write_register_value(PICO9918_INST 1, TMS_R1_RAM_16K);
  pico9918_write_register_value(PICO9918_INST 57, 0x00);
  if (pico9918_reg_value(PICO9918_INST 1) != TMS_R1_RAM_16K)
  {
    printf("a VR57 write was latched into VR1: VR1 0x%02x\n",
           pico9918_reg_value(PICO9918_INST 1));
    return 1;
  }

  printf("pico9918-core: chip switch honoured, the register file and config port follow it\n");
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

  /* The settings block a host loads out of its own storage. Writable through the
     accessor and read by the library, which is the whole contract - so setting one
     panel byte and applying must derive the summary the overlay gates on. */
  uint8_t* config = pico9918_config(PICO9918_INST_ONLY);
  if (!config)
  {
    printf("pico9918_config returned NULL\n");
    return 1;
  }

  config[PICO9918_CONF_DIAG]           = 0;
  config[PICO9918_CONF_DIAG_REGISTERS] = 1;
  pico9918_config_apply(PICO9918_INST_ONLY);

  if (!pico9918_config(PICO9918_INST_ONLY)[PICO9918_CONF_DIAG])
  {
    printf("config applied but the derived DIAG summary stayed clear\n");
    return 1;
  }

  config[PICO9918_CONF_DIAG_REGISTERS] = 0;
  pico9918_config_apply(PICO9918_INST_ONLY);

  if (pico9918_config(PICO9918_INST_ONLY)[PICO9918_CONF_DIAG])
  {
    printf("every panel is off and the derived DIAG summary is still set\n");
    return 1;
  }

  printf("pico9918-core: config block reachable, %u bytes, applied both ways\n",
         (unsigned)CONFIG_BYTES);

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
