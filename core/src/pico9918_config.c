/**
 * \file
 * \brief pico9918-core - Config semantics
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Portable half of the config block: the field descriptor table, the
 * validation / defaults / migration driver, the pending mirror refresh, and
 * the VDP-side apply. Host storage (flash), host detection (SCART, hardware
 * version) and host-side apply effects (VGA scanlines) live in the host.
 */

#include "impl/pico9918_priv.h"

#include <string.h>

const pico9918_config_field_t pico9918_config_fields[] = {
  {PICO9918_CONF_CRT_SCANLINES, 1, 0, PICO9918_PENDING_MIRROR_NONE, 0x1000},
  {PICO9918_CONF_SCANLINE_SPRITES, 3, 0, PICO9918_PENDING_MIRROR_NONE, 0x1000},
  {PICO9918_CONF_CLOCK_PRESET_ID, 2, 0, PICO9918_CONF_PENDING_CLOCK_PRESET, 0x1000},
  {PICO9918_CONF_SCART_MODE, 1, 0, PICO9918_CONF_PENDING_SCART_MODE, 0x1200},
  {PICO9918_CONF_VDP_DEVICE, 3, 0, PICO9918_PENDING_MIRROR_NONE, 0x1101},
  {PICO9918_CONF_DISP_DRIVER_PREF, 2, 0, PICO9918_CONF_PENDING_DRIVER_PREF, 0x1200}, // 1.2.0
  {PICO9918_CONF_VGA_MODE, 0, 0, PICO9918_CONF_PENDING_VGA_MODE, 0x1200},            // 0=480p60 (only)
  {PICO9918_CONF_DIAG_REGISTERS, 1, 0, PICO9918_PENDING_MIRROR_NONE, 0x1000},
  {PICO9918_CONF_DIAG_PERFORMANCE, 1, 0, PICO9918_PENDING_MIRROR_NONE, 0x1000},
  {PICO9918_CONF_DIAG_PALETTE, 1, 0, PICO9918_PENDING_MIRROR_NONE, 0x1000},
  {PICO9918_CONF_DIAG_ADDRESS, 1, 0, PICO9918_PENDING_MIRROR_NONE, 0x1000},
  // WARNING: VR58/59 lets a host write any byte >= 8, so shipped units may carry residue in byte 15.
  // migrateNewFields compares strictly, so the stamp must be the release that first CLAIMS the byte:
  // an earlier one leaves a unit already on that version unswept, booting on residue.
  {PICO9918_CONF_VDP_BASE, 1, PICO9918_BASE_TMS9918, PICO9918_PENDING_MIRROR_NONE, 0x1300},
};

const size_t pico9918_config_field_count = sizeof(pico9918_config_fields) / sizeof(pico9918_config_fields[0]);

void pico9918_config_refresh_pending_mirror(uint8_t config[PICO9918_CONFIG_BYTES], uint8_t state)
{
  config[PICO9918_CONF_PENDING_STATE] = state;
  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    if (pico9918_config_fields[i].pendingMirror == PICO9918_PENDING_MIRROR_NONE) continue;
    config[pico9918_config_fields[i].pendingMirror] = config[pico9918_config_fields[i].offset];
  }
}

void pico9918_config_pending_capture(const uint8_t config[PICO9918_CONFIG_BYTES], uint8_t* record)
{
  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    if (pico9918_config_fields[i].pendingMirror == PICO9918_PENDING_MIRROR_NONE) continue;
    record[pico9918_config_fields[i].pendingMirror - PICO9918_CONF_PENDING_STATE] =
      config[pico9918_config_fields[i].offset];
  }
}

void pico9918_config_pending_restore(uint8_t config[PICO9918_CONFIG_BYTES], const uint8_t* record)
{
  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    if (pico9918_config_fields[i].pendingMirror == PICO9918_PENDING_MIRROR_NONE) continue;
    config[pico9918_config_fields[i].offset] =
      record[pico9918_config_fields[i].pendingMirror - PICO9918_CONF_PENDING_STATE];
  }
}

PICO9918_INLINE_HOT uint16_t configStoredVersion(const uint8_t* config)
{
  return ((uint16_t)config[PICO9918_CONF_SW_VERSION] << 8) | config[PICO9918_CONF_SW_PATCH_VERSION];
}

/* This build's version in the packing the block stores, so one comparison serves both the
   stored version and the field table's introducedIn. */
#define CONFIG_RUNNING_VERSION (((uint16_t)PICO9918_BUILD_SW_VERSION << 8) | PICO9918_BUILD_SW_PATCH)

/* Board revision major 2 and up is the PRO tier, which is the RP2350 - see the encoding
   pico9918_config_validate() documents. */
static uint8_t configPicoModel(uint8_t hwVersion)
{
  return hwVersion >= 0x20 ? PICO9918_MODEL_RP2350 : PICO9918_MODEL_RP2040;
}

static bool configOutOfRange(const uint8_t* config)
{
  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    if (config[pico9918_config_fields[i].offset] > pico9918_config_fields[i].max) return true;
  }
  return false;
}

void pico9918_config_defaults(uint8_t config[PICO9918_CONFIG_BYTES])
{
  memset(config, 0, PICO9918_CONFIG_BYTES);

  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    config[pico9918_config_fields[i].offset] = pico9918_config_fields[i].defaultValue;
  }

  /* entry 0 stays zero; the rest carry the 0xf alpha the validator reads as "initialised" */
  for (int i = 1; i < 16; ++i)
  {
    uint16_t rgb                                      = 0xf000 | pico9918_default_palette(i);
    config[PICO9918_CONF_PALETTE_IDX_0 + (i * 2)]     = rgb >> 8;
    config[PICO9918_CONF_PALETTE_IDX_0 + (i * 2) + 1] = rgb & 0xff;
  }
}


// apply defaults only for fields introduced after storedVer
static void migrateNewFields(uint8_t* config, uint16_t storedVer)
{
  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    if (pico9918_config_fields[i].introducedIn > storedVer)
    {
      config[pico9918_config_fields[i].offset] = pico9918_config_fields[i].defaultValue;
    }
  }
}

bool pico9918_config_validate(uint8_t config[PICO9918_CONFIG_BYTES], uint8_t hwVersion)
{
  const uint8_t picoModel = configPicoModel(hwVersion);
  uint16_t storedVer      = configStoredVersion(config);

  if (config[PICO9918_CONF_PICO_MODEL] != picoModel || config[PICO9918_CONF_PALETTE_IDX_0] != 0x00 ||
      (config[PICO9918_CONF_PALETTE_IDX_0 + 2] & 0xf0) != 0xf0 || // not initialised
      configOutOfRange(config))
  {
    pico9918_config_defaults(config);

    storedVer = 0; // a defaulted block stamps and saves like a version change
  }

  config[PICO9918_CONF_PICO_MODEL] = picoModel;
  config[PICO9918_CONF_HW_VERSION] = hwVersion;

  // the host persists all 256 bytes; clear command bytes read back from storage
  config[PICO9918_CONF_SAVE_FORCED]     = 0;
  config[PICO9918_CONF_PENDING_CANCEL]  = 0;
  config[PICO9918_CONF_PENDING_CONFIRM] = 0;
  config[PICO9918_CONF_SAVE_TO_FLASH]   = 0;

  if (storedVer == CONFIG_RUNNING_VERSION) return false;

  migrateNewFields(config, storedVer);

  config[PICO9918_CONF_SW_VERSION]       = PICO9918_BUILD_SW_VERSION;
  config[PICO9918_CONF_SW_PATCH_VERSION] = PICO9918_BUILD_SW_PATCH;

  /* forced, not pending-split: a migration is not a display change the user chose */
  config[PICO9918_CONF_SAVE_FORCED] = 1;
  return true;
}

void pico9918_config_prepare_save(uint8_t config[PICO9918_CONFIG_BYTES], uint8_t hwVersion)
{
  config[PICO9918_CONF_PICO_MODEL]       = configPicoModel(hwVersion);
  config[PICO9918_CONF_HW_VERSION]       = hwVersion;
  config[PICO9918_CONF_SW_VERSION]       = PICO9918_BUILD_SW_VERSION;
  config[PICO9918_CONF_SW_PATCH_VERSION] = PICO9918_BUILD_SW_PATCH;

  /* the initialised marker: entry 0 always 0, the rest carrying alpha 0xf */
  config[PICO9918_CONF_PALETTE_IDX_0]     = 0;
  config[PICO9918_CONF_PALETTE_IDX_0 + 1] = 0;
  for (int i = 1; i < 16; ++i)
  {
    config[PICO9918_CONF_PALETTE_IDX_0 + (i * 2)] |= 0xf0;
  }
}

uint8_t* pico9918_config(PICO9918_INST_ONLY_ARG)
{
  return tms9918->config;
}

/* host config-applied hook - see the header for the contract, and pico9918.h for why only
   the storage differs between the two builds */
#if PICO9918_SINGLE_INSTANCE
static struct
{
  pico9918_config_applied_fn fn;
  void* userdata;
} configApplied;
#define CONFIG_APPLIED_CB configApplied
#else
#define CONFIG_APPLIED_CB tms9918->configApplied
#endif

void pico9918_config_set_applied_callback(PICO9918_INST_ARG pico9918_config_applied_fn cb, void* userdata)
{
  CONFIG_APPLIED_CB.fn       = cb;
  CONFIG_APPLIED_CB.userdata = userdata;
}

/* `tms9918` names the parameter in one build and the global instance in the other, so the
   callback is handed its instance either way */
static inline void configAppliedFire(PICO9918_INST_ONLY_ARG)
{
  if (CONFIG_APPLIED_CB.fn) CONFIG_APPLIED_CB.fn(tms9918, CONFIG_APPLIED_CB.userdata);
}

void pico9918_config_apply(PICO9918_INST_ONLY_ARG)
{
  /* the overlays are the personality's: panels nothing will draw are not a summary */
  tms9918->config[PICO9918_CONF_DIAG] =
    PICO9918_HAS(tms9918, PICO9918_FEAT_OVERLAY) &&
    (tms9918->config[PICO9918_CONF_DIAG_ADDRESS] || tms9918->config[PICO9918_CONF_DIAG_PALETTE] ||
     tms9918->config[PICO9918_CONF_DIAG_PERFORMANCE] || tms9918->config[PICO9918_CONF_DIAG_REGISTERS]);

  if (!PICO9918_HAS(tms9918, PICO9918_FEAT_CONFIG))
  {
    /* an F18A's registers, palette and render base are not a settings block's */
    tms9918->configVdpDirty = false;
    tms9918->vdpBase        = PICO9918_BASE_TMS9918;
  }
  else
  {
    if (tms9918->configVdpDirty)
    {
      tms9918->configVdpDirty = false;

      if (tms9918->config[PICO9918_CONF_CRT_SCANLINES])
        TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) |= PICO9918_R50_VSCANLINES;
      else
        TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) &= (uint8_t)~PICO9918_R50_VSCANLINES;

      TMS_REGISTER(tms9918, PICO9918_REG_MAX_SCAN_SPRITES) =
        1 << (tms9918->config[PICO9918_CONF_SCANLINE_SPRITES] + 2);

      for (int i = 0; i < 16; ++i)
      {
        uint16_t rgb = (tms9918->config[PICO9918_CONF_PALETTE_IDX_0 + (i * 2)] << 8) |
                       tms9918->config[PICO9918_CONF_PALETTE_IDX_0 + (i * 2) + 1];
        tms9918->vram.map.pram[i] = __builtin_bswap16(rgb);
      }
      tms9918->palDirty = 1;
    }

    tms9918->vdpBase = (tms9918->config[PICO9918_CONF_VDP_BASE] == PICO9918_BASE_V9938)
                         ? PICO9918_BASE_V9938
                         : PICO9918_BASE_TMS9918;
  }

  /* last, so the host derives its effects from registers this call may just have seeded */
  configAppliedFire(PICO9918_INST_ONLY);
}

void pico9918_config_schedule_apply(PICO9918_INST_ARG bool applyVdpEffects)
{
  tms9918->configDirty = true;
  if (applyVdpEffects) tms9918->configVdpDirty = true;
}

void pico9918_config_apply_now(PICO9918_INST_ARG bool applyVdpEffects)
{
  if (applyVdpEffects) tms9918->configVdpDirty = true;
  pico9918_config_apply(PICO9918_INST_ONLY);

  /* the boundary must not apply the same block again */
  tms9918->configDirty = false;
}
