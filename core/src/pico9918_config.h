/**
 * \file
 * \brief pico9918-core - config byte layout
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Purpose: THE single authoritative layout of the 256-byte config block,
 * plus the portable semantics over it (field descriptors, validation,
 * defaults, per-version migration, and the VDP-side apply).
 *
 * This header owns the config-byte ABI. It is shared by:
 *   - the library (core reset/write paths, GPU config-action keys)
 *   - the PICO9918 firmware (flash storage, apply, validation)
 *   - the configurator (generated from this header)
 *
 * Nothing else may declare a PICO9918_CONF_* byte index. If a new byte is needed, it
 * is claimed here and nowhere else.
 *
 * This header must stay free of host dependencies - no PICO9918_* macros, no
 * board headers, no SDK includes. Version numbers are host-owned and always
 * arrive as parameters, never as compile-time macros.
 *
 * -------------------------------------------------------------------------
 * ABI FREEZE
 * -------------------------------------------------------------------------
 * The following bytes are deployed in flash on real units. Their indices are
 * frozen and must NEVER be reassigned to a different meaning - doing so makes
 * existing units come up with corrupted settings:
 *
 *   0-6, 8-14, 16-20, 128-160, 200-204, 252-255
 *
 * Free for future claims: 7, 15 (claimed below), 21-127, 161-199, 205-245.
 *
 * Byte 7 is deliberately left unused: it falls in the "not settable via
 * registers" identity/version band (0-6) that the configurator may treat as
 * reserved, and the firmware's version-match load path does not clear it.
 *
 * -------------------------------------------------------------------------
 * CONFIGURATOR MENU-SENTINEL BAND: 246-255
 * -------------------------------------------------------------------------
 * The configurator uses 246-255 as menu-sentinel IDs in the SAME numeric
 * space as these config indices (CONF_MENU_OUTPUT = 246 .. CONF_MENU_EMPTY =
 * 255). The overlap at 252-255 is deliberate and already shipping. Do not
 * claim 246-251 for a real config byte without first checking the
 * configurator's sentinel list - a collision there breaks menu dispatch.
 */

#ifndef _PICO9918_CONFIG_H
#define _PICO9918_CONFIG_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* for PICO9918_INST_ONLY_ARG (single- vs multi-instance calling convention) */
#include "pico9918.h"

/** \brief size of the config block, in bytes */
#define CONFIG_BYTES 256

/** \brief vdpBase values - the render base selected by PICO9918_CONF_VDP_BASE */
#define PICO9918_BASE_TMS9918 0x00 /**< the TMS9918A base, which the F18A unlock extends */
#define PICO9918_BASE_V9938   0x01 /**< the V9938 base */

/** \brief every claimed config byte, by index. The values are a frozen ABI */
typedef enum
{
  // not settable via registers
  PICO9918_CONF_PICO_MODEL       = 0,
  PICO9918_CONF_HW_VERSION       = 1,
  PICO9918_CONF_SW_VERSION       = 2,
  PICO9918_CONF_SW_PATCH_VERSION = 3,
  PICO9918_CONF_CLOCK_TESTED     = 4,
  PICO9918_CONF_DISP_DRIVER      = 5,
  PICO9918_CONF_FLASH_STATUS     = 6,

  // 7: free (see ABI FREEZE note above)

  // settable via registers
  PICO9918_CONF_CRT_SCANLINES    = 8,
  PICO9918_CONF_SCANLINE_SPRITES = 9,
  PICO9918_CONF_CLOCK_PRESET_ID  = 10,
  PICO9918_CONF_SCART_MODE       = 11, // 0 = PAL 576i (default), 1 = NTSC 480i
  PICO9918_CONF_VDP_DEVICE       = 12, // emulated host-clock variant (GROMCLK/CPUCLK pins)
  PICO9918_CONF_DISP_DRIVER_PREF = 13, // 0 = AUTO (detect dongle), 1 = force VGA, 2 = force SCART
  PICO9918_CONF_VGA_MODE         = 14, // 0 = 480p60 (extensible)
  PICO9918_CONF_VDP_BASE         = 15, // render base: PICO9918_BASE_TMS9918 / _V9938

  PICO9918_CONF_DIAG             = 16,
  PICO9918_CONF_DIAG_REGISTERS   = 17,
  PICO9918_CONF_DIAG_PERFORMANCE = 18,
  PICO9918_CONF_DIAG_PALETTE     = 19,
  PICO9918_CONF_DIAG_ADDRESS     = 20,

  // 21-127: free

  PICO9918_CONF_PALETTE_IDX_0  = 128,
  PICO9918_CONF_PALETTE_IDX_15 = PICO9918_CONF_PALETTE_IDX_0 + 32, // 16x 2 bytes

  // 161-199: free

  // pending-block mirror (read by configurator)
  PICO9918_CONF_PENDING_STATE        = 200,
  PICO9918_CONF_PENDING_DRIVER_PREF  = 201,
  PICO9918_CONF_PENDING_VGA_MODE     = 202,
  PICO9918_CONF_PENDING_SCART_MODE   = 203,
  PICO9918_CONF_PENDING_CLOCK_PRESET = 204,

  // 205-245: free (246-251 only after checking the configurator sentinels)

  // commands (configurator writes 1 to trigger)
  PICO9918_CONF_SAVE_FORCED     = 252,
  PICO9918_CONF_PENDING_CANCEL  = 253,
  PICO9918_CONF_PENDING_CONFIRM = 254,
  PICO9918_CONF_SAVE_TO_FLASH   = 255,
} pico9918_config_option_t;

/* -------------------------------------------------------------------------
 * Field descriptors
 * -------------------------------------------------------------------------
 * Drives validation, defaults, per-version migration, and the pending-block
 * mirror. Adding a field: append one row in pico9918_config.c.
 * Set pendingMirror to
 * PENDING_MIRROR_NONE for fields that don't participate in the display-change
 * confirmation flow (a host concept - the library only copies the bytes).
 */
/** \brief pendingMirror value for a field outside the confirmation flow */
#define PENDING_MIRROR_NONE 0xFF

/** \brief one config field's descriptor */
typedef struct
{
  uint8_t offset;        /**< the field's config byte index */
  uint8_t max;           /**< bounds-check is value > max */
  uint8_t defaultValue;  /**< what a reset or a migration writes */
  uint8_t pendingMirror; /**< PICO9918_CONF_PENDING_* offset, or PENDING_MIRROR_NONE */
  uint16_t introducedIn; /**< packed major(4) | minor(4) | patch(8) */
} pico9918_config_field_t;

/**
 * \brief the descriptor table. The host save path reads pendingMirror/max/
 * defaultValue from it
 */
extern const pico9918_config_field_t pico9918_config_fields[];

/** \brief how many rows pico9918_config_fields has */
extern const size_t pico9918_config_field_count;

/**
 * \brief the instance's CONFIG_BYTES settings block
 *
 * The same bytes pico9918_config_validate() checks and pico9918_config_apply() acts
 * on, so a host reads its stored block into this and applies it. Persistence stays
 * the host's - the library never reaches storage - and so does the decision to
 * write, since these are settings a user chose rather than VDP state.
 */
uint8_t* pico9918_config(PICO9918_INST_ONLY_ARG);

/**
 * \brief validate a config block just read from host storage
 *
 * currentVerFull is the host's running firmware version, packed as
 * major(4) | minor(4) | patch(8) - the same encoding as introducedIn. It is a
 * parameter, not a macro: the library must never see host version defines.
 *
 * Returns true if the host must stamp currentVerFull into the block and
 * persist it - i.e. the stored version differed (a full reset forces this by
 * treating the stored version as 0).
 *
 * modelMatches lets the host fold its own "is this block mine" test (pico
 * model, etc.) into the same decision. wasReset, when non-NULL, reports
 * whether the block was cleared and defaulted - on that path every byte is
 * zeroed, so the host must re-stamp its own identity bytes.
 */
bool pico9918_config_validate(uint8_t config[CONFIG_BYTES], bool modelMatches, uint16_t currentVerFull,
                            bool* wasReset);

/** \brief copy live tracked fields into the in-RAM pending mirror with the given state */
void pico9918_config_refresh_pending_mirror(uint8_t config[CONFIG_BYTES], uint8_t state);

/**
 * \brief apply the config block's VDP-side effects: registers 50 and 30, the
 * palette unpack, and the derived PICO9918_CONF_DIAG summary byte
 *
 * Host-side effects (e.g. the VGA scanlines flag) stay with the host.
 */
void pico9918_config_apply(PICO9918_INST_ONLY_ARG);

/**
 * \brief register the host's config-applied hook
 *
 * Fires from pico9918_config_apply(), which the frame module calls where the
 * configDirty flag is actually consumed - the end-of-frame interrupt, not the
 * scanline body - so it is per-frame at worst and a function pointer is
 * permitted. It exists so the host's own apply effects (writing its VGA
 * scanlines flag from the same config byte the library folds into R50) stay in
 * lockstep with the library's register and palette effects, instead of the host
 * having to watch configDirty itself.
 *
 * Called FIRST, before any of the VDP-side effects. NULL (the default) means the
 * host has no such effects and nothing is called.
 */
void pico9918_config_set_applied_callback(void (*cb)(void));

#endif // _PICO9918_CONFIG_H
