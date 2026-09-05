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

#include "pico9918_build_config.h"

/* The instance mode comes from the generated header, not from the consumer's own flags:
 * it changes the calling convention of nearly every entry point below, and C symbols
 * carry no argument types, so a disagreement would link clean and then call wrongly.
 * A consumer may still state it - that is what the library's own build does - but it
 * has to agree with the archive. */
#ifndef PICO9918_SINGLE_INSTANCE
#define PICO9918_SINGLE_INSTANCE PICO9918_BUILD_SINGLE_INSTANCE
#elif (PICO9918_SINGLE_INSTANCE != 0) != (PICO9918_BUILD_SINGLE_INSTANCE != 0)
#error "PICO9918_SINGLE_INSTANCE disagrees with the archive - drop it and let pico9918_build_config.h supply it"
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

  /* The accessors take all 64 registers. A locked device decodes only the eight above,
     so everything below needs the F18A personality unlocked first. */
  PICO9918_REG_NAME_TABLE2      = 10, /**< tile layer 2 name table base */
  PICO9918_REG_COLOR_TABLE2     = 11, /**< tile layer 2 colour table base */
  PICO9918_REG_STATUS_SELECT    = 15, /**< which status register S1 reads back, and the counter controls */
  PICO9918_REG_HORZ_INT_LINE    = 19, /**< scanline the horizontal interrupt fires on */
  PICO9918_REG_PALETTE_SELECT   = 24, /**< sub-palette for sprites and each tile layer */
  PICO9918_REG_T2_HSCROLL       = 25, /**< tile layer 2 horizontal scroll */
  PICO9918_REG_T2_VSCROLL       = 26, /**< tile layer 2 vertical scroll */
  PICO9918_REG_T1_HSCROLL       = 27, /**< tile layer 1 horizontal scroll */
  PICO9918_REG_T1_VSCROLL       = 28, /**< tile layer 1 vertical scroll */
  PICO9918_REG_PAGE_SIZE        = 29, /**< scroll page sizes, and the ECM pattern plane stride */
  PICO9918_REG_MAX_SCAN_SPRITES = 30, /**< sprites drawn per scanline before the limit bites */
  PICO9918_REG_BML_CONTROL      = 31, /**< bitmap layer enable, priority, transparency, fat pixels */
  PICO9918_REG_BML_BASE         = 32, /**< bitmap layer base address, in 64-byte units */
  PICO9918_REG_BML_X            = 33, /**< bitmap layer left edge */
  PICO9918_REG_BML_TOP_ROW      = 34, /**< bitmap layer top row */
  PICO9918_REG_BML_WIDTH        = 35, /**< bitmap layer width in pixels */
  PICO9918_REG_BML_HEIGHT       = 36, /**< bitmap layer height in rows */
  PICO9918_REG_PALETTE_CONTROL  = 47, /**< palette data port mode, auto-increment and index */
  PICO9918_REG_VRAM_INC         = 48, /**< signed VRAM address increment per access */
  PICO9918_REG_ENHANCED1        = 49, /**< tile layer 2, 30-row mode, ECM levels, real Y */
  PICO9918_REG_ENHANCED2        = 50, /**< GPU triggers, per-position attributes, layer priority */
  PICO9918_REG_MAX_SPRITES      = 51, /**< sprites processed per frame before the scan stops */
  PICO9918_REG_GPU_PC_MSB       = 54, /**< GPU program counter, high byte */
  PICO9918_REG_GPU_PC_LSB       = 55, /**< GPU program counter, low byte - writing it also starts the GPU */
  PICO9918_REG_GPU_CONTROL      = 56, /**< GPU load and trigger */
  PICO9918_REG_UNLOCK           = 57, /**< two consecutive writes of 0x1c unlock the F18A personality */
  PICO9918_REG_CONFIG_INDEX     = 58, /**< PICO9918 only: which configuration byte R59 addresses */
  PICO9918_REG_CONFIG_VALUE     = 59, /**< PICO9918 only: the configuration byte R58 selected */
  PICO9918_REG_FLASH_CONTROL    = 63, /**< PICO9918 only: flash operation control */
} pico9918_register_t;

/**
 * \brief the status registers, by number and by what each one reports
 *
 * Which one a status read returns is selected by the low four bits of R15, so all but
 * the first need the F18A personality unlocked. The counters are pairs, low byte first.
 */
typedef enum
{
  PICO9918_SR_STATUS       = 0,  /**< the TMS9918A status: interrupt, 5th sprite, collision, sprite number */
  PICO9918_SR_IDENT        = 1,  /**< chip identity, blanking, and the scanline interrupt flag */
  PICO9918_SR_GPU          = 2,  /**< GPU running and its status byte */
  PICO9918_SR_RASTER_LINE  = 3,  /**< the line currently being drawn */
  PICO9918_SR_NANOS_LSB    = 4,  /**< nanosecond counter, low byte. Always 0 here: no 10ns source */
  PICO9918_SR_NANOS_MSB    = 5,  /**< nanosecond counter, high bits. Always 0 here */
  PICO9918_SR_MICROS_LSB   = 6,  /**< microsecond counter, low byte */
  PICO9918_SR_MICROS_MSB   = 7,  /**< microsecond counter, high bits */
  PICO9918_SR_MILLIS_LSB   = 8,  /**< millisecond counter, low byte */
  PICO9918_SR_MILLIS_MSB   = 9,  /**< millisecond counter, high bits */
  PICO9918_SR_SECONDS_LSB  = 10, /**< second counter, low byte */
  PICO9918_SR_SECONDS_MSB  = 11, /**< second counter, high byte */
  PICO9918_SR_CONFIG_VALUE = 12, /**< PICO9918 only: the configuration byte R58 selected */
  PICO9918_SR_TEMPERATURE  = 13, /**< PICO9918 only: core temperature, as degrees C times four */
  PICO9918_SR_VERSION      = 14, /**< the F18A feature level, as major and minor nibbles */
  PICO9918_SR_REG_VALUE    = 15, /**< the register value latched when the VRAM address was set */
} pico9918_status_register_t;

/** \brief status register 0 bits. The low five are the sprite number */
#define PICO9918_SR0_INT        0x80 /**< end of frame reached. Cleared by reading SR0 */
#define PICO9918_SR0_5S         0x40 /**< more sprites on a line than the limit allows */
#define PICO9918_SR0_COLLISION  0x20 /**< two sprites overlapped on an opaque pixel */
#define PICO9918_SR0_SPRITE_NUM 0x1f /**< the fifth sprite's number, or the highest seen */

/** \brief status register 1 bits. The high three are the chip identity */
#define PICO9918_SR1_HF    0x01 /**< the line in R19 was reached. Cleared by reading SR1 */
#define PICO9918_SR1_BLANK 0x02 /**< the raster is in blanking */

/** \brief register 0 bits: mode selection and the external VDP input.
 * The three modes register 1 selects are 0 here, so a mode is the pair of writes. */
#define TMS_R0_MODE_GRAPHICS_I  0x00 /**< Graphics I - no bit of its own in R0 */
#define TMS_R0_MODE_GRAPHICS_II 0x02 /**< Graphics II - the only mode R0 selects */
#define TMS_R0_MODE_MULTICOLOR  0x00 /**< Multicolor - selected in R1 */
#define TMS_R0_MODE_TEXT        0x00 /**< 40-column text - selected in R1 */
#define TMS_R0_MODE_TEXT_80     0x04 /**< 80-column text, with R1's text mode. The F18A's M4 */
#define TMS_R0_EXT_VDP_ENABLE   0x01 /**< take video from the external VDP input */
#define TMS_R0_EXT_VDP_DISABLE  0x00 /**< ignore the external VDP input */
#define TMS_R0_DOUBLE_ROWS      0x08 /**< PICO9918 only: twice the rows, drawn interlaced. Sprites stay low-res */
#define TMS_R0_INT_SCANLINE     0x10 /**< assert /INT when the raster reaches the line in R19. The F18A's IE1 */

/** \brief register 1 bits: VRAM size, blanking, interrupt, mode and sprite size */
#define TMS_R1_RAM_16K          0x80 /**< 16KB of VRAM */
#define TMS_R1_RAM_4K           0x00 /**< 4KB of VRAM */
#define TMS_R1_DISP_BLANK       0x00 /**< blank the display; the border still draws */
#define TMS_R1_DISP_ACTIVE      0x40 /**< render the active display */
#define TMS_R1_INT_ENABLE       0x20 /**< assert /INT at end of frame */
#define TMS_R1_INT_DISABLE      0x00 /**< leave /INT alone */
#define TMS_R1_MODE_GRAPHICS_I  0x00 /**< Graphics I - no bit of its own in R1 */
#define TMS_R1_MODE_GRAPHICS_II 0x00 /**< Graphics II - selected in R0 */
#define TMS_R1_MODE_MULTICOLOR  0x08 /**< Multicolor */
#define TMS_R1_MODE_TEXT        0x10 /**< 40-column text */
#define TMS_R1_SPRITE_8         0x00 /**< 8x8 sprite patterns */
#define TMS_R1_SPRITE_16        0x02 /**< 16x16 sprite patterns */
#define TMS_R1_SPRITE_MAG1      0x00 /**< sprites drawn at their pattern size */
#define TMS_R1_SPRITE_MAG2      0x01 /**< sprites drawn at twice their pattern size */

/* The F18A register bits worth naming. Every one of these needs the F18A personality
   unlocked, R0's M4 included, and each mask names the field's position, not a value. */

/** \brief register 24 bits: the sub-palette each layer takes */
#define PICO9918_R24_SPRITE_PS 0x30 /**< sprite palette select */
#define PICO9918_R24_TILE_PS   0x0f /**< tile palette select, layer 2 high and layer 1 low */
#define PICO9918_R24_TILE2_PS  0x0c /**< tile layer 2 palette select */
#define PICO9918_R24_TILE1_PS  0x03 /**< tile layer 1 palette select */

/** \brief register 29 fields: scroll page sizes, and the stride between ECM pattern planes */
#define PICO9918_R29_SPRITE_STRIDE 0xc0 /**< sprite pattern plane stride, 0x800 >> n */
#define PICO9918_R29_PAGE2_HORZ    0x20 /**< tile layer 2 scrolls across two pages */
#define PICO9918_R29_PAGE2_VERT    0x10 /**< tile layer 2 scrolls down two pages */
#define PICO9918_R29_TILE_STRIDE   0x0c /**< tile pattern plane stride, 0x800 >> n */
#define PICO9918_R29_PAGE1_HORZ    0x02 /**< tile layer 1 scrolls across two pages */
#define PICO9918_R29_PAGE1_VERT    0x01 /**< tile layer 1 scrolls down two pages */

/** \brief register 31 bits: the bitmap layer */
#define PICO9918_R31_BML_ENABLE   0x80 /**< draw the bitmap layer */
#define PICO9918_R31_BML_PRIORITY 0x40 /**< bitmap layer above the tile layers */
#define PICO9918_R31_BML_TRANSP   0x20 /**< pixel value 0 is transparent */
#define PICO9918_R31_BML_FAT      0x10 /**< two bits a pixel, drawn double width */
#define PICO9918_R31_BML_PS       0x0f /**< bitmap layer palette select */

/** \brief register 47 bits: the palette data port */
#define PICO9918_R47_DATA_PORT 0x80 /**< route data port writes to palette RAM */
#define PICO9918_R47_AUTO_INC  0x40 /**< step the palette index after each entry */
#define PICO9918_R47_INDEX     0x3f /**< first palette index to write */

/** \brief register 49 bits: tile layer 2, row count, and the enhanced colour modes */
#define PICO9918_R49_TILE2_ENABLE 0x80 /**< draw tile layer 2 */
#define PICO9918_R49_ROW30        0x40 /**< 30 rows of tiles rather than 24 */
#define PICO9918_R49_ECM_TILE     0x30 /**< tile ECM level field */
#define PICO9918_R49_ECM_TILE_1   0x10 /**< tiles take one bitplane, two colours */
#define PICO9918_R49_ECM_TILE_2   0x20 /**< tiles take two bitplanes, four colours */
#define PICO9918_R49_ECM_TILE_3   0x30 /**< tiles take three bitplanes, eight colours */
#define PICO9918_R49_Y_REAL       0x08 /**< sprite Y is the real row, not row minus one */
#define PICO9918_R49_ECM_SPRITE   0x03 /**< sprite ECM level field */
#define PICO9918_R49_ECM_SPRITE_1 0x01 /**< sprites take one bitplane, two colours */
#define PICO9918_R49_ECM_SPRITE_2 0x02 /**< sprites take two bitplanes, four colours */
#define PICO9918_R49_ECM_SPRITE_3 0x03 /**< sprites take three bitplanes, eight colours */

/** \brief register 50 bits: GPU triggers and the remaining layer controls */
#define PICO9918_R50_RESET       0x80 /**< reset the VDP */
#define PICO9918_R50_GPU_HSYNC   0x40 /**< trigger the GPU every scanline */
#define PICO9918_R50_GPU_VSYNC   0x20 /**< trigger the GPU every frame */
#define PICO9918_R50_TILE1_OFF   0x10 /**< stop drawing tile layer 1 */
#define PICO9918_R50_REPORT_MAX  0x08 /**< S0's sprite number reports the highest seen */
#define PICO9918_R50_VSCANLINES  0x04 /**< F18A only: dim every second raster line */
#define PICO9918_R50_POS_ATTR    0x02 /**< tile attributes come per position, not per tile */
#define PICO9918_R50_T2_PRIORITY 0x01 /**< tile layer 2 above tile layer 1 */

/** \brief register 56 bit: the GPU trigger */
#define PICO9918_R56_GPU_RUN 0x01 /**< 1 starts the GPU, 0 loads the PC without starting */

/** \brief the value register 57 takes, twice in a row, to unlock */
#define PICO9918_R57_UNLOCK 0x1c /**< two consecutive writes unlock the F18A personality */

/** \brief register 15 bits: the counter controls, and which status register S1 reads */
#define PICO9918_R15_COUNTER_RESET 0x40 /**< reset the frame/scanline counters */
#define PICO9918_R15_COUNTER_SNAP  0x20 /**< latch the counters for reading */
#define PICO9918_R15_COUNTER_EN    0x10 /**< let the counters run */
#define PICO9918_R15_STATUS_NUM    0x0f /**< which status register S1 reads back */

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
