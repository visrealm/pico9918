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
#define PICO9918_CONFIG_BYTES 256

/**
 * \brief the first config byte a guest may write through VR58/59
 *
 * Below it is the identity/version band the host stamps - see the ABI FREEZE note above.
 * The register path enforces this same boundary.
 */
#define PICO9918_CONFIG_FIRST_SETTABLE 8

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

/**
 * \brief PICO9918_CONF_PENDING_STATE values, and the state byte of a host's stored
 * pending record. A frozen ABI - the configurator reads it out of byte 200
 *
 * CONFIRMED -> PENDING (host saves) -> ARMED (host boots with it) -> CONFIRMED
 * (the user accepts, or the next boot reverts)
 */
typedef enum
{
  PICO9918_PENDING_STATE_CONFIRMED = 0xC0,
  PICO9918_PENDING_STATE_PENDING   = 0x9E,
  PICO9918_PENDING_STATE_ARMED     = 0xA0,
} pico9918_pending_state_t;

/**
 * \brief bytes in a pending record: the state, then one slot per tracked field
 *
 * LOAD-BEARING: a tracked field's slot is its pendingMirror less
 * PICO9918_CONF_PENDING_STATE, so a host's stored record and the in-RAM mirror band
 * are the same layout. pico9918_config_pending_capture() and _restore() are built on
 * it, as is every tool that writes the record, so claiming a new mirror byte means
 * naming it here.
 */
#define PICO9918_PENDING_RECORD_BYTES \
  (PICO9918_CONF_PENDING_CLOCK_PRESET - PICO9918_CONF_PENDING_STATE + 1)

/* -------------------------------------------------------------------------
 * Field descriptors
 * -------------------------------------------------------------------------
 * Drives validation, defaults, per-version migration, and the pending-block
 * mirror. Adding a field: append one row in pico9918_config.c.
 * Set pendingMirror to
 * PICO9918_PENDING_MIRROR_NONE for fields that don't participate in the display-change
 * confirmation flow (a host concept - the library only copies the bytes).
 */
/** \brief pendingMirror value for a field outside the confirmation flow */
#define PICO9918_PENDING_MIRROR_NONE 0xFF

/** \brief one config field's descriptor */
typedef struct
{
  uint8_t offset;        /**< the field's config byte index */
  uint8_t max;           /**< bounds-check is value > max */
  uint8_t defaultValue;  /**< what a reset or a migration writes */
  uint8_t pendingMirror; /**< PICO9918_CONF_PENDING_* offset, or PICO9918_PENDING_MIRROR_NONE */
  uint16_t introducedIn; /**< packed major(4) | minor(4) | patch(8) */
} pico9918_config_field_t;

/* The declarations below need C linkage under a C++ host, and this header carries no
   PICO9918_ macro to supply it - see the dependency rule at the top. */
#ifdef __cplusplus
extern "C"
{
#endif

/**
 * \brief the descriptor table. The host save path reads pendingMirror/max/
 * defaultValue from it
 */
PICO9918_DLLEXPORT_CONST const pico9918_config_field_t pico9918_config_fields[];

/** \brief how many rows pico9918_config_fields has */
PICO9918_DLLEXPORT_CONST const size_t pico9918_config_field_count;

/**
 * \brief the instance's PICO9918_CONFIG_BYTES settings block
 *
 * The same bytes pico9918_config_validate() checks and pico9918_config_apply_now() acts
 * on, so a host reads its stored block into this and applies it. Persistence stays
 * the host's - the library never reaches storage - and so does the decision to
 * write, since these are settings a user chose rather than VDP state.
 */
PICO9918_DLLEXPORT
uint8_t* pico9918_config(PICO9918_INST_ONLY_ARG);

/**
 * \brief write a complete, valid settings block: every field at its default
 *
 * What a host wants when it has nothing stored, and the reason it should not simply
 * zero the block: the field defaults happen to be zero today, but the palette's are
 * not, and applying the block unpacks those bytes into the live palette. A zeroed
 * block therefore renders black. This also sets the initialised marker that
 * pico9918_config_validate() looks for, so a block from here survives it untouched.
 *
 * The identity bytes at 0-3 are cleared with the rest; pico9918_config_validate() and
 * pico9918_config_prepare_save() are where a host's own identity is stamped in.
 */
PICO9918_DLLEXPORT
void pico9918_config_defaults(uint8_t config[PICO9918_CONFIG_BYTES]);

/**
 * \brief the identity bytes at 0-3, which only the host knows
 *
 * swVersion is packed major(4) | minor(4) as byte 2 stores it, so the running version
 * compared against the field table's introducedIn is (swVersion << 8) | swPatch. Host
 * version numbers arrive here and nowhere else - the library must never see a host's
 * version defines.
 */
typedef struct
{
  uint8_t picoModel;
  uint8_t hwVersion;
  uint8_t swVersion;
  uint8_t swPatch;
} pico9918_config_host_id_t;

/**
 * \brief validate a config block just read from host storage, and stamp \p id into it
 *
 * Resets the block to defaults if it is not this host's, is uninitialised, or holds an
 * out-of-range field; then defaults the fields introduced since the stored version.
 * Either way the identity bytes end up at \p id and the command bytes a host persisted
 * are cleared.
 *
 * Returns true if the block changed in a way the host should persist. A host running the
 * configurator protocol can ignore that: PICO9918_CONF_SAVE_FORCED is set on the same
 * path, which is the save request its GPU loop already dispatches.
 */
PICO9918_DLLEXPORT
bool pico9918_config_validate(uint8_t config[PICO9918_CONFIG_BYTES], pico9918_config_host_id_t id);

/**
 * \brief stamp \p id and the initialised marker into a block about to be persisted
 *
 * The marker is how pico9918_config_validate() tells a stored block from an erased one,
 * so a host that persists a block without this gets a factory reset on its next boot.
 * Host storage is untouched - this only prepares the bytes.
 */
PICO9918_DLLEXPORT
void pico9918_config_prepare_save(uint8_t config[PICO9918_CONFIG_BYTES], pico9918_config_host_id_t id);

/** \brief copy live tracked fields into the in-RAM pending mirror with the given state */
PICO9918_DLLEXPORT
void pico9918_config_refresh_pending_mirror(uint8_t config[PICO9918_CONFIG_BYTES], uint8_t state);

/**
 * \brief copy the live tracked fields into a PICO9918_PENDING_RECORD_BYTES record
 * \note  record[0], the state, is the caller's - only the field slots are written
 */
PICO9918_DLLEXPORT
void pico9918_config_pending_capture(const uint8_t config[PICO9918_CONFIG_BYTES], uint8_t* record);

/** \brief copy a pending record's field slots back over the live config */
PICO9918_DLLEXPORT
void pico9918_config_pending_restore(uint8_t config[PICO9918_CONFIG_BYTES], const uint8_t* record);

/**
 * \brief ask for the block to be applied at the next end of frame
 *
 * The deferred form, and what a device wants: the apply seeds registers and republishes
 * the palette, so doing it mid-frame would show on the line being scanned out. \p
 * applyVdpEffects asks for that reseeding; without it the apply runs its host-side and
 * derived effects only.
 *
 * A host that has no frame boundary to wait for wants pico9918_config_apply_now().
 */
PICO9918_DLLEXPORT
void pico9918_config_schedule_apply(PICO9918_INST_ARG bool applyVdpEffects);

/**
 * \brief apply the block now, and cancel any apply already owed
 *
 * For a host that has just written the block itself - a configurator front end, or a
 * consumer stepping the library a frame at a time - and would rather see the effects
 * than wait for a boundary it does not have. The deferred request is cleared, so the
 * next end of frame does not apply the same block a second time.
 *
 * Also where a host lands after pico9918_set_chip(), which schedules an apply rather than
 * performing one.
 */
PICO9918_DLLEXPORT
void pico9918_config_apply_now(PICO9918_INST_ARG bool applyVdpEffects);

/**
 * \brief register the host's config-applied hook
 *
 * Fires from the apply itself, which the frame module reaches where the
 * configDirty flag is actually consumed - the end-of-frame interrupt, not the
 * scanline body - so it is per-frame at worst and a function pointer is
 * permitted. It exists so a host's own apply effects stay in lockstep with the
 * library's register and palette effects, instead of the host having to watch
 * configDirty itself.
 *
 * Called LAST, after the VDP-side effects, and on every personality: a host effect
 * is the host's to gate, and one derived from a register has to read the value this
 * call may just have seeded. NULL (the default) means the host has no such effects
 * and nothing is called.
 *
 * Registered per instance in a multi-instance build - see pico9918.h for why the two
 * builds take different shapes.
 */
PICO9918_DLLEXPORT
void pico9918_config_set_applied_callback(PICO9918_INST_ARG pico9918_config_applied_fn cb, void* userdata);

#ifdef __cplusplus
}
#endif

#endif // _PICO9918_CONFIG_H
