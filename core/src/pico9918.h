/**
 * \file
 * \brief pico9918-core - core interface
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 */

#ifndef _PICO9918_H
#define _PICO9918_H

/* ------------------------------------------------------------------
 * LINKAGE MODES:
 *
 * Default (nothing defined):    using pico9918-core as a DLL
 * PICO9918_COMPILING_DLL:       compiling pico9918-core as a DLL
 * PICO9918_STATIC:              linking pico9918-core statically
 */

/* C linkage under a C++ consumer, plain extern under C. Every mode below carries it: a
   __declspec on its own leaves the name mangled, so a C++ host links against nothing. */
#ifdef __cplusplus
#define PICO9918_LINKAGE extern "C"
#else
#define PICO9918_LINKAGE extern
#endif

#if __EMSCRIPTEN__
#include <emscripten.h>
#define PICO9918_DLLEXPORT       EMSCRIPTEN_KEEPALIVE PICO9918_LINKAGE
#define PICO9918_DLLEXPORT_CONST PICO9918_LINKAGE
#elif PICO9918_COMPILING_DLL
#define PICO9918_DLLEXPORT PICO9918_LINKAGE __declspec(dllexport)
#elif defined WIN32 && !defined PICO9918_STATIC
#define PICO9918_DLLEXPORT PICO9918_LINKAGE __declspec(dllimport)
#else
/** \brief the linkage every public entry point carries - see LINKAGE MODES above */
#define PICO9918_DLLEXPORT PICO9918_LINKAGE
#endif

#ifndef PICO9918_DLLEXPORT_CONST
#define PICO9918_DLLEXPORT_CONST PICO9918_DLLEXPORT
#endif

/* INST_ONLY_ARG is `void`, not empty: an empty parameter list is a declaration
 * without a prototype, which clang rejects under -Wstrict-prototypes and C23
 * gives a different meaning. INST_ARG stays empty - it is always followed by
 * real parameters. */
#if PICO9918_SINGLE_INSTANCE
#define PICO9918_INST_ARG           /**< declare the instance ahead of other parameters */
#define PICO9918_INST_ONLY_ARG void /**< declare the instance as the only parameter */
#define PICO9918_INST               /**< pass the instance ahead of other arguments */
#define PICO9918_INST_ONLY          /**< pass the instance as the only argument */
#else
#define PICO9918_INST_ARG      pico9918_t *tms9918, /**< declare the instance ahead of other parameters */
#define PICO9918_INST_ONLY_ARG pico9918_t* tms9918  /**< declare the instance as the only parameter */
#define PICO9918_INST          tms9918,             /**< pass the instance ahead of other arguments */
#define PICO9918_INST_ONLY     tms9918              /**< pass the instance as the only argument */
#endif


#include <stdint.h>
#include <stdbool.h>

#include "pico9918_build_config.h"

/** \brief a VDP instance. Opaque: the layout is private to the library */
struct pico9918_s;
typedef struct pico9918_s pico9918_t;

/** \brief the display modes the VDP can be in, TMS9918A modes and F18A alike */
typedef enum
{
  TMS_MODE_GRAPHICS_I,
  TMS_MODE_GRAPHICS_II,
  TMS_MODE_TEXT,
  TMS_MODE_MULTICOLOR,
  TMS_MODE_TEXT80,
#ifdef PICO9918_V9938_BASE /* V9938 base scaffold (additive to the F18A build) */
  TMS_MODE_V9938_G3,
  TMS_MODE_V9938_G4,
  TMS_MODE_V9938_G5,
  TMS_MODE_V9938_G6,
  TMS_MODE_V9938_G7,
#endif
  TMS_MODE_COUNT,
} pico9918_mode_t;

#if PICO9918_BUILD_RUNTIME_CHIP

/**
 * \brief which chip an instance answers as
 *
 * A strict capability ladder: each personality is the one below it plus what the real
 * hardware adds, so one value orders them all.
 *
 *   TMS9918A  the base. The unlock write is refused, so the register file stays eight
 *             wide, there is no GPU to start, and the enhanced renderer folds away
 *             exactly as it does on a locked device.
 *   F18A      unlockable: the full register file, the enhanced modes and the GPU. None
 *             of the PICO9918's own extensions - a real F18A has no config port and no
 *             overlays - and it identifies as a real one in SR1.
 *   PICO9918  an F18A plus this board's extensions: the VR58/59 config port, the
 *             firmware-update register, and the splash and diagnostics overlays.
 *
 * Declared only where the library was built PICO9918_RUNTIME_CHIP=ON, which a board
 * does not: what the build fixes either way is the memory map, and a firmware that is
 * one chip has nothing to select. See PICO9918_BUILD_RUNTIME_CHIP.
 */
typedef enum
{
  PICO9918_CHIP_TMS9918A = 0, /**< a TMS9918A: locked, no GPU, no extensions */
  PICO9918_CHIP_F18A     = 1, /**< an F18A: unlock, enhanced renderer, GPU */
  PICO9918_CHIP_PICO9918 = 2, /**< an F18A plus the PICO9918's own extensions */
} pico9918_chip_t;

/**
 * \brief the highest personality this build can be, and what a new instance is
 *
 * The switch needs the F18A build, so this is the top of the ladder. A PICO9918_MODE=0
 * archive cannot have it at all - it has no 64KB map, no GPU and no enhanced renderer,
 * so nothing above the base is a personality it could honour - and the build is
 * rejected rather than quietly capped.
 */
#define PICO9918_CHIP_MAX PICO9918_CHIP_PICO9918

#endif // PICO9918_BUILD_RUNTIME_CHIP

/** \brief the sixteen TMS9918 colours, in palette-index order */
typedef enum
{
  TMS_TRANSPARENT = 0,
  TMS_BLACK,
  TMS_MED_GREEN,
  TMS_LT_GREEN,
  TMS_DK_BLUE,
  TMS_LT_BLUE,
  TMS_DK_RED,
  TMS_CYAN,
  TMS_MED_RED,
  TMS_LT_RED,
  TMS_DK_YELLOW,
  TMS_LT_YELLOW,
  TMS_DK_GREEN,
  TMS_MAGENTA,
  TMS_GREY,
  TMS_WHITE,
} pico9918_color_t;

/** \brief the eight TMS9918 registers, by number and by what each one holds */
typedef enum
{
  TMS_REG_0 = 0,
  TMS_REG_1,
  TMS_REG_2,
  TMS_REG_3,
  TMS_REG_4,
  TMS_REG_5,
  TMS_REG_6,
  TMS_REG_7,
  TMS_NUM_REGISTERS,
  TMS_REG_NAME_TABLE        = TMS_REG_2,
  TMS_REG_COLOR_TABLE       = TMS_REG_3,
  TMS_REG_PATTERN_TABLE     = TMS_REG_4,
  TMS_REG_SPRITE_ATTR_TABLE = TMS_REG_5,
  TMS_REG_SPRITE_PATT_TABLE = TMS_REG_6,
  TMS_REG_FG_BG_COLOR       = TMS_REG_7,
} pico9918_register_t;

#define TMS9918_PIXELS_X 256 /**< active display width, every mode */
#define TMS9918_PIXELS_Y 384 /**< tallest active display any mode reaches; a TMS9918A draws 192 */


/* PUBLIC INTERFACE
 * ---------------------------------------- */

#if PICO9918_SINGLE_INSTANCE

/** \brief initialize the TMS9918 library in single-instance mode */
PICO9918_DLLEXPORT
void pico9918_init(void);

#else

/**
 * \brief create a new TMS9918
 *
 * NOTE - multi-instance limitations. Instances are independent for bus
 * access, VRAM, registers and status. Rendering is not fully independent:
 *
 *  - Rendering is NOT re-entrant. The scanline path uses file-scope scratch
 *    (row bit masks, background fill), so pico9918_scan_line must never be
 *    in flight for two instances at once. Render one at a time; alternating
 *    between instances is fine.
 *  - Three pieces of state are shared that arguably should not be: the
 *    cached display mode, the active mode-ops pointer, and the expanded
 *    palette LUT. Each reflects whichever instance last touched it, so an
 *    instance whose mode or palette differs from the previous renderer's may
 *    produce one stale scanline after a switch.
 *
 * Driving a single instance - the overwhelmingly common case - is unaffected.
 */
PICO9918_DLLEXPORT
pico9918_t* pico9918_new(void);

#endif

#if PICO9918_BUILD_RUNTIME_CHIP

/**
 * \brief select which chip this instance answers as
 *
 * Clamped to PICO9918_CHIP_MAX, so a request the build cannot honour comes back as the
 * highest it can rather than as a half-honoured one - read pico9918_chip() to find out
 * which you got. Stepping down from an unlocked personality relocks the device, because
 * the register file it would otherwise leave visible is not one a TMS9918A has.
 *
 * A reset preserves it: the personality is the chip on the board, not state the bus can
 * clear. A new instance starts at PICO9918_CHIP_MAX, which is what a consumer that never
 * calls this keeps.
 */
PICO9918_DLLEXPORT
void pico9918_set_chip(PICO9918_INST_ARG pico9918_chip_t chip);

/** \brief which chip this instance answers as */
PICO9918_DLLEXPORT
pico9918_chip_t pico9918_chip(PICO9918_INST_ONLY_ARG);

#endif // PICO9918_BUILD_RUNTIME_CHIP

/** \brief reset the TMS9918 */
PICO9918_DLLEXPORT
void pico9918_reset(PICO9918_INST_ONLY_ARG);

/** \brief destroy a TMS9918 and release everything it owns */
PICO9918_DLLEXPORT
void pico9918_destroy(PICO9918_INST_ONLY_ARG);

/** \brief write an address (mode = 1) to the tms9918 - the data byte DB0 -> DB7 */
PICO9918_DLLEXPORT
void pico9918_write_addr(PICO9918_INST_ARG uint8_t data);

/** \brief write data (mode = 0) to the tms9918 - the data byte DB0 -> DB7 */
PICO9918_DLLEXPORT
void pico9918_write_data(PICO9918_INST_ARG uint8_t data);

/** \brief read from the status register */
PICO9918_DLLEXPORT
uint8_t pico9918_read_status(PICO9918_INST_ONLY_ARG);

/** \brief read from the status register without resetting it */
PICO9918_DLLEXPORT
uint8_t pico9918_peek_status(PICO9918_INST_ONLY_ARG);

/** \brief read data (mode = 0) from the tms9918 */
PICO9918_DLLEXPORT
uint8_t pico9918_read_data(PICO9918_INST_ONLY_ARG);

/** \brief read data (mode = 0) without incrementing the address pointer */
PICO9918_DLLEXPORT
uint8_t pico9918_read_data_no_inc(PICO9918_INST_ONLY_ARG);


/** \brief true if both the INT status and the INT control bit are set */
PICO9918_DLLEXPORT
bool pico9918_interrupt_status(PICO9918_INST_ONLY_ARG);

/** \brief set the interrupt flag */
PICO9918_DLLEXPORT
void pico9918_interrupt_set(PICO9918_INST_ONLY_ARG);

/** \brief set the status flags */
PICO9918_DLLEXPORT
void pico9918_set_status(PICO9918_INST_ARG uint8_t status);

/**
 * \brief the library's line buffer size - the active pixels plus the eight bytes past
 * them that a fine-h-scrolled tile layer's last quad can reach
 *
 * From the width the library was COMPILED at, not the includer's flags: an 8bpp
 * 80-column build renders two bytes a pixel. pico9918_line_bytes() is the runtime
 * answer for one line; this is the allocation.
 */
#define PICO9918_SCANLINE_BUFFER_SIZE \
  ((PICO9918_BUILD_TEXT80_8BPP ? TMS9918_PIXELS_X * 2 : TMS9918_PIXELS_X) + 8)

/**
 * \brief generate a scanline
 *
 * Read it back with pico9918_line_source and pico9918_line_bytes: how wide a
 * line is and which buffer holds it are both properties of the mode and the
 * build, so the library owns the memory.
 */
PICO9918_DLLEXPORT
uint8_t pico9918_scan_line(PICO9918_INST_ARG uint16_t y);

/** \brief return a register value */
PICO9918_DLLEXPORT
uint8_t pico9918_reg_value(PICO9918_INST_ARG pico9918_register_t reg);

/** \brief write a register value */
PICO9918_DLLEXPORT
void pico9918_write_reg_value(PICO9918_INST_ARG pico9918_register_t reg, uint8_t value);


/** \brief return a value from vram */
PICO9918_DLLEXPORT
uint8_t pico9918_vram_value(PICO9918_INST_ARG uint16_t addr);


/** \brief check the BLANK flag */
PICO9918_DLLEXPORT
bool pico9918_display_enabled(PICO9918_INST_ONLY_ARG);


/** \brief the current display mode */
PICO9918_DLLEXPORT
pico9918_mode_t pico9918_display_mode(PICO9918_INST_ONLY_ARG);

/**
 * \brief how many bytes of the line the current mode fills: 256, or 512 for
 * unlocked 80-column text on a board built with the 8bpp tier
 */
PICO9918_DLLEXPORT
uint32_t pico9918_line_bytes(PICO9918_INST_ONLY_ARG);

/**
 * \brief where the scanline just generated actually is - the arbitration
 * buffer, or a tile layer's own buffer on a line that needed no compositing
 *
 * Valid until the next scanline, and the only way to read the line back.
 * Always word-aligned, so it can be read a word at a time.
 */
PICO9918_DLLEXPORT
const uint8_t* pico9918_line_source(PICO9918_INST_ONLY_ARG);

/** \brief a default palette value, 0x0rgb */
PICO9918_DLLEXPORT
uint16_t pico9918_default_palette(int index);

#endif // _PICO9918_H
