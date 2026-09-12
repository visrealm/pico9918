/**
 * \file
 * \brief pico9918-core - the config block's validation, defaults and migration
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Nothing else covers these. The goldens and the scene suite both start from a block
 * that is already valid, so the paths that decide what "valid" means - the identity
 * check, the reset to defaults, and the per-version migration that decides which
 * fields a firmware upgrade re-defaults - have never been exercised by a test.
 *
 * The migration case is the one that matters: it is driven by a version number
 * compared against this library's own descriptor table, and the two have to come from
 * the same build to mean anything.
 */

#include "pico9918_config.h"

#include <stdio.h>
#include <string.h>

#define HW_V0_3 0x03
#define HW_V1_X 0x10
#define HW_V2_X 0x20

static int failures;

static void check(const char* what, unsigned wanted, unsigned got)
{
  if (wanted == got) return;
  ++failures;
  printf("  FAIL %s: want %02x got %02x\n", what, wanted, got);
}

/* A block as a host would have stored it, then aged back to the given version. There is
   no way to ask the library to stamp a version other than its own, which is the point -
   so the stamp is rewritten here, the way a unit running an older release left it. */
static void storedAt(uint8_t* config, uint8_t major, uint8_t minor, uint8_t patch)
{
  pico9918_config_defaults(config);
  pico9918_config_prepare_save(config, HW_V1_X);

  config[PICO9918_CONF_SW_VERSION]       = (uint8_t)((major << 4) | minor);
  config[PICO9918_CONF_SW_PATCH_VERSION] = patch;
}

/* The descriptor for a byte, so a case can assert against the table rather than against
   a literal that has to be kept in step with it. */
static const pico9918_config_field_t* field(uint8_t offset)
{
  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    if (pico9918_config_fields[i].offset == offset) return &pico9918_config_fields[i];
  }
  return NULL;
}

int main(void)
{
  uint8_t config[PICO9918_CONFIG_BYTES];
  const pico9918_config_field_t* base = field(PICO9918_CONF_VDP_BASE);

  if (!base)
  {
    printf("FAIL: no descriptor for PICO9918_CONF_VDP_BASE\n");
    return 1;
  }

  /* 1. a block this build just saved is returned untouched, with nothing to persist */
  {
    uint8_t before[PICO9918_CONFIG_BYTES];

    pico9918_config_defaults(config);
    pico9918_config_prepare_save(config, HW_V1_X);
    memcpy(before, config, sizeof(config));

    check("round-trip reports a change", 0, pico9918_config_validate(config, HW_V1_X));
    check("round-trip altered the block", 0, memcmp(before, config, sizeof(config)) != 0);
  }

  /* 2. an upgrade defaults the fields introduced since, and leaves the others alone */
  {
    storedAt(config, 1, 2, 0);
    config[PICO9918_CONF_VDP_BASE]      = 1;
    config[PICO9918_CONF_CRT_SCANLINES] = 1;

    check("upgrade reports no change", 1, pico9918_config_validate(config, HW_V1_X));
    check("upgrade left byte 15 unswept", base->defaultValue, config[PICO9918_CONF_VDP_BASE]);
    check("upgrade lost a settled field", 1, config[PICO9918_CONF_CRT_SCANLINES]);
    check("upgrade did not ask to be saved", 1, config[PICO9918_CONF_SAVE_FORCED]);
  }

  /* 3. the stamp is this build's own version, and no caller can claim otherwise. A block
        that says it is newer is still re-stamped, so a downgrade settles rather than
        migrating on every boot. */
  {
    storedAt(config, 1, 2, 0);
    pico9918_config_validate(config, HW_V1_X);
    check("upgrade stamped a foreign version", PICO9918_BUILD_SW_VERSION,
          config[PICO9918_CONF_SW_VERSION]);
    check("upgrade stamped a foreign patch", PICO9918_BUILD_SW_PATCH,
          config[PICO9918_CONF_SW_PATCH_VERSION]);

    storedAt(config, 9, 9, 9);
    check("downgrade reports no change", 1, pico9918_config_validate(config, HW_V1_X));
    check("downgrade left a newer stamp", PICO9918_BUILD_SW_VERSION,
          config[PICO9918_CONF_SW_VERSION]);
    check("downgrade migrates a second time", 0, pico9918_config_validate(config, HW_V1_X));
  }

  /* 4. the model is the board revision's, not something a caller states separately */
  {
    pico9918_config_defaults(config);
    pico9918_config_prepare_save(config, HW_V2_X);
    check("v2.x is not the PRO tier", PICO9918_MODEL_RP2350, config[PICO9918_CONF_PICO_MODEL]);
    check("v2.x lost its revision", HW_V2_X, config[PICO9918_CONF_HW_VERSION]);

    pico9918_config_defaults(config);
    pico9918_config_prepare_save(config, HW_V1_X);
    check("v1.x is not the RP2040", PICO9918_MODEL_RP2040, config[PICO9918_CONF_PICO_MODEL]);

    pico9918_config_defaults(config);
    pico9918_config_prepare_save(config, HW_V0_3);
    check("v0.3 is not the RP2040", PICO9918_MODEL_RP2040, config[PICO9918_CONF_PICO_MODEL]);
  }

  /* 5. a tier toggle corrects the identity and keeps the settings. One stored block has to
        survive it, or a host offering both loses the user's settings on every switch. */
  {
    pico9918_config_defaults(config);
    pico9918_config_prepare_save(config, HW_V2_X);
    config[PICO9918_CONF_CRT_SCANLINES] = 1;

    check("tier toggle asked to be persisted", 0, pico9918_config_validate(config, HW_V1_X));
    check("tier toggle lost a setting", 1, config[PICO9918_CONF_CRT_SCANLINES]);
    check("tier toggle left the model", PICO9918_MODEL_RP2040, config[PICO9918_CONF_PICO_MODEL]);
    check("tier toggle left the revision", HW_V1_X, config[PICO9918_CONF_HW_VERSION]);
  }

  /* 6. junk is still junk: the marker and the range checks are what reset a block */
  {
    pico9918_config_defaults(config);
    pico9918_config_prepare_save(config, HW_V1_X);
    config[PICO9918_CONF_CRT_SCANLINES] = 1;
    config[PICO9918_CONF_SCANLINE_SPRITES] = 0xff;

    check("out-of-range block kept", 1, pico9918_config_validate(config, HW_V1_X));
    check("out-of-range block not reset", 0, config[PICO9918_CONF_CRT_SCANLINES]);

    pico9918_config_defaults(config);
    pico9918_config_prepare_save(config, HW_V1_X);
    config[PICO9918_CONF_CRT_SCANLINES]      = 1;
    config[PICO9918_CONF_PALETTE_IDX_0 + 2] &= 0x0f;

    check("uninitialised block kept", 1, pico9918_config_validate(config, HW_V1_X));
    check("uninitialised block not reset", 0, config[PICO9918_CONF_CRT_SCANLINES]);
  }

  /* 7. a command byte read back from storage is not a command */
  {
    pico9918_config_defaults(config);
    pico9918_config_prepare_save(config, HW_V1_X);
    config[PICO9918_CONF_SAVE_TO_FLASH]   = 1;
    config[PICO9918_CONF_PENDING_CONFIRM] = 1;

    pico9918_config_validate(config, HW_V1_X);
    check("stored save command survived", 0, config[PICO9918_CONF_SAVE_TO_FLASH]);
    check("stored confirm command survived", 0, config[PICO9918_CONF_PENDING_CONFIRM]);
  }

  /* 8. no field may be stamped later than the build that carries it. A firmware upgrade
        never writes the settings block, so migration is the only thing that brings a new
        field to its default - and one stamped ahead of this version passes that test on
        every boot, re-defaulting itself and discarding whatever the user chose. */
  {
    const uint16_t running =
      ((uint16_t)PICO9918_BUILD_SW_VERSION << 8) | PICO9918_BUILD_SW_PATCH;

    for (size_t i = 0; i < pico9918_config_field_count; ++i)
    {
      if (pico9918_config_fields[i].introducedIn <= running) continue;
      ++failures;
      printf("  FAIL byte %u is stamped %04x, ahead of this build's %04x\n",
             pico9918_config_fields[i].offset, pico9918_config_fields[i].introducedIn, running);
    }
  }

  printf("%s: config validation, defaults and migration, %d failure(s)\n",
         failures ? "FAIL" : "PASS", failures);
  return failures != 0;
}
