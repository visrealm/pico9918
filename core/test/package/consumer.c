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

#include "overlay/splash.h"
#include "pico9918.h"
#include "pico9918_config.h"
#include "pico9918_frame.h"
#include "pico9918_util.h"

#include <stdio.h>

#if !PICO9918_SINGLE_INSTANCE
/* What the config-applied callback was handed. One recorder for both instances on
   purpose: the registration is what has to be per instance, not the function. */
static struct
{
  pico9918_t* inst;
  void* userdata;
  int calls;
} appliedSeen;

static void appliedCallback(pico9918_t* tms9918, void* userdata)
{
  appliedSeen.inst     = tms9918;
  appliedSeen.userdata = userdata;
  ++appliedSeen.calls;
}
#endif

#if PICO9918_BUILD_RUNTIME_CHIP
/* VR15 selects which status register a read returns, so it is how a host reads any of
   them. Above R7, so it needs an unlocked device. */
static uint8_t statusReg(PICO9918_INST_ARG uint8_t index)
{
  pico9918_write_register_value(PICO9918_INST PICO9918_REG_STATUS_SELECT, index);
  return pico9918_read_status(PICO9918_INST_ONLY);
}

/* The F18A badge's expected artwork, as a 64-bit FNV-1a over every pixel of rows 0..13,
   low byte then high byte so the constant does not depend on this host's endianness.
   Computed from the ROM art and PICO9918_PIXEL_FROM_RGB12 independently of the library,
   which is what makes it a check rather than a restatement. */
#define FNV64_OFFSET 0xcbf29ce484222325ull
#define FNV64_PRIME  0x00000100000001b3ull
#define BADGE_DIGEST 0xc0ce60a2a13c9a2dull

#define BADGE_GUARD       2 /* columns past the badge that must stay untouched */
#define BADGE_BUFFER      (PICO9918_F18A_BADGE_WIDTH + BADGE_GUARD)
#define BADGE_LINE_PIXELS 640

static uint64_t fnv1aByte(uint64_t h, uint8_t b)
{
  return (h ^ b) * FNV64_PRIME;
}

static void fillPixels(PICO9918_PIXEL_T* pixels, uint32_t count, PICO9918_PIXEL_T value)
{
  for (uint32_t i = 0; i < count; ++i) pixels[i] = value;
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
  pico9918_write_register_value(PICO9918_INST PICO9918_REG_UNLOCK, PICO9918_R57_UNLOCK);
  pico9918_write_register_value(PICO9918_INST PICO9918_REG_UNLOCK, PICO9918_R57_UNLOCK);
  pico9918_write_register_value(PICO9918_INST 0x21, 0x5a);
  if (pico9918_reg_value(PICO9918_INST 0x21) != pico9918_reg_value(PICO9918_INST 0x01))
  {
    printf("a TMS9918A honoured the unlock\n");
    return 1;
  }

  pico9918_set_chip(PICO9918_INST PICO9918_CHIP_PICO9918);
  pico9918_write_register_value(PICO9918_INST PICO9918_REG_UNLOCK, PICO9918_R57_UNLOCK);
  pico9918_write_register_value(PICO9918_INST PICO9918_REG_UNLOCK, PICO9918_R57_UNLOCK);
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
  pico9918_write_register_value(PICO9918_INST PICO9918_REG_CONFIG_INDEX, 8);
  pico9918_write_register_value(PICO9918_INST PICO9918_REG_CONFIG_VALUE, 0x5a);
  if (statusReg(PICO9918_INST 12) == 0x5a)
  {
    printf("an F18A reached the config port\n");
    return 1;
  }

  pico9918_set_chip(PICO9918_INST PICO9918_CHIP_PICO9918);
  pico9918_write_register_value(PICO9918_INST PICO9918_REG_CONFIG_INDEX, 8);
  pico9918_write_register_value(PICO9918_INST PICO9918_REG_CONFIG_VALUE, 0x5a);
  if (statusReg(PICO9918_INST 12) != 0x5a)
  {
    printf("a PICO9918 could not reach the config port\n");
    return 1;
  }

  /* M4 is F18A-only and honoured there even while locked, so the gate is the personality.
     The mode is cached at render time, hence the scan_line before each read. */
  pico9918_write_register_value(PICO9918_INST TMS_REG_0, TMS_R0_MODE_TEXT_80);
  pico9918_write_register_value(PICO9918_INST TMS_REG_1,
                                TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE | TMS_R1_MODE_TEXT);

  pico9918_set_chip(PICO9918_INST PICO9918_CHIP_TMS9918A);
  pico9918_scan_line(PICO9918_INST 0);
  if (pico9918_display_mode(PICO9918_INST_ONLY) != TMS_MODE_TEXT)
  {
    printf("a TMS9918A with M4 and the text bit is not in 40-column text: mode %d\n",
           (int)pico9918_display_mode(PICO9918_INST_ONLY));
    return 1;
  }

  /* Mode comes from M1/M2/M3 alone, so the bit selects nothing. */
  pico9918_write_register_value(PICO9918_INST TMS_REG_1, TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE);
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

  pico9918_write_register_value(PICO9918_INST TMS_REG_1,
                                TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE | TMS_R1_MODE_TEXT);
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
  pico9918_write_register_value(PICO9918_INST TMS_REG_1, TMS_R1_RAM_16K);
  pico9918_write_register_value(PICO9918_INST 57, 0x00);
  if (pico9918_reg_value(PICO9918_INST 1) != TMS_R1_RAM_16K)
  {
    printf("a VR57 write was latched into VR1: VR1 0x%02x\n",
           pico9918_reg_value(PICO9918_INST 1));
    return 1;
  }

  printf("pico9918-core: chip switch honoured, the register file and config port follow it\n");

  /* The F18A's power-on badge. Its geometry macros come from the installed overlay
     header, so reaching them at all also checks the install rules carry it. */
  {
    const PICO9918_PIXEL_T sentinel = 0x0123;
    PICO9918_PIXEL_T __aligned(4) badge[PICO9918_F18A_BADGE_WIDTH + BADGE_GUARD];
    uint64_t digest = FNV64_OFFSET;

    for (uint16_t line = 0; line < PICO9918_F18A_BADGE_HEIGHT; ++line)
    {
      fillPixels(badge, BADGE_BUFFER, sentinel);

      if (!pico9918_f18a_badge_render(line, 0, badge))
      {
        printf("the F18A badge did not draw row %u\n", (unsigned)line);
        return 1;
      }

      /* The badge is the left edge of a line the host owns the rest of, so an overrun
         here is an overrun into the border or the picture. */
      for (uint32_t g = 0; g < BADGE_GUARD; ++g)
      {
        if (badge[PICO9918_F18A_BADGE_WIDTH + g] != sentinel)
        {
          printf("the F18A badge drew past column %u on row %u\n",
                 (unsigned)PICO9918_F18A_BADGE_WIDTH, (unsigned)line);
          return 1;
        }
      }

      for (uint32_t x = 0; x < PICO9918_F18A_BADGE_WIDTH; ++x)
      {
        digest = fnv1aByte(digest, (uint8_t)(badge[x] & 0xff));
        digest = fnv1aByte(digest, (uint8_t)((badge[x] >> 8) & 0xff));
      }
    }

    /* Pins the artwork, the palette conversion and the 1bpp packing together. The asset
       carries no bit-depth macro, so a palette grown past two entries would silently
       become 2bpp and only this catches it. */
    if (digest != BADGE_DIGEST)
    {
      printf("the F18A badge artwork digest is 0x%016llx, expected 0x%016llx\n",
             (unsigned long long)digest, (unsigned long long)BADGE_DIGEST);
      return 1;
    }

    /* Named, so a digest mismatch has somewhere to start: the margin and the box. */
    pico9918_f18a_badge_render(0, 0, badge);
    if (badge[0] != PICO9918_PIXEL_FROM_RGB12(0xb202))
    {
      printf("the F18A badge margin is 0x%04x, expected dark green\n", (unsigned)badge[0]);
      return 1;
    }
    pico9918_f18a_badge_render(1, 0, badge);
    if (badge[2] != PICO9918_PIXEL_FROM_RGB12(0xff0f))
    {
      printf("the F18A badge box edge is 0x%04x, expected white\n", (unsigned)badge[2]);
      return 1;
    }

    /* Shown for frames 0..383 and never again, and a frame outside that window must
       leave the buffer alone rather than draw something the host then has to undo. */
    if (!pico9918_f18a_badge_render(0, PICO9918_F18A_BADGE_FRAMES - 1, badge))
    {
      printf("the F18A badge stopped one frame early\n");
      return 1;
    }

    fillPixels(badge, BADGE_BUFFER, sentinel);
    if (pico9918_f18a_badge_render(0, PICO9918_F18A_BADGE_FRAMES, badge) ||
        badge[0] != sentinel)
    {
      printf("the F18A badge outlived frame %u\n", (unsigned)PICO9918_F18A_BADGE_FRAMES);
      return 1;
    }

    fillPixels(badge, BADGE_BUFFER, sentinel);
    if (pico9918_f18a_badge_render(PICO9918_F18A_BADGE_HEIGHT, 0, badge) ||
        badge[0] != sentinel)
    {
      printf("the F18A badge drew below row %u\n", (unsigned)PICO9918_F18A_BADGE_HEIGHT);
      return 1;
    }

    /* One badge row per OUTPUT line, odd lines included. An odd line re-reads the
       buffer rather than re-rendering, and what it holds is the row above, so the badge
       has to be drawn again there and the call has to say the buffer changed. */
    {
      PICO9918_PIXEL_T __aligned(4) out[BADGE_LINE_PIXELS];
      pico9918_scanline_params_t params = {BADGE_LINE_PIXELS, 240, false, 0};

      pico9918_set_chip(PICO9918_INST PICO9918_CHIP_F18A);
      pico9918_reset(PICO9918_INST_ONLY);
      pico9918_initialise_gfx_i(PICO9918_INST_ONLY);
      pico9918_write_register_value(PICO9918_INST TMS_REG_1, TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE);
      /* Otherwise an odd line reports a change because it was dimmed, and the badge's
         own contribution to that answer would go unchecked. */
      pico9918_config(PICO9918_INST_ONLY)[PICO9918_CONF_CRT_SCANLINES] = 0;

      for (uint32_t line = 0; line < PICO9918_F18A_BADGE_HEIGHT; ++line)
      {
        fillPixels(out, BADGE_LINE_PIXELS, sentinel);

        if (!pico9918_frame_output_line(PICO9918_INST line, &params, out))
        {
          printf("output line %u reported no change with the F18A badge on it\n",
                 (unsigned)line);
          return 1;
        }

        fillPixels(badge, BADGE_BUFFER, sentinel);
        pico9918_f18a_badge_render((uint16_t)line, 0, badge);

        for (uint32_t x = 0; x < PICO9918_F18A_BADGE_WIDTH; ++x)
        {
          if (out[x] != badge[x])
          {
            printf("output line %u pixel %u is 0x%04x, badge row %u has 0x%04x\n",
                   (unsigned)line, (unsigned)x, (unsigned)out[x], (unsigned)line,
                   (unsigned)badge[x]);
            return 1;
          }
        }
      }

      /* A PICO9918 shows its own splash instead, and that one is not here. */
      pico9918_set_chip(PICO9918_INST PICO9918_CHIP_PICO9918);
      fillPixels(out, BADGE_LINE_PIXELS, sentinel);
      pico9918_frame_output_line(PICO9918_INST 1, &params, out);

      pico9918_f18a_badge_render(1, 0, badge);
      if (out[2] == badge[2])
      {
        printf("a PICO9918 drew the F18A badge\n");
        return 1;
      }
    }

    printf("pico9918-core: the F18A badge is pixel-exact, one row per output line\n");
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

  /* The status file, read without the side effects of reading it. Two things to prove,
     because pico9918_read_status has neither: that it INDEXES rather than always
     answering SR0, and that it leaves the flags alone. */
  const uint8_t sr1Before = pico9918_status_value(PICO9918_INST PICO9918_SR_IDENT);
  pico9918_set_status(PICO9918_INST 0x45);

  if (pico9918_status_value(PICO9918_INST PICO9918_SR_STATUS) != 0x45)
  {
    printf("status_value did not read SR0 back: 0x%02x\n",
           pico9918_status_value(PICO9918_INST PICO9918_SR_STATUS));
    return 1;
  }

  /* SR1 must not have moved - if this always answered SR0 it would read 0x45 now */
  if (pico9918_status_value(PICO9918_INST PICO9918_SR_IDENT) != sr1Before)
  {
    printf("status_value ignores its register argument\n");
    return 1;
  }

  /* twice, unchanged: a destructive read would differ the second time */
  if (pico9918_status_value(PICO9918_INST PICO9918_SR_STATUS) != 0x45)
  {
    printf("status_value cleared what it returned\n");
    return 1;
  }

  /* and the destructive read still is destructive, so the two are really different */
  pico9918_read_status(PICO9918_INST_ONLY);
  if (pico9918_status_value(PICO9918_INST PICO9918_SR_STATUS) == 0x45)
  {
    printf("read_status left the flags standing\n");
    return 1;
  }

  printf("pico9918-core: status file readable without clearing it\n");

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

  /* The host callbacks, registered per instance - the reason this mode has them at all.
     A single shared registration passes the first check and fails the second, because
     registering on `second` would have overwritten the first instance's. */
  int firstTag = 0, secondTag = 0;
  pico9918_config_set_applied_callback(tms9918, appliedCallback, &firstTag);
  pico9918_config_set_applied_callback(second, appliedCallback, &secondTag);

  pico9918_config_apply(second);
  if (appliedSeen.calls != 1 || appliedSeen.inst != second || appliedSeen.userdata != &secondTag)
  {
    printf("the second instance's config-applied callback did not fire with its own "
           "instance and userdata\n");
    return 1;
  }

  pico9918_config_apply(tms9918);
  if (appliedSeen.calls != 2 || appliedSeen.inst != tms9918 || appliedSeen.userdata != &firstTag)
  {
    printf("registering on one instance disturbed the other's callback\n");
    return 1;
  }

  /* and a NULL registration is how a host withdraws one */
  pico9918_config_set_applied_callback(second, NULL, NULL);
  pico9918_config_apply(second);
  if (appliedSeen.calls != 2)
  {
    printf("a NULL registration still fired\n");
    return 1;
  }

  printf("pico9918-core: host callbacks registered per instance\n");

  pico9918_destroy(second);
  pico9918_destroy(PICO9918_INST_ONLY);
#endif
  return 0;
}
