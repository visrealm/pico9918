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
  {PICO9918_CONF_CRT_SCANLINES, 1, 0, PENDING_MIRROR_NONE, 0x1000},
  {PICO9918_CONF_SCANLINE_SPRITES, 3, 0, PENDING_MIRROR_NONE, 0x1000},
  {PICO9918_CONF_CLOCK_PRESET_ID, 2, 0, PICO9918_CONF_PENDING_CLOCK_PRESET, 0x1000},
  {PICO9918_CONF_SCART_MODE, 1, 0, PICO9918_CONF_PENDING_SCART_MODE, 0x1200},
  // max/default track the host's VdpDevice enum (pico9918 src/config.h): the
  // pin-behaviour variant is host policy, so only its range lives here
  {PICO9918_CONF_VDP_DEVICE, 3, 0, PENDING_MIRROR_NONE, 0x1101},
  {PICO9918_CONF_DISP_DRIVER_PREF, 2, 0, PICO9918_CONF_PENDING_DRIVER_PREF, 0x1200}, // 1.2.0
  {PICO9918_CONF_VGA_MODE, 0, 0, PICO9918_CONF_PENDING_VGA_MODE, 0x1200},            // 0=480p60 (only)
  {PICO9918_CONF_DIAG_REGISTERS, 1, 0, PENDING_MIRROR_NONE, 0x1000},
  {PICO9918_CONF_DIAG_PERFORMANCE, 1, 0, PENDING_MIRROR_NONE, 0x1000},
  {PICO9918_CONF_DIAG_PALETTE, 1, 0, PENDING_MIRROR_NONE, 0x1000},
  {PICO9918_CONF_DIAG_ADDRESS, 1, 0, PENDING_MIRROR_NONE, 0x1000},
  // byte 15 was never a declared option, but VR58/59 lets host software write any
  // byte >= 8, so shipped units may carry residue there. The stamp must be the first
  // release that CLAIMS the byte, and migrateNewFields compares strictly: stamping an
  // earlier release leaves a unit already on that version unswept, and residue boots
  // it into the V9938 render base. This claim ships in 1.3.0.
  {PICO9918_CONF_VDP_BASE, 1, PICO9918_BASE_TMS9918, PENDING_MIRROR_NONE, 0x1300},
};

const size_t pico9918_config_field_count = sizeof(pico9918_config_fields) / sizeof(pico9918_config_fields[0]);

void pico9918_config_refresh_pending_mirror(uint8_t config[CONFIG_BYTES], uint8_t state)
{
  config[PICO9918_CONF_PENDING_STATE] = state;
  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    if (pico9918_config_fields[i].pendingMirror == PENDING_MIRROR_NONE) continue;
    config[pico9918_config_fields[i].pendingMirror] = config[pico9918_config_fields[i].offset];
  }
}

PICO9918_INLINE_HOT uint16_t configStoredVersion(const uint8_t* config)
{
  return ((uint16_t)config[PICO9918_CONF_SW_VERSION] << 8) | config[PICO9918_CONF_SW_PATCH_VERSION];
}

static bool configOutOfRange(const uint8_t* config)
{
  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    if (config[pico9918_config_fields[i].offset] > pico9918_config_fields[i].max) return true;
  }
  return false;
}

static void applyConfigDefaults(uint8_t* config)
{
  for (size_t i = 0; i < pico9918_config_field_count; ++i)
  {
    config[pico9918_config_fields[i].offset] = pico9918_config_fields[i].defaultValue;
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

bool pico9918_config_validate(uint8_t config[CONFIG_BYTES], bool modelMatches, uint16_t currentVerFull,
                            bool* wasReset)
{
  uint16_t storedVer = configStoredVersion(config);

  if (wasReset) *wasReset = false;

  if (!modelMatches || config[PICO9918_CONF_PALETTE_IDX_0] != 0x00 ||
      (config[PICO9918_CONF_PALETTE_IDX_0 + 2] & 0xf0) != 0xf0 || // not initialised
      configOutOfRange(config))
  {
    memset(config, 0, CONFIG_BYTES);

    applyConfigDefaults(config);

    config[PICO9918_CONF_PALETTE_IDX_0]     = 0;
    config[PICO9918_CONF_PALETTE_IDX_0 + 1] = 0;
    for (int i = 1; i < 16; ++i)
    {
      uint16_t rgb                             = 0xf000 | pico9918_default_palette(i);
      config[PICO9918_CONF_PALETTE_IDX_0 + (i * 2)]     = rgb >> 8;
      config[PICO9918_CONF_PALETTE_IDX_0 + (i * 2) + 1] = rgb & 0xff;
    }

    storedVer = 0; // force version stamp + save by the caller
    if (wasReset) *wasReset = true;
  }

  // the host persists all 256 bytes; clear command bytes read back from storage
  config[PICO9918_CONF_SAVE_FORCED]     = 0;
  config[PICO9918_CONF_PENDING_CANCEL]  = 0;
  config[PICO9918_CONF_PENDING_CONFIRM] = 0;
  config[PICO9918_CONF_SAVE_TO_FLASH]   = 0;

  if (storedVer == currentVerFull) return false;

  migrateNewFields(config, storedVer);
  return true;
}

uint8_t* pico9918_config(PICO9918_INST_ONLY_ARG)
{
  return tms9918->config;
}

/* host config-applied hook - see the header for the contract */
static void (*configAppliedCallback)(void) = NULL;

void pico9918_config_set_applied_callback(void (*cb)(void))
{
  configAppliedCallback = cb;
}

void pico9918_config_apply(PICO9918_INST_ONLY_ARG)
{
  /* first, so the host's own apply effects land where its former applyConfig()
     put them: ahead of the VDP-side effects below */
  if (configAppliedCallback) configAppliedCallback();

  /* the rest of the configuration lives in VDP state, which is a running program's
     to own: seed it at boot and on an explicit reset, not on every option write */
  if (tms9918->configVdpDirty)
  {
    tms9918->configVdpDirty = false;

    if (tms9918->config[PICO9918_CONF_CRT_SCANLINES])
      TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) |= 0x04;
    else
      TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) &= ~0x04;

    TMS_REGISTER(tms9918, PICO9918_REG_MAX_SCAN_SPRITES) = 1 << (tms9918->config[PICO9918_CONF_SCANLINE_SPRITES] + 2);

    for (int i = 0; i < 16; ++i)
    {
      uint16_t rgb = (tms9918->config[PICO9918_CONF_PALETTE_IDX_0 + (i * 2)] << 8) |
                     tms9918->config[PICO9918_CONF_PALETTE_IDX_0 + (i * 2) + 1];
      tms9918->vram.map.pram[i] = __builtin_bswap16(rgb);
    }
    tms9918->palDirty = 1;
  }

  tms9918->config[PICO9918_CONF_DIAG] = tms9918->config[PICO9918_CONF_DIAG_ADDRESS] || tms9918->config[PICO9918_CONF_DIAG_PALETTE] ||
                               tms9918->config[PICO9918_CONF_DIAG_PERFORMANCE] || tms9918->config[PICO9918_CONF_DIAG_REGISTERS];

  /* The only writer of the render base after construction. The config byte can change
     under a sanitize or a host write, so the renderer sees it only from here. */
  tms9918->vdpBase = (tms9918->config[PICO9918_CONF_VDP_BASE] == PICO9918_BASE_V9938)
                       ? PICO9918_BASE_V9938
                       : PICO9918_BASE_TMS9918;
}
