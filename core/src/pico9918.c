/**
 * \file
 * \brief pico9918-core - Core interface
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 */


#include "impl/pico9918_priv.h"
/* pico9918_gpu_service: the register writes below are where a GPU program is armed,
   and a host that handed the library the GPU wants it run there. */
#include "impl/pico9918_gpu_priv.h"
#include "overlay/splash.h" /* pico9918_reset re-arms the splash, as a host reset does */

#include <string.h>


/* The mode emitters stay out of pico9918_scan_line. That function is well past the Thumb-1 b.n
   reach, so anything added to it relaxes branches and shuffles registers inside whichever emitters
   are inlined there. Keeping them out of line costs nothing on either board. */
#define EMITTER_NOINLINE PICO9918_NOINLINE

#ifdef PICO_BUILD
/* The DMA channel numbers are compile-time macros in the pico platform header, not
   variables here: an extern channel number costs three instructions at every access.
   The copy channel's two configs do live here. */
PICO9918_COPY_STATE()
#else
/* Off-target the fills and the copy are plain structs. */
pico9918_fill32_t pico9918_fill_border = {NULL, 0};
pico9918_fill32_t pico9918_fill_masks  = {NULL, 0};
pico9918_fill32_t pico9918_fill_line   = {NULL, 0};
pico9918_copy32_t pico9918_copy       = {NULL, NULL, 0};
#endif

/* not .scratch_x: that is where the fill writes */
PICO9918_SECTION_SCRATCH_Y(buffer) static uint32_t bg;

/* Where a scanline is arbitrated: the fill, the sprites, the bitmap layer and the composite all land
   here. Its own bank, so the fill writes it from `bg`'s and a caller reads it against striped SRAM. */
static PICO9918_SECTION_SCRATCH_X(buffer) uint8_t __aligned(4) scanlineBuffer[SCANLINE_BUFFER_BYTES];


/* For a single scanline, we only support a single mode... so let's cache it. Shared between
   instances: pico9918_scan_line recomputes it from the instance's own registers on entry, and
   a mismatch there is what marks the shared palette LUT dirty. */
static pico9918_mode_t tmsCachedMode = TMS_MODE_GRAPHICS_I;

/* Is this row 80 columns at one byte a pixel - twice as wide a line, on two pixel grids?
   Without the tier it is a literal false, so every count and shift below it folds away.
   Unlocked only, and that is not a restriction: all four things the tier buys are F18A features that
   need the unlock anyway, so locked 80-column text keeps the packed line and its own emitter. */
#if PICO9918_TEXT80_8BPP
#define TEXT80_WIDE_ROW (tmsCachedMode == TMS_MODE_TEXT80 && PICO9918_UNLOCKED(tms9918))
#else
#define TEXT80_WIDE_ROW false
#endif

/* Configured and claimed once, before the host brings up anything that shares the
   DMA. Defined below, next to the tables it fills. */
void initLookups(void);

#if PICO9918_SINGLE_INSTANCE

// VRAM is intentionally never zeroed at boot (pico9918_reset() below
// leaves it alone - matches real TMS9918 hardware, whose VRAM content is
// undefined at power-on); every other field is explicitly written by
// pico9918_reset()/vdpRegisterReset() before anything reads it - so this
// doesn't need the crt0 .bss zero-fill, and skipping it saves boot time.
//
// The 256-byte alignment is required: the GPU guards vram.bytes[0x8000] and
// the palette with MPU regions that are whole 256-byte pages, and neither
// range may cross a page boundary. A 256-aligned instance is what fixes where
// inside its page each range lands.
static pico9918_t __aligned(256) PICO9918_UNINITIALIZED(tms9918Inst);

/* const so the instance address is a link-time constant rather than a pointer the
   emitters have to load and keep live: every field offset then folds into its own
   literal, which is what makes vram's offset within the struct cost nothing. */
pico9918_t* const tms9918 = &tms9918Inst;

/** \brief initialize the TMS9918 library in single-instance mode */
PICO9918_DLLEXPORT
void __time_critical_func(pico9918_init)(void)
{
  /* The instance skips the .bss zero-fill, and pico9918_config_apply is the only other
     writer of the render base - so without this the host bus can read it before the
     host has loaded a config. */
  tms9918->vdpBase = PICO9918_BASE_TMS9918;
#if PICO9918_BUILD_RUNTIME_CHIP
  pico9918_set_chip(PICO9918_INST PICO9918_CHIP_MAX);
#endif
  initLookups();
  pico9918_reset(PICO9918_INST_ONLY);
}

#else

#include <stdlib.h>

/** \brief create a new TMS9918 */
PICO9918_DLLEXPORT pico9918_t* pico9918_new(void)
{
  /* Zeroed, like the .bss instance the single-instance build gets: nothing else writes the
     GPU's mapped region above 0x4000, and its DMA port at 0x8008 is read after every program. */
  pico9918_t* tms9918 = (pico9918_t*)calloc(1, sizeof(pico9918_t));
  if (tms9918 != NULL)
  {
    tms9918->vdpBase = PICO9918_BASE_TMS9918; /* see pico9918_init */
#if PICO9918_BUILD_RUNTIME_CHIP
    pico9918_set_chip(tms9918, PICO9918_CHIP_MAX);
#endif
    initLookups();
    pico9918_reset(tms9918);
  }

  return tms9918;
}

#endif


static const pico9918_mode_t r1Modes[] = {TMS_MODE_GRAPHICS_I, TMS_MODE_MULTICOLOR, TMS_MODE_TEXT,
                                          TMS_MODE_GRAPHICS_I};

static inline pico9918_mode_t tmsMode(pico9918_t* tms9918)
{
  if (TMS_REGISTER(tms9918, TMS_REG_0) & TMS_R0_MODE_GRAPHICS_II)
    return TMS_MODE_GRAPHICS_II;
  /* An F18A honours M4 while still locked, so the test is the personality, not the lock. */
  else if (PICO9918_M4(tms9918))
    return TMS_MODE_TEXT80;
  else
    return r1Modes[(TMS_REGISTER(tms9918, TMS_REG_1) & (TMS_R1_MODE_MULTICOLOR | TMS_R1_MODE_TEXT)) >> 3];
}

/** \brief sprite size (8 or 16) */
static inline uint8_t tmsSpriteSize(pico9918_t* tms9918)
{
  return TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_SPRITE_16 ? 16 : 8;
}

/** \brief sprite size (0 = 1x, 1 = 2x) */
static inline bool tmsSpriteMag(pico9918_t* tms9918)
{
  return TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_SPRITE_MAG2;
}

/** \brief name table base address */
static inline uint16_t tmsNameTableAddr(pico9918_t* tms9918)
{
  return (TMS_REGISTER(tms9918, TMS_REG_NAME_TABLE) & 0x0f) << 10;
}

/** \brief name table base address */
static inline uint16_t tmsNameTable2Addr(pico9918_t* tms9918)
{
  return (TMS_REGISTER(tms9918, PICO9918_REG_NAME_TABLE2) & 0x0f) << 10;
}

/** \brief color table base address */
static inline uint16_t tmsColorTableAddr(pico9918_t* tms9918)
{
  const uint8_t mask = (tmsCachedMode == TMS_MODE_GRAPHICS_II) ? 0x80 : 0xff;

  return (TMS_REGISTER(tms9918, TMS_REG_COLOR_TABLE) & mask) << 6;
}

/** \brief color table base address */
static inline uint16_t tmsColorTable2Addr(pico9918_t* tms9918)
{
  const uint8_t mask = (tmsCachedMode == TMS_MODE_GRAPHICS_II) ? 0x80 : 0xff;

  return (TMS_REGISTER(tms9918, PICO9918_REG_COLOR_TABLE2) & mask) << 6;
}

/** \brief pattern table base address */
static inline uint16_t tmsPatternTableAddr(pico9918_t* tms9918)
{
  const uint8_t mask = (tmsCachedMode == TMS_MODE_GRAPHICS_II) ? 0x04 : 0x07;

  return (TMS_REGISTER(tms9918, TMS_REG_PATTERN_TABLE) & mask) << 11;
}

/** \brief sprite attribute table base address */
static inline uint16_t tmsSpriteAttrTableAddr(pico9918_t* tms9918)
{
  return (TMS_REGISTER(tms9918, TMS_REG_SPRITE_ATTR_TABLE) & 0x7f) << 7;
}

/** \brief sprite pattern table base address */
static inline uint16_t tmsSpritePatternTableAddr(pico9918_t* tms9918)
{
  return (TMS_REGISTER(tms9918, TMS_REG_SPRITE_PATT_TABLE) & 0x07) << 11;
}

/** \brief background color */
static inline pico9918_color_t tmsMainBgColor(pico9918_t* tms9918)
{
  return TMS_REGISTER(tms9918, TMS_REG_FG_BG_COLOR) & 0x0f;
}

/** \brief foreground color */
static inline pico9918_color_t tmsMainFgColor(pico9918_t* tms9918)
{
  const pico9918_color_t c = (pico9918_color_t)(TMS_REGISTER(tms9918, TMS_REG_FG_BG_COLOR) >> 4);
  return c == TMS_TRANSPARENT ? tmsMainBgColor(tms9918) : c;
}

/** \brief foreground color */
static inline pico9918_color_t tmsFgColor(pico9918_t* tms9918, uint8_t colorByte)
{
  const pico9918_color_t c = (pico9918_color_t)(colorByte >> 4);
  return c == TMS_TRANSPARENT ? tmsMainBgColor(tms9918) : c;
}

/** \brief background color */
static inline pico9918_color_t tmsBgColor(pico9918_t* tms9918, uint8_t colorByte)
{
  const pico9918_color_t c = (pico9918_color_t)(colorByte & 0x0f);
  return c == TMS_TRANSPARENT ? tmsMainBgColor(tms9918) : c;
}


// default palette 0xARGB
static const uint16_t defaultPalette[] = {
  //-- Palette 0, default TMS9918A palette
  0x0000, 0xF000, 0xF2C3, 0xF5D6, 0xF54F, 0xF76F, 0xFD54, 0xF4EF, 0xFF54, 0xFF76, 0xFDC3, 0xFED6, 0xF2B2,
  0xFC5C, 0xFCCC, 0xFFFF,
  //-- Palette 1, ECM1 (0 index is always 000) version of palette 0
  0x0000, 0xF2C3, 0xF000, 0xF54F, 0xF000, 0xFD54, 0xF000, 0xF4EF, 0xF000, 0xFCCC, 0xF000, 0xFDC3, 0xF000,
  0xFC5C, 0xF000, 0xFFFF,
  //-- Palette 2, CGA colors
  0x0000, 0xF00A, 0xF0A0, 0xF0AA, 0xFA00, 0xFA0A, 0xFA50, 0xFAAA, 0xF555, 0xF55F, 0xF5F5, 0xF5FF, 0xFF55,
  0xFF5F, 0xFFF5, 0xFFFF,
  //-- Palette 3, ECM1 (0 index is always 000) version of palette 2
  0x0000, 0xF555, 0xF000, 0xF00A, 0xF000, 0xF0A0, 0xF000, 0xF0AA, 0xF000, 0xFA00, 0xF000, 0xFA0A, 0xF000,
  0xFA50, 0xF000, 0xFFFF};

static PICO9918_NOINLINE void vdpRegisterReset(pico9918_t* tms9918)
{
  tms9918->isUnlocked  = false;
  tms9918->restart     = 0;
  tms9918->unlockCount = 0;
  tms9918->lockedMask  = 0x07;
  memset(&TMS_REGISTER(tms9918, TMS_REG_0), 0, TMS_REGISTERS);
  TMS_REGISTER(tms9918, TMS_REG_1) = 0x40;
  TMS_REGISTER(tms9918, TMS_REG_3) = 0x10;
  TMS_REGISTER(tms9918, TMS_REG_4) = 0x01;
  TMS_REGISTER(tms9918, TMS_REG_5) = 0x0A;
  TMS_REGISTER(tms9918, TMS_REG_6) = 0x02;
  TMS_REGISTER(tms9918, TMS_REG_7) = 0xF2;
  TMS_REGISTER(tms9918, PICO9918_REG_MAX_SCAN_SPRITES) = MAX_SPRITES - 1; // scanline sprites
  TMS_REGISTER(tms9918, PICO9918_REG_VRAM_INC) = 1;               // vram address increment register
  TMS_REGISTER(tms9918, PICO9918_REG_MAX_SPRITES) = MAX_SPRITES;     // Sprites to process
  TMS_REGISTER(tms9918, PICO9918_REG_GPU_PC_MSB) = 0x40;
}


#if PICO9918_BUILD_RUNTIME_CHIP

/** \brief the feature bits a personality answers to - the ladder, in one place */
static uint8_t chipFeatures(pico9918_chip_t chip)
{
  switch (chip)
  {
    case PICO9918_CHIP_PICO9918:
      return PICO9918_FEAT_UNLOCK | PICO9918_FEAT_CONFIG | PICO9918_FEAT_OVERLAY;
    case PICO9918_CHIP_F18A:
      return PICO9918_FEAT_UNLOCK;
    default:
      return 0;
  }
}

/** \brief select which chip this instance answers as */
PICO9918_DLLEXPORT void pico9918_set_chip(PICO9918_INST_ARG pico9918_chip_t chip)
{
  /* unsigned, so a value below the base clamps here too rather than being stored */
  if ((unsigned)chip > (unsigned)PICO9918_CHIP_MAX)
  {
    chip = PICO9918_CHIP_MAX;
  }

  tms9918->chip     = (uint8_t)chip;
  tms9918->features = chipFeatures(chip);

  /* A personality that cannot unlock cannot be left unlocked either: the wide register
     file and the enhanced renderer both hang off this one flag, and a device that kept
     them across the step down would be neither chip. A GPU program already running is
     the same problem - the registers that start one are out of reach now, and nothing in
     the core stops it on its own. */
  if (!PICO9918_HAS(tms9918, PICO9918_FEAT_UNLOCK))
  {
    tms9918->isUnlocked         = false;
    tms9918->unlockCount        = 0;
    tms9918->lockedMask         = 0x07;
    TMS_REGISTER(tms9918, PICO9918_REG_GPU_CONTROL) = 0;
    tms9918->palDirty           = 1; // the 80-column line narrows with it
  }

  /* Written here as well as in the reset, so a personality chosen after one is not a
     frame late in answering a probe. */
  TMS_STATUS(tms9918, 1) = PICO9918_SR1_ID(tms9918);
}

/** \brief which chip this instance answers as */
PICO9918_DLLEXPORT pico9918_chip_t pico9918_chip(PICO9918_INST_ONLY_ARG)
{
  return (pico9918_chip_t)tms9918->chip;
}

#endif // PICO9918_BUILD_RUNTIME_CHIP

/** \brief reset the new TMS9918 */
PICO9918_DLLEXPORT void __time_critical_func(pico9918_reset)(PICO9918_INST_ONLY_ARG)
{
  tms9918->regWriteStage0Value = 0;
  tms9918->currentAddress      = 0;
  tms9918->gpuAddress          = 0xFFFF; // "Odd" don't start value
  tms9918->regWriteStage       = 0;

  tms9918->palWriteStage       = 0;
  tms9918->palWriteStage0Value = 0;
  tms9918->flash               = 0;
  memset(&TMS_STATUS(tms9918, 0), 0, TMS_STATUS_REGISTERS);
  /* SR0 has a shadow the frame path merges into, and the interrupt latch resets with it:
     the register alone leaves the next merge republishing the flags just dropped. */
  pico9918_frame_reset_int_impl(PICO9918_INST_ONLY);
  /* ID = F18A (0xE0), plus 0x08 for anyone who cares it's not a real one. Which chip
     this is does not reset - see pico9918_set_chip. */
  TMS_STATUS(tms9918, 1)   = PICO9918_SR1_ID(tms9918);
  TMS_STATUS(tms9918, 14)  = 0x1A; // Version
  tms9918->readAheadBuffer = 0;

  vdpRegisterReset(tms9918);
  TMS_REGISTER(tms9918, TMS_REG_1) = 0x00; // turn display off
  TMS_REGISTER(tms9918, TMS_REG_7) = 0x00;
  tmsCachedMode               = TMS_MODE_GRAPHICS_I;

  // set up default palettes (arm is little-endian, tms9900 is big-endian)
  for (int i = 0; i < sizeof(defaultPalette) / sizeof(uint16_t); ++i)
  {
    tms9918->vram.map.pram[i] = __builtin_bswap16(defaultPalette[i]);
  }

  /* Both halves, or neither works: the frame count is what reopens the splash gate -
     validWrites is a once-per-run latch and stays set - and the rewind is what makes the
     animation play from its start rather than from wherever it stopped. */
  pico9918_frame_reset_count_impl(PICO9918_INST_ONLY);
  pico9918_splash_reset();

  /* ram intentionally left in unknown state */
}


/**
 * \brief destroy a TMS9918
 *
 * tms9918: tms9918 object to destroy / clean up
 */
PICO9918_DLLEXPORT void __time_critical_func(pico9918_destroy)(PICO9918_INST_ONLY_ARG)
{
#if !PICO9918_SINGLE_INSTANCE
  free(tms9918);
  tms9918 = NULL;
#endif
}

/**
 * \brief write an address (mode = 1) to the tms9918
 *
 * data: the data (DB0 -> DB7) to send
 */
PICO9918_DLLEXPORT void __time_critical_func(pico9918_write_addr)(PICO9918_INST_ARG uint8_t data)
{
  pico9918_write_addr_impl(PICO9918_INST data);
  /* An R1 mask change has to move the pin now, not at the next active line. The board's
     write IRQ does this itself; the direct pico9918_write_reg_value deliberately does not,
     which is what lets the golden surface write a register with nothing reconciling. */
  pico9918_write_reconcile_int_impl(PICO9918_INST_ONLY);
}

/** \brief read from the status register */
PICO9918_DLLEXPORT uint8_t __time_critical_func(pico9918_read_status)(PICO9918_INST_ONLY_ARG)
{
  return pico9918_read_status_impl(PICO9918_INST_ONLY);
}

/** \brief read from the status register without resetting it */
PICO9918_DLLEXPORT uint8_t __time_critical_func(pico9918_peek_status)(PICO9918_INST_ONLY_ARG)
{
  return pico9918_peek_status_impl(PICO9918_INST_ONLY);
}

/**
 * \brief write data (mode = 0) to the tms9918
 *
 * data: the data (DB0 -> DB7) to send
 */
PICO9918_DLLEXPORT void __time_critical_func(pico9918_write_data)(PICO9918_INST_ARG uint8_t data)
{
  pico9918_write_data_impl(PICO9918_INST data);
}


/** \brief read data (mode = 0) from the tms9918 */
PICO9918_DLLEXPORT uint8_t __time_critical_func(pico9918_read_data)(PICO9918_INST_ONLY_ARG)
{
  return pico9918_read_data_impl(PICO9918_INST_ONLY);
}

/** \brief read data (mode = 0) from the tms9918 */
PICO9918_DLLEXPORT uint8_t __time_critical_func(pico9918_read_data_no_inc)(PICO9918_INST_ONLY_ARG)
{
  return pico9918_read_data_no_inc_impl(PICO9918_INST_ONLY);
}

/** \brief return true if both INT status and INT control set */
PICO9918_DLLEXPORT bool __time_critical_func(pico9918_interrupt_status)(PICO9918_INST_ONLY_ARG)
{
  return pico9918_interrupt_status_impl(PICO9918_INST_ONLY);
}

/** \brief raise the INT status flag */
PICO9918_DLLEXPORT void __time_critical_func(pico9918_interrupt_set)(PICO9918_INST_ONLY_ARG)
{
  pico9918_interrupt_set_impl(PICO9918_INST_ONLY);
}

/** \brief set status flag */
PICO9918_DLLEXPORT
void __time_critical_func(pico9918_set_status)(PICO9918_INST_ARG uint8_t status)
{
  pico9918_set_status_impl(PICO9918_INST status);
}

static const uint32_t zeroWord = 0;

/* Sprites and the bitmap layer are always on the 256-pixel grid, so their masks are one
   bit per grid pixel whatever the mode. A tile layer's own line is not: 80 columns at eight bits a
   pixel are 512 pixels and want a bit for each, which is what lets a layer be selected per pixel.
   The two are different lengths *and* different units, and the same function must not take both. */
typedef uint32_t BitMask[9];
typedef uint32_t TileMask[SCANLINE_MASK_WORDS];

/* A mask walked one `<<= 1` a pixel has to be unsigned: shifting a negative signed value left is
   undefined, and a compiler may then fold the sign test away. `-fsanitize=shift-base` catches it. */
#define MASK_NEXT_PIXEL 0x80000000u


/* one object, so the per-scanline clear is a single transfer and the layout cannot drift */
static PICO9918_SECTION_SCRATCH_X(lookup) struct
{
  BitMask rowBits;                  /* pixel mask */
  BitMask rowTransparentSpriteBits; /* transparent sprite pixels */
  BitMask rowSpriteBits;            /* collision mask */
} __aligned(4) rowMasks;

/** \brief Test and update the sprite collision mask. */
static inline uint32_t tmsTestCollisionMask(const uint32_t xPos, const uint32_t spritePixels,
                                            const uint32_t spriteWidth)
{
  uint32_t rowSpriteBitsWord    = xPos >> 5;
  uint32_t rowSpriteBitsWordBit = xPos & 0x1f;

  uint32_t validPixels =
    (~rowMasks.rowSpriteBits[rowSpriteBitsWord]) & (spritePixels >> rowSpriteBitsWordBit);
  rowMasks.rowSpriteBits[rowSpriteBitsWord] |= validPixels;
  validPixels <<= rowSpriteBitsWordBit;

  rowSpriteBitsWordBit = 32 - rowSpriteBitsWordBit;
  if (rowSpriteBitsWordBit < spriteWidth)
  {
    uint32_t right = (~rowMasks.rowSpriteBits[++rowSpriteBitsWord]) & (spritePixels << rowSpriteBitsWordBit);
    rowMasks.rowSpriteBits[rowSpriteBitsWord] |= right;
    validPixels |= (right >> rowSpriteBitsWordBit);
  }

  return validPixels;
}


/** \brief set the transparent sprite mask. */
static inline void tmsSetTransparentSpriteMask(const uint32_t xPos, const uint32_t spritePixels,
                                               const uint32_t spriteWidth)
{
  uint32_t rowSpriteBitsWord    = xPos >> 5;
  uint32_t rowSpriteBitsWordBit = xPos & 0x1f;

  rowMasks.rowTransparentSpriteBits[rowSpriteBitsWord] |= spritePixels >> rowSpriteBitsWordBit;

  rowSpriteBitsWordBit = 32 - rowSpriteBitsWordBit;
  if (rowSpriteBitsWordBit < spriteWidth)
  {
    rowMasks.rowTransparentSpriteBits[rowSpriteBitsWord + 1] |= spritePixels << rowSpriteBitsWordBit;
  }
}


/** \brief Clear the row pixels bit mask. */
static inline void tmsClearRowBitsMask(const uint32_t xPos, const uint32_t tilePixels,
                                       const uint32_t tileWidth, BitMask rowBitsMask)
{
  uint32_t rowBitsWord    = xPos >> 5;
  uint32_t rowBitsWordBit = xPos & 0x1f;

  uint32_t validPixels = tilePixels >> rowBitsWordBit;
  rowBitsMask[rowBitsWord] &= ~validPixels;

  rowBitsWordBit = 32 - rowBitsWordBit;
  if (rowBitsWordBit < tileWidth)
  {
    ++rowBitsWord;
    uint32_t right = (tilePixels << rowBitsWordBit);
    rowBitsMask[rowBitsWord] &= ~right;
  }
}

/** \brief Update the row pixels bit mask (aligned - no word boundary crossing). */
static inline void tmsUpdateRowBitsMaskAligned(const uint32_t xPos, const uint32_t tilePixels,
                                               BitMask rowBitsMask)
{
  rowBitsMask[xPos >> 5] |= tilePixels >> (xPos & 0x1f);
}

/** \brief Test against the row pixels bit mask (aligned - no word boundary crossing). */
static inline uint32_t tmsTestRowBitsMaskAligned(const uint32_t xPos, const uint32_t tilePixels,
                                                 const BitMask rowBitsMask)
{
  return tilePixels & ~(rowBitsMask[xPos >> 5] << (xPos & 0x1f));
}

// Copy and align bit mask with small pixel offset (0-7)
static void tmsCopyAlignMask(TileMask dstMask, const TileMask srcMask, int pixelShift)
{
  if (pixelShift == 0)
  {
    /* straight-line, not a loop: -O3 rewrites the loop form into a bootrom memcpy call */
    dstMask[0] = srcMask[0];
    dstMask[1] = srcMask[1];
    dstMask[2] = srcMask[2];
    dstMask[3] = srcMask[3];
    dstMask[4] = srcMask[4];
    dstMask[5] = srcMask[5];
    dstMask[6] = srcMask[6];
    dstMask[7] = srcMask[7];
    dstMask[8] = srcMask[8];
#if SCANLINE_MASK_WORDS > 9
    dstMask[9]  = srcMask[9];
    dstMask[10] = srcMask[10];
    dstMask[11] = srcMask[11];
    dstMask[12] = srcMask[12];
    dstMask[13] = srcMask[13];
    dstMask[14] = srcMask[14];
    dstMask[15] = srcMask[15];
    dstMask[16] = srcMask[16];
#endif
    return;
  }

  if (pixelShift > 0)
  {
    // Right shift - carry flows left to right (low to high index)
    uint32_t carry = 0;
    for (int i = 0; i < SCANLINE_MASK_WORDS; i++) // LOW to HIGH
    {
      uint32_t word = srcMask[i];
      dstMask[i]    = (word >> pixelShift) | carry;
      carry         = word << (32 - pixelShift);
    }
  }
  else
  {
    // Left shift - carry flows right to left (high to low index)
    pixelShift     = -pixelShift;
    uint32_t carry = 0;
    for (int i = SCANLINE_MASK_WORDS - 1; i >= 0; i--) // HIGH to LOW
    {
      uint32_t word = srcMask[i];
      dstMask[i]    = (word << pixelShift) | carry;
      carry         = word >> (32 - pixelShift);
    }
  }
}


/** \brief Test and update the row pixels bit mask. */
static inline uint32_t tmsTestRowBitsMask(const uint32_t xPos, const uint32_t tilePixels,
                                          const uint32_t tileWidth, const bool update, const bool test,
                                          const bool testColl)
{
  uint32_t rowBitsWord    = xPos >> 5;
  uint32_t rowBitsWordBit = xPos & 0x1f;

  uint32_t validPixels = tilePixels >> rowBitsWordBit;
  if (testColl) validPixels &= ~rowMasks.rowSpriteBits[rowBitsWord];
  if (test) validPixels &= ~rowMasks.rowBits[rowBitsWord];
  if (update) rowMasks.rowBits[rowBitsWord] |= validPixels;
  if (test || testColl) validPixels <<= rowBitsWordBit;

  rowBitsWordBit = 32 - rowBitsWordBit;
  if (rowBitsWordBit < tileWidth)
  {
    ++rowBitsWord;
    uint32_t right = (tilePixels << rowBitsWordBit);

    if (testColl) right &= ~rowMasks.rowSpriteBits[rowBitsWord];
    if (test) right &= ~rowMasks.rowBits[rowBitsWord];

    if (update) rowMasks.rowBits[rowBitsWord] |= right;
    if (test || testColl) validPixels |= (right >> rowBitsWordBit);
  }

  return (test || testColl) ? validPixels : tilePixels;
}


/* lookup for combining ecm nibbles, returning 4 pixels.
 *
 * Deliberately a full table in striped SRAM. Plane 3 only ever lands in bit 2 of a pixel and
 * nothing else does at any level (the palette note below), so a 256-entry two-plane table plus a
 * 16-entry plane 3 mask would give the same words in a fraction of the space. That was built and
 * rejected: it costs a load and an OR on every ECM3 quad, and the tile path takes far more lookups
 * a line than sprites do. Revisit it only when something else needs the room. It cannot live in
 * .scratch_x either way - core 1's stack has the top half of that bank.
 */
/* Every entry is written by ecmLookupInit() before `lookupsReady` is ever set, so it does not
   need the crt0 .bss zero-fill either. */
static uint32_t __aligned(8) PICO9918_UNINITIALIZED(ecmLookup)[16 * 16 * 16];

static uint8_t PICO9918_IN_FLASH_FUNC(ecmByte)(bool h, bool m, bool l)
{
  return (h << 2) | (m << 1) | l;
}

/* lookup from bit planes: 333322221111 to merged palette values for four pixels
 * NOTE: The left-most pixel is stored in the least significant byte of the result
 *       because it's more efficient to offload them that way
 */
static void PICO9918_IN_FLASH_FUNC(ecmLookupInit)(void)
{
  for (uint16_t i = 0; i < 16 * 16 * 16; ++i)
  {
    ecmLookup[i] =
      (ecmByte(i & 0x800, i & 0x080, i & 0x008)) | (ecmByte(i & 0x400, i & 0x040, i & 0x004) << 8) |
      (ecmByte(i & 0x200, i & 0x020, i & 0x002) << 16) | (ecmByte(i & 0x100, i & 0x010, i & 0x001) << 24);
  }
}

/* random note about how palettes are applied:
 * PR Address bit: 0 1 2 3 4 5
 * --------------------------------------
 * original mode: ps0 ps1 cs0 cs1 cs2 cs3
 * 1-bit (ECM1) : ps0 cs0 cs1 cs2 cs3 px0
 * 2-bit (ECM2) : cs0 cs1 cs2 cs3 px1 px0
 * 3-bit (ECM3) : cs0 cs1 cs2 px2 px1 px0
*/


/*
 * to generate the doubled pixels required when the sprite MAG flag is set,
 * use a lookup table. generate the doubledBits lookup table when we need it
 * using doubledBitsNibble.
 */
static uint8_t __aligned(4) doubledBitsNibble[16] = {0x00, 0x03, 0x0c, 0x0f, 0x30, 0x33, 0x3c, 0x3f,
                                                     0xc0, 0xc3, 0xcc, 0xcf, 0xf0, 0xf3, 0xfc, 0xff};

/* lookup for doubling pixel patterns in mag mode */
static PICO9918_SECTION_SCRATCH_X(lookup) uint16_t __aligned(4) doubledBits[256];
static void PICO9918_IN_FLASH_FUNC(doubledBitsInit)(void)
{
  for (int i = 0; i < 256; ++i)
  {
    doubledBits[i] = (doubledBitsNibble[(i & 0xf0) >> 4] << 8) | doubledBitsNibble[i & 0x0f];
  }
}

/* reversed bits in a byte */
static PICO9918_SECTION_SCRATCH_X(lookup) uint8_t __aligned(4) reversedBits[256];

static uint8_t PICO9918_IN_FLASH_FUNC(reverseBits)(uint8_t byte)
{
  byte = (byte & 0xf0) >> 4 | (byte & 0x0f) << 4;
  byte = (byte & 0xcc) >> 2 | (byte & 0x33) << 2;
  return (byte & 0xaa) >> 1 | (byte & 0x55) << 1;
}

/* the same reversal a text cell wants: six bits, so the two pixels it never shows fall off the
   bottom and the mirror lands back at bit 7. Folding the shift into the table saves a shift and a
   truncation on each of the three planes and the mask - paid only by a flipped cell, and a row of
   those is the most expensive row a text mode has. */
static PICO9918_SECTION_SCRATCH_X(lookup) uint8_t __aligned(4) reversedBits6[256];

static void PICO9918_IN_FLASH_FUNC(reversedBitsInit)(void)
{
  for (int i = 0; i < 256; ++i)
  {
    reversedBits[i]  = reverseBits(i);
    reversedBits6[i] = reverseBits(i) << 2;
  }
}

/* a 6-bit palette index applied to all four bytes of a uint32_t, which is one multiply and wants no
   table at all.

   Spelling it as a multiply is what makes that true. Left to itself GCC expands the constant
   multiply into a run of shifts and adds, because materialising the constant that way costs nothing
   - the right call for a cold caller and the wrong one for three hot ones. `mul` rather than
   `muls`: GCC wraps inline asm in `.syntax divided`, where the Thumb-1 multiply takes two operands
   and always sets the flags. The C arm keeps the host build and the init-time constant folding at
   ecm0PaletteInit. */
static inline uint32_t repeatedPalette(const uint32_t index)
{
#ifdef PICO_BUILD
  uint32_t repeated = index;
  __asm__("mul %0, %1" : "+l"(repeated) : "l"(0x01010101u));
  return repeated;
#else
  return index * 0x01010101u;
#endif
}

/* The same value, except that colour 0 of each sub-palette holds what a tile writes where it draws
   nothing - so this one cannot be arithmetic. ECM0 tiles index it, and transparency then costs them
   no test: `pal` is always a multiple of 16, so those four entries are exactly the zero colours. */
static PICO9918_SECTION_SCRATCH_X(lookup) uint32_t __aligned(4) ecm0Palette[64];

static void PICO9918_IN_FLASH_FUNC(ecm0PaletteInit)(void)
{
  for (int i = 0; i < 64; ++i)
  {
    ecm0Palette[i] = repeatedPalette(i);
  }
}

/* What a tile layer writes where it draws nothing. Hardware marks a zero tile colour as not-a-pixel
   and falls through to the backdrop; our layer buffer carries no such bit, so the backdrop colour
   goes in directly. The exception is a non-priority bitmap layer, where zero is the composite's own
   transparency marker and letting the layer show through matters more. Decided once per scanline. */
static uint32_t transparentPixels[2];

/* a lookup from a 4-bit mask to a word of 8-bit masks (reversed byte order) */
static PICO9918_SECTION_SCRATCH_X(lookup) uint32_t __aligned(4) maskExpandNibbleToWordRev[16] = {
  0x00000000, 0xff000000, 0x00ff0000, 0xffff0000, 0x0000ff00, 0xff00ff00, 0x00ffff00, 0xffffff00,
  0x000000ff, 0xff0000ff, 0x00ff00ff, 0xffff00ff, 0x0000ffff, 0xff00ffff, 0x00ffffff, 0xffffffff};

bool lookupsReady = false;
void PICO9918_IN_FLASH_FUNC(initLookups)(void)
{
  if (lookupsReady) return;

  /* Before anything else: the host's VGA layer claims its own channels at vgaInit,
     and a collision has to surface here rather than once the display is running. */
  PICO9918_DMA_CLAIM();

  ecmLookupInit();
  doubledBitsInit();
  reversedBitsInit();
  ecm0PaletteInit();

  /* Every instance is configured here, never lazily from the scanline path: one
     triggered before a lazy init would reach it fires unconfigured. The border
     fill is the frame module's, so its source word is exported rather than
     reached for as a static in another TU. */
  PICO9918_FILL32_INIT(PICO9918_FILL_LINE, &bg);
  PICO9918_FILL32_SET_COUNT(PICO9918_FILL_LINE, TMS9918_PIXELS_X / 4);

  PICO9918_FILL32_INIT(PICO9918_FILL_MASKS, &zeroWord);
  PICO9918_FILL32_SET_COUNT(PICO9918_FILL_MASKS, sizeof(rowMasks) / sizeof(uint32_t));

  PICO9918_FILL32_INIT(PICO9918_FILL_BORDER, &pico9918_border_bg);

  PICO9918_COPY_INIT(PICO9918_COPY);

  lookupsReady = true;
}

/* a tile plane's byte split into its two quads: the high nibble - the cell's left four pixels - at
 * bit 16, the low nibble at bit 0. Three of these OR together into one accumulator holding both of
 * the cell's ecmLookup indices, `index >> 16` and `(uint16_t)index`.
 */
static inline uint32_t ecmSplitQuads(const uint32_t patt)
{
  return (patt | (patt << 12)) & 0x000f000fu;
}

/* the index into ecmLookup for four sprite pixels, from the three left-aligned plane words: each
 * plane's top nibble is this quad's bit for that plane, `sb0` being plane 1. The tile path's `patt`
 * numbers them the other way, plane 3 first, and indexes the same table.
 *
 * Not for correctness - the planes above `ecm` are zero and only ever shifted - but for shape:
 * `ecm` is a scanline invariant, so GCC unswitches the emit loops on it and each level gets a
 * straight-line body with the unused planes' terms dead.
 */
static inline uint32_t calculateEcmIndex(const uint32_t ecm, const uint32_t sb0, const uint32_t sb1,
                                         const uint32_t sb2)
{
  uint32_t ecmIndex = 0;
  switch (ecm)
  {
  case 3:
    ecmIndex = sb2 >> 28;
    // fallthrough
  case 2:
    ecmIndex = (ecmIndex << 4) | (sb1 >> 28);
    // fallthrough
  default: ecmIndex = (ecmIndex << 4) | (sb0 >> 28);
  }
  return ecmIndex;
}

static inline void loadSpriteData(const uint8_t* vram, uint32_t* spriteBits, uint32_t pattOffset,
                                  uint32_t* pattMask, const uint32_t ecm, const uint32_t ecmOffset,
                                  const bool flipX, const bool sprite16)
{
  int i = 0;
  do // do-while since behavior for ecm=0 and ecm==1 is the same
  {
    /* every plane is stored, a blank one included: a zero byte shifts to a zero word and reverses
       to one, so testing for it only bought a shift and cost a branch - and leaving the blank
       planes unwritten is what made the caller zero the array, which it did by calling memset */
    uint32_t patt = vram[pattOffset];
    if (flipX) patt = reversedBits[patt];
    uint32_t bits = patt << ((flipX && sprite16) ? 16 : 24);

    if (sprite16)
    {
      patt = vram[pattOffset + PATTERN_BYTES * 2];
      if (flipX) patt = reversedBits[patt];
      bits |= patt << (flipX ? 24 : 16);
    }
    spriteBits[i] = bits;
    *pattMask |= bits;
    pattOffset += ecmOffset;
  } while (++i < ecm);
}


/* The sprites this scanline draws, in list order. Each carries its attribute with the row inside
   the pattern in place of the y, which the drawing pass does not need - the collect pass has the
   whole word in a register for the zero test anyway, so keeping it spares that pass three reads of
   VRAM, which shares its bank with the DMA and the PIO. The index is only ever read to report a
   fifth sprite, so it sits apart rather than widening the record every sprite pays for. */
static PICO9918_SECTION_SCRATCH_X(lookup) uint32_t spriteAttrRows[MAX_SPRITES];
static PICO9918_SECTION_SCRATCH_X(lookup) uint8_t spriteIndices[MAX_SPRITES];

/**
 * \brief Which sprites this scanline draws at all, and where in each pattern it starts.
 *
 * The y tests want the scanline, the wrap threshold and the table bounds. The drawing pass wants
 * the ECM settings, the palette and the pattern table. Neither wants the other's, and together
 * they are more than the eight registers hold - so the list is walked once here and the drawing
 * pass reads a row at a time instead of carrying both sets through every sprite.
 */
static uint32_t __time_critical_func(collectSpriteRows)(PICO9918_INST_ARG uint16_t y)
{
  const uint32_t unlockedMask = -(uint32_t)PICO9918_UNLOCKED(tms9918);
  const uint32_t row30Mode    = (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED1) & 0x40) & unlockedMask;
  /* the first row is YPOS -1, and both the wrap threshold and the row it lands on carry that
     offset, so the list walks raw YPOS and never adds it */
  const int32_t realY = (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED1) & 0x08) ? 0 : 1;
  const int32_t maxY  = (row30Mode ? 0xf0 : 0xe0) - realY;
  const int32_t yAdj  = (int32_t)y - realY;

  uint32_t maxSprites = TMS_REGISTER(tms9918, PICO9918_REG_MAX_SPRITES);
  if (maxSprites > MAX_SPRITES) maxSprites = MAX_SPRITES;

  const uint8_t* spriteAttr = tms9918->vram.bytes + tmsSpriteAttrTableAddr(tms9918);
  uint32_t count            = 0;

  for (uint32_t spriteIdx = 0; spriteIdx < maxSprites; ++spriteIdx, spriteAttr += SPRITE_ATTR_BYTES)
  {
    int32_t yPos = spriteAttr[SPRITE_ATTR_Y];

    /* stop processing when yPos == LAST_SPRITE_YPOS */
    if (yPos == LAST_SPRITE_YPOS && !row30Mode)
    {
      break;
    }

    /* check if sprite position is in the -31 to 0 range and move back to top */
    if (yPos > maxY) yPos -= 256;

    const int32_t pattRow = yAdj - yPos;
    if ((uint32_t)pattRow > 31)
    {
      continue;
    }

    const uint32_t attr = *(const uint32_t*)spriteAttr;

    /* Kidd-proofing: not strictly correct, however some F18A games (lookin' at you, Kidd)
       sometimes have all sprites enabled with all zeros and that hurts us :( An all-zero
       attribute is only ever on the handful of rows its zero y reaches, so the whole list
       pays the test for it here rather than above the row check */
    if (attr == 0 && unlockedMask)
    {
      continue;
    }

    spriteAttrRows[count]                             = attr;
    ((uint8_t*)&spriteAttrRows[count])[SPRITE_ATTR_Y] = (uint8_t)pattRow;
    spriteIndices[count]                              = (uint8_t)spriteIdx;
    ++count;
  }

  return count;
}

/** \brief Output Sprites to a scanline */
static inline uint8_t __time_critical_func(renderSprites)(PICO9918_INST_ARG const uint32_t spriteCount,
                                                          const bool spriteMag, const bool wide,
                                                          uint8_t pixels[TMS9918_PIXELS_X])
{
  /* The instance is a global, and the emit loop writes bytes into `pixels` - a character store may
     alias anything, so the pointer has to be read from memory again for every sprite. Nothing below
     touches the instance inside the loop, which lets it stay in a register for the whole scanline.
     `unlockedMask` is 0 or ~0 so the settings it gates fold into an AND rather than a branch. */
  const uint32_t unlockedMask      = -(uint32_t)PICO9918_UNLOCKED(tms9918);
  const uint8_t* const vram        = tms9918->vram.bytes;
  bool hasSprites                  = false;
  const uint8_t spriteSize         = tmsSpriteSize(tms9918);
  const bool sprite16              = spriteSize == 16;
  const uint8_t spriteIdxMask      = sprite16 ? 0xfc : 0xff;
  const uint8_t spriteColorMask    = 0x8f | unlockedMask;
  const uint8_t spriteSizePx       = spriteSize << spriteMag;
  const uint16_t spritePatternAddr = tmsSpritePatternTableAddr(tms9918);
  uint32_t spritesShown            = 0;
  /* the sprite-number field reads zero unless a fifth sprite latches one into it: the
     scan counter is parked at zero whenever it is not walking the attribute table */
  uint8_t tempStatus               = 0;
  uint32_t transparentCount        = 0;

  // ecm settings
  const uint32_t ecm            = (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED1) & 0x03) & unlockedMask;
  const uint32_t ecmColorOffset = (ecm == 3) ? 2 : ecm;
  const uint32_t ecmColorMask   = (ecm == 3) ? 0x0e : 0x0f;
  const uint32_t ecmOffset      = 0x800 >> ((TMS_REGISTER(tms9918, PICO9918_REG_PAGE_SIZE) & 0xc0) >> 6);

  uint8_t pal = (TMS_REGISTER(tms9918, PICO9918_REG_PALETTE_SELECT) & 0x30) & unlockedMask;
  if (ecm == 1)
  {
    pal &= 0x20;
  }
  else if (ecm)
  {
    pal = 0;
  }

  const uint32_t scanlineSprites = TMS_REGISTER(tms9918, PICO9918_REG_MAX_SCAN_SPRITES);
  const uint32_t unlimited       = (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) & 0x08) & unlockedMask;

  for (uint32_t n = 0; n < spriteCount; ++n)
  {
    const uint8_t* spriteAttr = (const uint8_t*)&spriteAttrRows[n];

    int32_t pattRow = spriteAttr[SPRITE_ATTR_Y] >> spriteMag;

    uint8_t thisSpriteSize    = spriteSize;
    bool thisSprite16         = sprite16;
    uint8_t thisSpriteIdxMask = spriteIdxMask;
    uint8_t thisSpriteSizePx  = spriteSizePx;
    uint8_t spriteAttrColor   = spriteAttr[SPRITE_ATTR_COLOR] & spriteColorMask;
    bool opaq                 = false;

    if (spriteAttrColor & 0x10)
    {
      if (sprite16)
      {
        // PICO9918-specific. If all sprites are 16px anyway, this bit is used to have opaque sprites
        opaq = true;
      }
      else
      {
        thisSpriteSize    = 16;
        thisSprite16      = true;
        thisSpriteIdxMask = 0xfc;
        thisSpriteSizePx  = thisSpriteSize << spriteMag;
      }
    }

    /* check if sprite is visible on this line */
    if (pattRow >= thisSpriteSize)
    {
      continue;
    }

    /* have we exceeded the scanline sprite limit? */
    if (++spritesShown > MAX_SCANLINE_SPRITES)
    {
      if (((tempStatus & STATUS_5S) == 0) && (!unlimited || spritesShown > scanlineSprites))
      {
        tempStatus |= STATUS_5S | spriteIndices[n];
      }

      if (spritesShown > scanlineSprites) break;
    }

    const int32_t earlyClockOffset = (spriteAttrColor & 0x80) ? -32 : 0;
    int32_t xPos                   = (int32_t)(spriteAttr[SPRITE_ATTR_X]) + earlyClockOffset;
    if ((xPos > TMS9918_PIXELS_X) || (-xPos > thisSpriteSizePx))
    {
      continue;
    }

    if (spriteAttrColor & 0x20) pattRow = thisSpriteSize - pattRow - 1; // flip Y?

    /* sprite is visible on this line */
    uint8_t spriteColor   = (spriteAttrColor & ecmColorMask) << ecmColorOffset;
    const uint8_t pattIdx = spriteAttr[SPRITE_ATTR_NAME] & thisSpriteIdxMask;
    uint16_t pattOffset   = spritePatternAddr + pattIdx * PATTERN_BYTES + (uint16_t)pattRow;


    /* create a 32-bit mask of this sprite's pixels
     * left-aligned, so the first pixel in the sprite is the
     * MSB of spriteBits
     */
    uint32_t pattMask = 0;
    /* one left-aligned word per ecm plane. Zeroed because the clip below shifts all three
       whatever the level uses, while loadSpriteData writes only the ones it has. */
    uint32_t spriteBits[3] = {0};
    const bool flipX = spriteAttrColor & 0x40;

    loadSpriteData(vram, spriteBits, pattOffset, &pattMask, ecm, ecmOffset, flipX, thisSprite16);

    if (opaq) pattMask = 0xffff0000;

    /* bail early if no bits to draw */
    if (!pattMask)
    {
      continue;
    }

    if (spriteMag)
    {
      pattMask = ((uint32_t)doubledBits[pattMask >> 24] << 16) | doubledBits[(pattMask >> 16) & 0xff];
    }

    /* perform clipping operations */
    if (xPos < 0)
    {
      int32_t absX    = -xPos;
      uint32_t offset = absX >> spriteMag;
      spriteBits[2] <<= offset;
      spriteBits[1] <<= offset;
      spriteBits[0] <<= offset;
      pattMask <<= absX;

      /* bail early if no bits to draw */
      if (!pattMask)
      {
        continue;
      }

      thisSpriteSizePx += xPos;
      xPos = 0;
    }

    int pixelsLeft = TMS9918_PIXELS_X - xPos;
    if (pixelsLeft < thisSpriteSizePx)
    {
      thisSpriteSizePx = pixelsLeft;
      pattMask &= ~((1u << (32 - pixelsLeft)) - 1);
    }

    /* test and update the collision mask */
    uint32_t validPixels = tmsTestCollisionMask(xPos, pattMask, thisSpriteSizePx);

    /* if the result is different, we collided */
    if (validPixels != pattMask)
    {
      tempStatus |= STATUS_COL;
    }

    // Render valid pixels to the scanline
    if (ecm || (spriteColor != TMS_TRANSPARENT))
    {
      hasSprites = true;
      spriteColor |= pal;
      if (ecm)
      {

        /* Note: Again, I've made the choice to branch early for some of the sprite options
              to improve performance for each case (reduce branches in loops) */
        uint32_t quadPal = repeatedPalette(spriteColor);

        /* magnified: a quad of the pattern is four colours and eight screen pixels, so it is emitted
           where it is generated. Staging it in a buffer and copying afterwards cost four word
           stores, sixteen byte loads and a second loop, for a pair of pixels that share one colour
           and can be stored as they come. A byte-wide store is what an arbitrary sprite x asks for
           anyway - placing whole words needs the pixels doubled first, which measured dearer than
           the stores it saved. */
        if (spriteMag)
        {
          /* a sprite pixel is one byte on the 256-pixel grid and two where a tile line is twice as
             wide, the sprite grid not doubling with the depth */
          uint8_t* p          = pixels + (wide ? xPos * 2 : xPos);
          const uint32_t step = wide ? 2 : 1;
          uint32_t bits       = validPixels;

          while (bits)
          {
            if (bits >> 24)
            {
              const uint32_t ecmIndex = calculateEcmIndex(ecm, spriteBits[0], spriteBits[1], spriteBits[2]);
              uint32_t quad           = ecmLookup[ecmIndex] | quadPal;

              for (int n = 0; n < 4; ++n)
              {
                const uint8_t v = (uint8_t)quad;
                if (bits & MASK_NEXT_PIXEL)
                {
                  if (wide)
                    *(uint16_t*)p = v | (v << 8);
                  else
                    p[0] = v;
                }
                bits <<= 1;
                if (bits & MASK_NEXT_PIXEL)
                {
                  if (wide)
                    *(uint16_t*)(p + 2) = v | (v << 8);
                  else
                    p[1] = v;
                }
                bits <<= 1;
                p += 2 * step;
                quad >>= 8;
              }
            }
            else
            {
              bits <<= 8;
              p += 8 * step;
            }
            spriteBits[2] <<= 4;
            spriteBits[1] <<= 4;
            spriteBits[0] <<= 4;
          }
        }
        else if (wide)
        {
          /* Four pixels still come out of one lookup, but eight bytes cannot be one store and a word
             of byte masks cannot gate them. So the quad is stored pixel by pixel and the word
             alignment below is not worth arranging - which also means no shifting of `validPixels`
             or the planes to reach it. */
          uint32_t x = xPos;

          while (validPixels)
          {
            uint32_t chunkMask = validPixels >> 28;
            if (chunkMask)
            {
              const uint32_t ecmIndex = calculateEcmIndex(ecm, spriteBits[0], spriteBits[1], spriteBits[2]);
              uint32_t color          = ecmLookup[ecmIndex] | quadPal;
              uint8_t* q              = pixels + x * 2;

              for (int n = 0; n < 4; ++n)
              {
                if (chunkMask & 0x8)
                {
                  const uint8_t v = (uint8_t)color;
                  *(uint16_t*)q   = v | (v << 8);
                }
                chunkMask <<= 1;
                color >>= 8;
                q += 2;
              }
            }
            spriteBits[2] <<= 4;
            spriteBits[1] <<= 4;
            spriteBits[0] <<= 4;
            x += 4;
            validPixels <<= 4;
          }
        }
        else // regular ecm sprite (8 or 16px, non-magnified)
        {

          // get him to be word aligned so we can smash out 4 pixels at a time
          uint32_t quadOffset      = xPos >> 2;
          const uint32_t pixOffset = xPos & 0x3;
          validPixels >>= pixOffset;
          spriteBits[2] >>= pixOffset;
          spriteBits[1] >>= pixOffset;
          spriteBits[0] >>= pixOffset;

          uint32_t* quadPixels = (uint32_t*)pixels;

          while (validPixels)
          {
            /* output the sprite 4 pixels at a time. A solid quad owns the whole word, which is most
               of a sprite that is not its edge - the read, the two masks and the merge are all for
               the pixels it does not cover. Both arms settle what they need of `chunkMask` before
               the index, so it is dead by then: held across it, it is the value that costs the
               third bit plane its register. */
            uint32_t chunkMask = validPixels >> 28;
            if (chunkMask == 0x0f)
            {
              const uint32_t ecmIndex = calculateEcmIndex(ecm, spriteBits[0], spriteBits[1], spriteBits[2]);
              quadPixels[quadOffset]  = ecmLookup[ecmIndex] | quadPal;
            }
            else if (chunkMask)
            {
              const uint32_t maskQuad = maskExpandNibbleToWordRev[chunkMask];
              const uint32_t ecmIndex = calculateEcmIndex(ecm, spriteBits[0], spriteBits[1], spriteBits[2]);
              const uint32_t color    = ecmLookup[ecmIndex] | quadPal;
              quadPixels[quadOffset]  = (quadPixels[quadOffset] & ~maskQuad) | (color & maskQuad);
            }
            spriteBits[2] <<= 4;
            spriteBits[1] <<= 4;
            spriteBits[0] <<= 4;
            ++quadOffset;
            validPixels <<= 4;
          }
        }
      }
      else // non-ecm single-color sprite
      {
        /* a packed 80-column line holds two pixels in a byte, so one store covers both; an 8bpp one
           holds one, so the same pixel pair is a halfword - and `xPos * 2` is even, so it is aligned
           without arranging anything */
        if (!wide && tmsCachedMode == TMS_MODE_TEXT80) spriteColor |= spriteColor << 4;

        while (validPixels)
        {
          if ((int32_t)validPixels < 0)
          {
            if (wide)
              *(uint16_t*)(pixels + xPos * 2) = spriteColor | (spriteColor << 8);
            else
              pixels[xPos] = spriteColor;
          }
          validPixels <<= 1;
          ++xPos;
        }
      }
    }
    else
    {
      // keep track of the transparent sprites, we remove them from the sprite mask later
      tmsSetTransparentSpriteMask(xPos, validPixels, thisSpriteSizePx);
      ++transparentCount;
    }
  }

  tms9918->scanlineHasSprites = hasSprites;

  // remove the transparent sprite pixels if there are any
  if (transparentCount)
  {
    for (int i = 0; i < 9; ++i)
    {
      rowMasks.rowSpriteBits[i] ^= rowMasks.rowTransparentSpriteBits[i];
    }
  }


  return tempStatus;
}

static EMITTER_NOINLINE uint8_t
__time_critical_func(pico9918_output_sprites)(PICO9918_INST_ARG uint16_t y, uint8_t pixels[TMS9918_PIXELS_X])
{
  const bool spriteMag = tmsSpriteMag(tms9918);

  if (TMS_REGISTER(tms9918, TMS_REG_0) & R0_DOUBLE_ROWS) // double rows (high-res)? still only have low-res sprites
    y >>= 1;

  const uint32_t spriteCount = collectSpriteRows(PICO9918_INST y);

#if PICO9918_TEXT80_8BPP
  /* the store width is inside the emit loop, so it rides a clone parameter rather than a test */
  if (TEXT80_WIDE_ROW)
  {
    return spriteMag ? renderSprites(PICO9918_INST spriteCount, true, true, pixels)
                     : renderSprites(PICO9918_INST spriteCount, false, true, pixels);
  }
#endif

  if (spriteMag)
  {
    return renderSprites(PICO9918_INST spriteCount, true, false, pixels);
  }
  else
  {
    return renderSprites(PICO9918_INST spriteCount, false, false, pixels);
  }
}

/* What a tile row needs that the mode, rather than the layer, decides. The name and colour
 * addresses stay out of it: the row loop walks them as it crosses a page boundary.
 */
typedef struct
{
  const uint8_t* pattern; /* pattern table + this row; index with name * PATTERN_BYTES */
  int8_t flipY;           /* what the ECM attribute's Y flip adds, or 0 where it is inert */
  uint8_t nameMask;
} TileRowAddr;

/**
 * \brief the per-mode half of a tile row's addressing, once per layer per scanline.
 *
 * `y` is the scrolled raster row and `rawY` the unscrolled one. Multicolor is the only mode that
 * needs both, and it needs them apart: its name address uses the scrolled row while its pattern
 * byte comes from the raw one, so a vertical scroll changes which
 * tiles are fetched but not which four-line block of each is shown.
 */
static inline void tileRowAddr(PICO9918_INST_ARG const uint16_t y, const uint16_t rawY, const uint8_t colorReg,
                               const bool gm2, const bool mcm, TileRowAddr* addr, uint16_t* colorTableAddr)
{
  uint16_t pageOffset   = 0;
  const uint8_t pattRow = mcm ? (((rawY >> 2) & 0x01) + ((rawY >> 3) & 0x03) * 2) : (y & 0x07);

  addr->nameMask = 0xff;
  /* Y flip is inert in Multicolor for the same reason: the flip lives in the scrolled row, which
     its pattern address never reads. Zero here rather than a test per tile. */
  addr->flipY = mcm ? 0 : (7 - 2 * pattRow);

  if (gm2)
  {
    /* the pattern third follows the scrolled row; the colour table follows
       it only where the register says so, and colours a tile row rather than a name */
    pageOffset     = (((y >> 6) & 0x03) & (TMS_REGISTER(tms9918, TMS_REG_PATTERN_TABLE) & 0x03)) << 11;
    addr->nameMask = ((colorReg & 0x7f) << 3) | 0x07;
    *colorTableAddr += (pageOffset & ((colorReg & 0x60) << 6)) + pattRow;
  }

  addr->pattern = tms9918->vram.bytes + tmsPatternTableAddr(tms9918) + pageOffset + pattRow;
}

/* Which cell a scrolled text row starts on. Graphics cells are eight pixels wide so the scroll
   register splits by shifting; six does not divide, so hardware multiplies by the reciprocal
   instead, exactly floor(h/6) for every value the register holds. The
   pixel within that cell is what is left: h - 6 * cell. 80 columns doubles h first, its cells
   being half as wide, which is why its offset is only ever 0, 2 or 4 (:741). */
static inline uint32_t textScrollCell(const uint32_t hscrollPixels)
{
  return (hscrollPixels * 342) >> 11;
}

/* 80 columns double the register before dividing, their cells being half as wide, and what is left
   inside the first cell is an even number of pixels - a whole byte at four bits a pixel, which is
   what lets that depth place the offset by moving the destination. Both depths and both
   column counts come through here so the emitter and the composite cannot disagree about it. */
static inline uint32_t textStartCell(const uint32_t hscroll, const bool wide)
{
  return textScrollCell(wide ? hscroll * 2 : hscroll);
}

static inline uint32_t textPixelOffset(const uint32_t hscroll, const bool wide)
{
  const uint32_t h      = wide ? hscroll * 2 : hscroll;
  const uint32_t offset = h - textScrollCell(h) * 6;
  return wide ? (offset & ~1u) : offset;
}

static inline int scrollOffset(const uint32_t hscroll, const bool text, const bool wide)
{
  return text ? (int)textPixelOffset(hscroll, wide) : (int)(hscroll & 0x07);
}

typedef struct
{
  uint8_t vertScrollReg;
  uint8_t yPageSwapMask;
  uint8_t paletteShift;
  uint8_t paletteMask;
  uint8_t startPattReg;
  uint8_t hpSizeMask;
  uint8_t priorityReg;
  uint8_t priorityMask;
  uint8_t colorTableReg;
  bool isTile2;
  uint16_t (*nameTableAddrFunc)(pico9918_t*);
  uint16_t (*colorTableAddrFunc)(pico9918_t*);
} TileLayerConfig;

static const TileLayerConfig T1_CONFIG = {.vertScrollReg      = 0x1c,
                                          .yPageSwapMask      = 0x01,
                                          .paletteShift       = 4,
                                          .paletteMask        = 0x03,
                                          .startPattReg       = 0x1b,
                                          .hpSizeMask         = 0x02,
                                          .priorityReg        = 0, // T1 has no priority control
                                          .priorityMask       = 0,
                                          .colorTableReg      = TMS_REG_COLOR_TABLE,
                                          .isTile2            = false,
                                          .nameTableAddrFunc  = tmsNameTableAddr,
                                          .colorTableAddrFunc = tmsColorTableAddr};

static const TileLayerConfig T2_CONFIG = {.vertScrollReg      = 0x1a,
                                          .yPageSwapMask      = 0x10,
                                          .paletteShift       = 2,
                                          .paletteMask        = 0x0c,
                                          .startPattReg       = 0x19,
                                          .hpSizeMask         = 0x20,
                                          .priorityReg        = 0x32,
                                          .priorityMask       = 0x01,
                                          .colorTableReg      = 11,
                                          .isTile2            = true,
                                          .nameTableAddrFunc  = tmsNameTable2Addr,
                                          .colorTableAddrFunc = tmsColorTable2Addr};

/* Where a scrolled 80-column row starts. Hardware doubles the scroll register before dividing by
 * six, T80 cells being half as wide, so the offset it leaves inside the first cell is only ever 0,
 * 2 or 4 pixels - a whole number of bytes at 4bpp, and never a nibble.
 *
 * `startCol` and `cells` are for the aligned emitter, which stores three words per four cells and
 * so cannot begin part-way through one: it backs up `bytes` cells and the caller moves the
 * destination four bytes for each, leaving the picture where it should be and the cells backed over
 * in the side border.
 */
typedef struct
{
  uint16_t startCell; /* the first cell the row shows */
  uint16_t startCol;  /* the first cell it emits */
  uint8_t cells;
  uint8_t bytes; /* how far into the first cell the row starts */
  bool scrolled;
} TextScroll;

static inline TextScroll textScroll80(PICO9918_INST_ARG const TileLayerConfig* config)
{
  const uint32_t hscroll =
    PICO9918_UNLOCKED(tms9918) ? TMS_REGISTER(tms9918, config->startPattReg) * 2 : 0;
  const uint32_t cell    = textScrollCell(hscroll);
  const uint32_t bytes   = (hscroll - cell * 6) >> 1;

  TextScroll s;
  s.startCell = cell;
  s.startCol  = (cell >= bytes) ? (cell - bytes) : (cell + TEXT80_NUM_COLS - bytes);
  s.cells     = bytes ? TEXT80_NUM_COLS + 4 : TEXT80_NUM_COLS;
  s.bytes     = bytes;
  s.scrolled  = hscroll != 0;
  return s;
}

/**
 * \brief where one layer reads this scanline from: the vertical scroll and its page swap, the name and
 * colour rows, and the mode's pattern table. Every mode and both layers come through here, which
 * is what stops the scroll from being written a fourth time.
 *
 * Position attributes decide the attribute row offset for every mode alike - by name when they are
 * off, ECM0 or not. `textMode` selects only what a text row does not have: the page bits, in
 * either direction.
 */
static inline void tileLayerAddr(PICO9918_INST_ARG const uint16_t rawY, const TileLayerConfig* config,
                                 const uint8_t numCols, const uint8_t nameAddrMask, const bool textMode,
                                 const bool gm2, const bool mcm, const bool attrPerPos, TileRowAddr* addr,
                                 uint16_t* namesAddr, uint16_t* colorAddr)
{
  uint16_t y     = rawY;
  bool swapYPage = false;

  if (TMS_REGISTER(tms9918, config->vertScrollReg))
  {
    int virtY = y + TMS_REGISTER(tms9918, config->vertScrollReg);
    /* the field wraps at the height actually rendered, which R0's row doubling
       doubles - 384 or 480 rather than 192 or 240 */
    const int maxY = ((TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED1) & 0x40) ? (8 * 30) : (8 * 24))
                     << (bool)(TMS_REGISTER(tms9918, TMS_REG_0) & R0_DOUBLE_ROWS);

    if (virtY >= maxY)
    {
      virtY -= maxY;
      /* graphics only: a text row's address is a row count times a column count and never carries
         the page bit, so the size bit does nothing */
      swapYPage = !textMode && (TMS_REGISTER(tms9918, PICO9918_REG_PAGE_SIZE) & config->yPageSwapMask);
    }

    y = virtY;
  }

  const uint16_t rowOffset = (y >> 3) * numCols;

  *namesAddr = (config->nameTableAddrFunc(tms9918) & (nameAddrMask << 10)) + rowOffset;
  if (swapYPage) *namesAddr ^= 0x800;

  *colorAddr = config->colorTableAddrFunc(tms9918);
  if (attrPerPos)
  {
    /* The scroll page bits are *added* to the attribute base, carries and all - not written over
       the bits it already has. A base with bits of its own keeps them, and bit 10 carrying into
       bit 11 is a real part of the address rather than an overflow to mask off.
       A text row's attribute address is its name address's own form and carries no page bit. */
    if (!textMode) *colorAddr += *namesAddr & 0xc00;
    *colorAddr = (*colorAddr + rowOffset) & VRAM_MASK;
  }

  tileRowAddr(PICO9918_INST y, rawY, TMS_REGISTER(tms9918, config->colorTableReg), gm2, mcm, addr, colorAddr);
}

#define TEXT80_COLOR_WORD(n) ((uint32_t)((n) * 0x111111u))
#define TEXT80_MASK_WORD(bits) \
  (((uint32_t)((bits) & 0x20 ? 0x0000F0u : 0x0)) | ((uint32_t)((bits) & 0x10 ? 0x00000Fu : 0x0)) | \
   ((uint32_t)((bits) & 0x08 ? 0x00F000u : 0x0)) | ((uint32_t)((bits) & 0x04 ? 0x000F00u : 0x0)) | \
   ((uint32_t)((bits) & 0x02 ? 0xF00000u : 0x0)) | ((uint32_t)((bits) & 0x01 ? 0x0F0000u : 0x0)))

static const uint32_t text80ColorWord[16] = {
  TEXT80_COLOR_WORD(0x0), TEXT80_COLOR_WORD(0x1), TEXT80_COLOR_WORD(0x2), TEXT80_COLOR_WORD(0x3),
  TEXT80_COLOR_WORD(0x4), TEXT80_COLOR_WORD(0x5), TEXT80_COLOR_WORD(0x6), TEXT80_COLOR_WORD(0x7),
  TEXT80_COLOR_WORD(0x8), TEXT80_COLOR_WORD(0x9), TEXT80_COLOR_WORD(0xa), TEXT80_COLOR_WORD(0xb),
  TEXT80_COLOR_WORD(0xc), TEXT80_COLOR_WORD(0xd), TEXT80_COLOR_WORD(0xe), TEXT80_COLOR_WORD(0xf)};

/* the low two pattern bits never reach the screen, so indexing by the raw pattern byte and
   repeating each entry four times spends table space to save a shift on every cell */
#define TEXT80_MASK_WORD4(bits) \
  TEXT80_MASK_WORD(bits), TEXT80_MASK_WORD(bits), TEXT80_MASK_WORD(bits), TEXT80_MASK_WORD(bits)

static const uint32_t text80MaskWord[256] = {
  TEXT80_MASK_WORD4(0x00), TEXT80_MASK_WORD4(0x01), TEXT80_MASK_WORD4(0x02), TEXT80_MASK_WORD4(0x03),
  TEXT80_MASK_WORD4(0x04), TEXT80_MASK_WORD4(0x05), TEXT80_MASK_WORD4(0x06), TEXT80_MASK_WORD4(0x07),
  TEXT80_MASK_WORD4(0x08), TEXT80_MASK_WORD4(0x09), TEXT80_MASK_WORD4(0x0a), TEXT80_MASK_WORD4(0x0b),
  TEXT80_MASK_WORD4(0x0c), TEXT80_MASK_WORD4(0x0d), TEXT80_MASK_WORD4(0x0e), TEXT80_MASK_WORD4(0x0f),
  TEXT80_MASK_WORD4(0x10), TEXT80_MASK_WORD4(0x11), TEXT80_MASK_WORD4(0x12), TEXT80_MASK_WORD4(0x13),
  TEXT80_MASK_WORD4(0x14), TEXT80_MASK_WORD4(0x15), TEXT80_MASK_WORD4(0x16), TEXT80_MASK_WORD4(0x17),
  TEXT80_MASK_WORD4(0x18), TEXT80_MASK_WORD4(0x19), TEXT80_MASK_WORD4(0x1a), TEXT80_MASK_WORD4(0x1b),
  TEXT80_MASK_WORD4(0x1c), TEXT80_MASK_WORD4(0x1d), TEXT80_MASK_WORD4(0x1e), TEXT80_MASK_WORD4(0x1f),
  TEXT80_MASK_WORD4(0x20), TEXT80_MASK_WORD4(0x21), TEXT80_MASK_WORD4(0x22), TEXT80_MASK_WORD4(0x23),
  TEXT80_MASK_WORD4(0x24), TEXT80_MASK_WORD4(0x25), TEXT80_MASK_WORD4(0x26), TEXT80_MASK_WORD4(0x27),
  TEXT80_MASK_WORD4(0x28), TEXT80_MASK_WORD4(0x29), TEXT80_MASK_WORD4(0x2a), TEXT80_MASK_WORD4(0x2b),
  TEXT80_MASK_WORD4(0x2c), TEXT80_MASK_WORD4(0x2d), TEXT80_MASK_WORD4(0x2e), TEXT80_MASK_WORD4(0x2f),
  TEXT80_MASK_WORD4(0x30), TEXT80_MASK_WORD4(0x31), TEXT80_MASK_WORD4(0x32), TEXT80_MASK_WORD4(0x33),
  TEXT80_MASK_WORD4(0x34), TEXT80_MASK_WORD4(0x35), TEXT80_MASK_WORD4(0x36), TEXT80_MASK_WORD4(0x37),
  TEXT80_MASK_WORD4(0x38), TEXT80_MASK_WORD4(0x39), TEXT80_MASK_WORD4(0x3a), TEXT80_MASK_WORD4(0x3b),
  TEXT80_MASK_WORD4(0x3c), TEXT80_MASK_WORD4(0x3d), TEXT80_MASK_WORD4(0x3e), TEXT80_MASK_WORD4(0x3f)};


/**
 * \brief one 40- or 80-column text row, six pixels a cell at one byte each. Two cells are twelve bytes, so they
 * go out as three words with the pair straddling the middle one. The nibble expansion is the tile
 * path's: bit 3 of a nibble lands in byte 0, so a cell's last two pixels are the low half of the
 * second lookup.
 *
 * Colour comes from `rowColors` stepped by `colorStride`, which is 0 when the whole row shares one
 * pair - no per-cell test for a per-row property. Layer 2 also accumulates its coverage, six bits a
 * cell at a position that never aligns.
 *
 * `pal` is the layer's sub-palette. At ECM0 it indexes `ecm0Palette`, whose entry 0 is what a cell
 * writes where it draws nothing - also the colour a zero colour byte gets, so the memo primes on it
 * rather than on the backdrop, the two differing under a non-priority bitmap layer. At ECM1-3 a cell
 * is an ordinary ECM tile six pixels wide: the attribute byte carries priority, both flips,
 * transparency and the sub-palette, and the fg/bg pair goes inert. The attribute is by name unless
 * position attributes are on, which `nameAttrMask` and `colorStride` select between without a test
 * per cell.
 *
 * A horizontal scroll enters as `hscroll`: the row still begins on a cell boundary, so the stores
 * stay aligned however they are grouped, and the fine offset is the composite's shift. That costs
 * the two extra cells the count then asks for, to fill the far edge the shift uncovers.
 */
PICO9918_INLINE_HOT void
renderTextRow(PICO9918_INST_ARG const uint8_t* __restrict rowNames, const TileRowAddr* __restrict addr,
              const uint8_t* __restrict rowColors, const uint32_t colorStride, uint32_t pal,
              uint8_t* __restrict dest, const uint32_t hscroll, const bool alwaysOnTop,
              const uint32_t numCols, const bool isTile2, const uint32_t ecm, const bool blend)
{
  const uint8_t* __restrict patternTable = addr->pattern;
  const bool wide                        = numCols == TEXT80_NUM_COLS;
  /* the picture starts at sprite pixel 8 in either depth, so at one byte a pixel an
     80-column line begins twice as far into the buffer as a 40-column one */
  const uint32_t padding     = wide ? TEXT80_PADDING_PX : TEXT_PADDING_PX;
  const uint32_t startCell   = textStartCell(hscroll, wide);
  const uint32_t pixelOffset = textPixelOffset(hscroll, wide);
  const uint32_t numCells    = numCols + (pixelOffset ? 2 : 0);

  uint32_t* pix32 = (uint32_t*)PICO9918_ASSUME_ALIGNED(dest, 4);
  /* ECM0 counts buffer pixels, for the coverage mask it writes cell by cell. An ECM row rolls its
     coverage instead and asks only where the sprite mask is, which is in screen pixels - so it
     takes the fine scroll off here and carries one counter rather than two values */
  uint32_t xPos = ecm ? (padding - pixelOffset) : padding;

  const uint8_t* __restrict names  = rowNames + startCell;
  const uint8_t* __restrict colors = rowColors + startCell * colorStride;
  const uint32_t colorWrap         = numCols * colorStride;
  uint32_t col                     = startCell;

  /* the attribute is indexed by name where position attributes are off, and by a walking pointer
     where they are on - one mask and one stride rather than a branch a cell. ECM0 keeps neither:
     its colour byte is `fixed` or the walking one, and index 0 is what it already read */
  const uint32_t nameAttrMask = (ecm && !colorStride) ? 0xff : 0;
  /* what a cell's attribute must show to be taken above the sprites, and nothing at all where the
     scanline has no sprite pixels to clear. Both are per-row, but left as two invariant branches in
     the body GCC emits a version of the whole loop for each combination */
  const uint32_t spritePriMask = tms9918->scanlineHasSprites ? 0x80 : 0;
  /* only layer 2 can be unconditionally above the sprites, so layer 1 carries neither the flag nor
     a register to hold it */
  const uint32_t spritePriForced = (isTile2 && alwaysOnTop) ? spritePriMask : 0;
  /* the row masks are uint32_t too, so a store through one forces this reload unless it is held */
  const uint32_t clear = transparentPixels[0];
  const int32_t flipY  = addr->flipY;
  uint32_t ecmOffset = 0, ecmColorMask = 0, ecmColorOffset = 0;
  const uint32_t* __restrict palette = ecm0Palette + pal;
  if (ecm)
  {
    ecmColorOffset = (ecm == 3) ? 2 : ecm;
    ecmColorMask   = (ecm == 3) ? 0x0e : 0x0f;
    ecmOffset      = 0x800 >> ((TMS_REGISTER(tms9918, PICO9918_REG_PAGE_SIZE) & 0x0c) >> 2);
    pal            = (ecm == 1) ? (pal & 0x20) : 0;
  }

  uint8_t lastColor = 0;
  uint32_t bgWord = palette[0], diffWord = 0;
  uint32_t lo = 0, hi = 0, m0 = 0, m1 = 0;

#define TEXT40_NEXT_CELL() \
  const uint32_t name = *names++; \
  const uint8_t color = colors[name & nameAttrMask]; \
  colors += colorStride;

#define TEXT40_CELL() \
  { \
    TEXT40_NEXT_CELL() \
    /* back to the row's own first cell, and no page swap: text has no page size bits. A start \
         cell past the last one never meets this test, so it reads on into the next row, which is \
         what hardware's counter does too */ \
    if (++col == numCols) \
    { \
      col = 0; \
      names -= numCols; \
      colors -= colorWrap; \
    } \
    const uint32_t patt = patternTable[name * PATTERN_BYTES]; \
    if (color != lastColor) \
    { \
      const uint8_t bgColor = color & 0xf; \
      const uint8_t fgColor = color >> 4; \
      bgWord                = palette[bgColor]; \
      diffWord              = bgWord ^ palette[fgColor]; \
      lastColor             = color; \
    } \
    lo = bgWord ^ (diffWord & maskExpandNibbleToWordRev[patt >> 4]); \
    hi = (uint16_t)(bgWord ^ (diffWord & maskExpandNibbleToWordRev[patt & 0x0f])); \
    if (isTile2) \
    { \
      /* where this layer draws at all: a zero colour draws nothing. Six bits a cell at a position \
           that never aligns, rolled into an accumulator and stored when a word fills rather than a \
           read-modify-write of one word and usually two - the shape the ECM cell below already has. \
           Every cell rolls, drawn or not, or the position stops tracking */ \
      const uint32_t bits  = patt >> 2; \
      const uint32_t cover = ((((color >> 4) ? bits : 0) | ((color & 0xf) ? ~bits : 0)) & 0x3f) << 26; \
      if (blend) \
      { \
        /* the cell writes where it covers instead of recording it, so there is no mask to leave \
             behind - and it must be left at zero, or a composite still running for the sprites would \
             merge an empty layer 2 over the line. Cover bit 31 is pixel 0, so its top nibble is the \
             first lookup's and the next two bits ride the top of the second's */ \
        m0 = maskExpandNibbleToWordRev[cover >> 28]; \
        m1 = maskExpandNibbleToWordRev[(cover >> 24) & 0x0c]; \
      } \
      else \
      { \
        coverAcc |= cover >> coverBit; \
        coverBit += 6; \
        if (coverBit >= 32) \
        { \
          *coverWord++ |= coverAcc; \
          coverBit -= 32; \
          coverAcc = cover << (6 - coverBit); \
        } \
      } \
    } \
  }

/* the same cell as the graphics tile path, six pixels wide: the pattern pointer already carries
   this row so a Y flip only steps to its mirror, an X flip mirrors six bits rather than eight, and
   the three planes fold into one accumulator holding both lookup indices. `pattMask` rides at bit
   31 for the coverage and priority masks the graphics path uses. */
#define TEXT40_ECM_CELL() \
  { \
    TEXT40_NEXT_CELL() \
    const uint8_t* pattData = patternTable + name * PATTERN_BYTES + ((color & 0x20) ? flipY : 0); \
    uint32_t pattMask       = (color & 0x10) ? 0 : 0xff; \
    uint32_t cover          = 0; \
    /* a byte, though the quad split would mask a wider one just as well: widening it to save the \
         X flip three truncations costs more in register allocation than the truncations do */ \
    uint8_t patt[3] = {0}; \
    switch (ecm) \
    { \
    case 3: patt[0] = pattData[ecmOffset * 2]; pattMask |= patt[0]; \
    case 2: patt[1] = pattData[ecmOffset]; pattMask |= patt[1]; \
    default: patt[2] = *pattData; pattMask |= patt[2]; \
    } \
    if (pattMask) \
    { \
      if (color & 0x40) \
      { \
        patt[0]  = reversedBits6[patt[0]]; \
        patt[1]  = reversedBits6[patt[1]]; \
        patt[2]  = reversedBits6[patt[2]]; \
        pattMask = reversedBits6[pattMask]; \
      } \
      /* the mask stays a byte: the blend indexes its two nibbles straight, and only the coverage \
           and priority words want it at bit 31 */ \
      cover = (pattMask & 0xfc) << 24; \
      if ((color | spritePriForced) & spritePriMask) \
        tmsClearRowBitsMask(xPos, cover, 6, rowMasks.rowSpriteBits); \
      uint32_t index = 0; \
      switch (ecm) \
      { \
      case 3: index = ecmSplitQuads(patt[0]) << 8; \
      case 2: index |= ecmSplitQuads(patt[1]) << 4; \
      default: index |= ecmSplitQuads(patt[2]); \
      } \
      const uint32_t cellPal = repeatedPalette(pal | ((color & ecmColorMask) << ecmColorOffset)); \
      lo                     = ecmLookup[index >> 16] | cellPal; \
      hi                     = ecmLookup[(uint16_t)index] | cellPal; \
      if (!isTile2 && (color & 0x10)) \
      { \
        /* where every plane is zero the pixel is not a pixel at all, so the backdrop shows \
             rather than the sub-palette's colour 0 */ \
        lo = clear ^ ((lo ^ clear) & maskExpandNibbleToWordRev[pattMask >> 4]); \
        hi = clear ^ ((hi ^ clear) & maskExpandNibbleToWordRev[pattMask & 0x0f]); \
      } \
    } \
    else \
    { \
      lo = hi = clear; \
    } \
    if (isTile2) \
    { \
      /* six bits a cell at a position that never aligns: rolled into an accumulator and stored \
           when a word fills, rather than a read-modify-write of one word and usually two. Every \
           cell rolls, drawn or not, or the position stops tracking. `cover << 6` is zero, so a \
           cell that ends exactly on a word boundary needs no test of its own */ \
      coverAcc |= cover >> coverBit; \
      coverBit += 6; \
      if (coverBit >= 32) \
      { \
        *coverWord++ |= coverAcc; \
        coverBit -= 32; \
        coverAcc = cover << (6 - coverBit); \
      } \
    } \
    xPos += 6; \
  }

  /* Six bytes is a word and a halfword, and at a six-byte stride both land aligned: the first cell
     of a pair at offsets 0 and 4, the second at 6 and 8. So each cell stores as it is computed and
     nothing is carried between them - stitching them into three words instead cost two spills and
     two reloads, which is more than the fourth store.
     ECM0 takes the pair as its unit, which is what leaves both alignments untested. An ECM cell is
     ten times the body, so emitting it twice to keep that trick costs far more code than it saves,
     and testing the alignment instead invites GCC to unroll the loop. Six bytes is also three
     halfwords at any even address, so it writes three: one more store than the pair geometry, and
     it needs neither the test nor the second body. */
  if (ecm)
  {
    /* the row reaches its own first cell at most once, so it is two runs rather than a test on
       every cell - and inside a run nothing carries the row's width or its wrap. One loop with a
       pointer compare instead is smaller and slower, which is what register pressure costs in this
       body. A start cell past the last one never wraps at all: it reads on into the next row, as
       hardware's counter does */
    /* six bits a cell at a position that never aligns: rolled and stored when a word fills,
       rather than a read-modify-write of one word and usually two. Declared per shape, not
       shared: hoisting it kept three values live across both and cost the ECM clone five
       dropped scanlines on RP2040, where the registers are tightest */
    uint32_t* coverWord = tms9918->layerSelectionMask + (padding >> 5);
    uint32_t coverAcc = 0, coverBit = padding & 0x1f;

    uint8_t* p         = dest;
    uint32_t remaining = numCells;
    uint32_t run       = (startCell < numCols) ? (numCols - startCell) : numCells;

    while (remaining)
    {
      if (run > remaining) run = remaining;
      remaining -= run;

      while (run--)
      {
        TEXT40_ECM_CELL();
        *(uint16_t*)(p)     = lo;
        *(uint16_t*)(p + 2) = lo >> 16;
        *(uint16_t*)(p + 4) = hi;
        p += 6;
      }

      names  = rowNames;
      colors = rowColors;
      run    = remaining;
    }

    if (isTile2) *coverWord |= coverAcc;
  }
  else if (blend)
  {
    /* Three halfwords, because layer 1's frame is only ever even-aligned relative to layer 2's:
       both text scrolls are even but their difference need not be a word. Six bytes is
       three halfwords at any even address, which measured better than a branch and two stores
       two stores. */
    /* dead here - the cell writes rather than records - but the macro's other arm names them */
    uint32_t* coverWord = tms9918->layerSelectionMask + (padding >> 5);
    uint32_t coverAcc = 0, coverBit = padding & 0x1f;

    uint16_t* p = (uint16_t*)dest;

    for (uint32_t tileX = 0; tileX < numCells; ++tileX)
    {
      TEXT40_CELL();
      uint32_t d;
      d    = p[0];
      p[0] = d ^ ((d ^ lo) & m0);
      d    = p[1];
      p[1] = d ^ ((d ^ (lo >> 16)) & (m0 >> 16));
      d    = p[2];
      p[2] = d ^ ((d ^ hi) & m1);
      p += 3;
    }
  }
  else
  {
    /* six bits a cell at a position that never aligns: rolled and stored when a word fills,
       rather than a read-modify-write of one word and usually two. Declared per shape, not
       shared: hoisting it kept three values live across both and cost the ECM clone five
       dropped scanlines on RP2040, where the registers are tightest */
    uint32_t* coverWord = tms9918->layerSelectionMask + (padding >> 5);
    uint32_t coverAcc = 0, coverBit = padding & 0x1f;

    for (uint32_t tileX = 0; tileX < numCells; tileX += 2)
    {
      uint16_t* pix16 = (uint16_t*)pix32;

      TEXT40_CELL();
      pix32[0] = lo;
      pix16[2] = hi;
      TEXT40_CELL();
      pix16[3] = lo;
      pix32[2] = (lo >> 16) | (hi << 16);
      pix32 += 3;
    }

    if (isTile2) *coverWord |= coverAcc;
  }

#undef TEXT40_ECM_CELL
#undef TEXT40_CELL
#undef TEXT40_NEXT_CELL
}

/* One body per (layer, ecm). 80 columns arrives here on the 8bpp tier, where a text row is the same
   byte-per-pixel shape and only the count differs. At 4bpp it cannot use a layer buffer at all, so
   RP2040 keeps the blend-in-place emitter below. */
#define TEXT_ROW_PARAMS \
  PICO9918_INST_ARG const uint8_t *rowNames, const TileRowAddr *addr, const uint8_t *rowColors, \
    const uint32_t colorStride, const uint32_t pal, uint8_t *dest, const uint32_t hscroll, \
    const bool alwaysOnTop
#define TEXT_ROW_CLONE(name, cols, t2, e) \
  static EMITTER_NOINLINE void __time_critical_func(name)(TEXT_ROW_PARAMS) \
  { \
    renderTextRow(PICO9918_INST rowNames, addr, rowColors, colorStride, pal, dest, hscroll, alwaysOnTop, \
                  cols, t2, e, false); \
  }

TEXT_ROW_CLONE(text40RowT1, TEXT_NUM_COLS, false, 0)
TEXT_ROW_CLONE(text40RowT1Ecm1, TEXT_NUM_COLS, false, 1)
TEXT_ROW_CLONE(text40RowT1Ecm2, TEXT_NUM_COLS, false, 2)
TEXT_ROW_CLONE(text40RowT1Ecm3, TEXT_NUM_COLS, false, 3)
TEXT_ROW_CLONE(text40RowT2, TEXT_NUM_COLS, true, 0)
TEXT_ROW_CLONE(text40RowT2Ecm1, TEXT_NUM_COLS, true, 1)
TEXT_ROW_CLONE(text40RowT2Ecm2, TEXT_NUM_COLS, true, 2)
TEXT_ROW_CLONE(text40RowT2Ecm3, TEXT_NUM_COLS, true, 3)

static void (*const textRowClones[2][4])(TEXT_ROW_PARAMS) = {
  {text40RowT1, text40RowT1Ecm1, text40RowT1Ecm2, text40RowT1Ecm3},
  {text40RowT2, text40RowT2Ecm1, text40RowT2Ecm2, text40RowT2Ecm3}};

#if PICO9918_TEXT80_8BPP
/* 80 columns are the same body at a different count: one byte a pixel, six pixels a cell, the same
   word-and-halfword stores at the same alignments, and a layer buffer the composite can arbitrate
   because a mask bit now covers one pixel rather than two */
TEXT_ROW_CLONE(text80RowT1, TEXT80_NUM_COLS, false, 0)
TEXT_ROW_CLONE(text80RowT1Ecm1, TEXT80_NUM_COLS, false, 1)
TEXT_ROW_CLONE(text80RowT1Ecm2, TEXT80_NUM_COLS, false, 2)
TEXT_ROW_CLONE(text80RowT1Ecm3, TEXT80_NUM_COLS, false, 3)
TEXT_ROW_CLONE(text80RowT2, TEXT80_NUM_COLS, true, 0)
TEXT_ROW_CLONE(text80RowT2Ecm1, TEXT80_NUM_COLS, true, 1)
TEXT_ROW_CLONE(text80RowT2Ecm2, TEXT80_NUM_COLS, true, 2)
TEXT_ROW_CLONE(text80RowT2Ecm3, TEXT80_NUM_COLS, true, 3)

/* Layer 2 folded into layer 1's line as it emits, instead of into a coverage mask for the
   composite to arbitrate. Only an ECM0 line with layer 1 present may do it - above ECM0 priority is
   attr(0) per tile - so it is a body of its own rather than what the layer always does. */
static EMITTER_NOINLINE void __time_critical_func(text80RowT2Blend)(TEXT_ROW_PARAMS)
{
  renderTextRow(PICO9918_INST rowNames, addr, rowColors, colorStride, pal, dest, hscroll, alwaysOnTop,
                TEXT80_NUM_COLS, true, 0, true);
}

static void (*const text80RowClones[2][4])(TEXT_ROW_PARAMS) = {
  {text80RowT1, text80RowT1Ecm1, text80RowT1Ecm2, text80RowT1Ecm3},
  {text80RowT2, text80RowT2Ecm1, text80RowT2Ecm2, text80RowT2Ecm3}};
#endif

/**
 * \brief one 80-column text row, six pixels a cell at half a byte each. Four cells are twelve bytes, so a
 * group goes out as three words.
 *
 * `scrolled` splits it in two. Unscrolled, the colour bytes arrive four at a time as one aligned
 * word and the row cannot wrap, so the colour memo and the four-cell transparent skip both live in
 * that loop. Scrolled, the row starts on an arbitrary cell and wraps to its own first one, so
 * neither holds: the colour table is not word-aligned to the group and the group can straddle the
 * wrap. That clone reads a colour byte a cell and does without the skip.
 *
 * The fine offset is only ever 0, 2 or 4 pixels, which at 4bpp is a whole number of
 * bytes - so the caller backs the start cell up by that many and moves `pixels` four bytes per
 * byte of offset, which keeps the three-word store aligned and puts the cells it skipped over in
 * the side border.
 */
PICO9918_INLINE_HOT void
renderText80Row(PICO9918_INST_ARG const uint8_t* __restrict rowNames,
                const uint8_t* __restrict patternTable, const uint8_t* __restrict rowColors,
                const bool opaq, uint8_t* __restrict pixels, const uint32_t startCol,
                const uint32_t numCells, const bool scrolled)
{
  const uint8_t bgc            = tmsMainBgColor(tms9918);
  const uint8_t* rowNamesTable = rowNames + startCol;
  const uint8_t* colors        = rowColors + startCol;
  const uint32_t* colorTable32 = (const uint32_t*)PICO9918_ASSUME_ALIGNED(rowColors, 4);
  uint32_t* pix32              = (uint32_t*)PICO9918_ASSUME_ALIGNED(pixels, 4);
  uint32_t col                 = startCol;

  /* the row wraps to its own first cell, with no page swap */
#define TEXT80_NEXT_CELL() \
  { \
    ++rowNamesTable; \
    if (scrolled && ++col == TEXT80_NUM_COLS) \
    { \
      col           = 0; \
      rowNamesTable = rowNames; \
      colors        = rowColors; \
    } \
    else if (scrolled) \
      ++colors; \
  }

  if (opaq)
  {
    uint8_t lastColor      = 0;
    uint32_t bgColorMask   = text80ColorWord[bgc];
    uint32_t diffColorMask = 0;

    for (uint8_t tileX = 0; tileX < numCells; tileX += 4)
    {
      uint32_t colorWord = scrolled ? 0 : *colorTable32++;
      uint32_t word, acc;

#define TEXT80_OPAQUE_CELL() \
  { \
    uint8_t color; \
    if (scrolled) \
      color = *colors; \
    else \
    { \
      color = (uint8_t)colorWord; \
      colorWord >>= 8; \
    } \
    if (color != lastColor) \
    { \
      const uint8_t bgColor = color & 0xf; \
      const uint8_t fgColor = color >> 4; \
      bgColorMask           = text80ColorWord[bgColor ? bgColor : bgc]; \
      diffColorMask         = bgColorMask ^ text80ColorWord[fgColor ? fgColor : bgc]; \
      lastColor             = color; \
    } \
    const uint32_t mask = text80MaskWord[patternTable[*rowNamesTable * PATTERN_BYTES]]; \
    TEXT80_NEXT_CELL(); \
    word = bgColorMask ^ (diffColorMask & mask); \
  }

      TEXT80_OPAQUE_CELL();
      acc = word;
      TEXT80_OPAQUE_CELL();
      *pix32++ = acc | (word << 24);
      acc      = word >> 8;
      TEXT80_OPAQUE_CELL();
      *pix32++ = acc | (word << 16);
      acc      = word >> 16;
      TEXT80_OPAQUE_CELL();
      *pix32++ = acc | (word << 8);

#undef TEXT80_OPAQUE_CELL
    }
  }
  else
  {
    for (uint8_t tileX = 0; tileX < numCells; tileX += 4)
    {
      uint32_t colorWord = scrolled ? 0 : *colorTable32++;
      uint32_t val, sel, accVal, accSel;

      /* all four cells fully transparent - every blend below would be the identity. A scrolled row
         has no such word to test: its four colours are neither adjacent in one load nor certain to
         be four cells of the same row */
      if (!scrolled && !colorWord)
      {
        rowNamesTable += 4;
        pix32 += 3;
        continue;
      }

#define TEXT80_OVERLAY_CELL() \
  { \
    const uint32_t mask = text80MaskWord[patternTable[*rowNamesTable * PATTERN_BYTES]]; \
    uint8_t colorByte; \
    if (scrolled) \
      colorByte = *colors; \
    else \
    { \
      colorByte = (uint8_t)colorWord; \
      colorWord >>= 8; \
    } \
    TEXT80_NEXT_CELL(); \
    const uint32_t fgWord = text80ColorWord[colorByte >> 4]; \
    const uint32_t bgWord = text80ColorWord[colorByte & 0xf]; \
    const uint32_t fgSel  = fgWord ? mask : 0; \
    const uint32_t bgSel  = bgWord ? (~mask & 0xffffffu) : 0; \
    sel                   = fgSel | bgSel; \
    val                   = (fgWord & fgSel) | (bgWord & bgSel); \
  }

#define TEXT80_OVERLAY_STORE(shift) \
  { \
    const uint32_t m   = accSel | (sel << (shift)); \
    const uint32_t v   = accVal | (val << (shift)); \
    const uint32_t old = *pix32; \
    *pix32++           = old ^ ((old ^ v) & m); \
  }

      TEXT80_OVERLAY_CELL();
      accVal = val;
      accSel = sel;
      TEXT80_OVERLAY_CELL();
      TEXT80_OVERLAY_STORE(24);
      accVal = val >> 8;
      accSel = sel >> 8;
      TEXT80_OVERLAY_CELL();
      TEXT80_OVERLAY_STORE(16);
      accVal = val >> 16;
      accSel = sel >> 16;
      TEXT80_OVERLAY_CELL();
      TEXT80_OVERLAY_STORE(8);

#undef TEXT80_OVERLAY_CELL
#undef TEXT80_OVERLAY_STORE
    }
  }
#undef TEXT80_NEXT_CELL
}

/* One body per (can this row scroll). The unscrolled one keeps its cell count as a constant. */
#define TEXT80_ROW_CLONE(name, cells, scroll) \
  static EMITTER_NOINLINE void __time_critical_func(name)( \
    PICO9918_INST_ARG const uint8_t* rowNames, const uint8_t* patternTable, const uint8_t* rowColors, \
    const bool opaq, uint8_t* pixels, const uint32_t startCol, const uint32_t numCells) \
  { \
    renderText80Row(PICO9918_INST rowNames, patternTable, rowColors, opaq, pixels, startCol, cells, \
                    scroll); \
  }

TEXT80_ROW_CLONE(text80Row, TEXT80_NUM_COLS, false)
TEXT80_ROW_CLONE(text80RowScrolled, numCells, true)

static inline void renderText80Layer(PICO9918_INST_ARG const uint8_t* rowNames,
                                     const uint8_t* patternTable, const uint8_t* rowColors,
                                     const bool opaq, uint8_t* pixels, const uint32_t startCol,
                                     const uint32_t numCells, const bool scrolled)
{
  if (scrolled)
    text80RowScrolled(PICO9918_INST rowNames, patternTable, rowColors, opaq, pixels, startCol, numCells);
  else
    text80Row(PICO9918_INST rowNames, patternTable, rowColors, opaq, pixels, 0, TEXT80_NUM_COLS);
}


/* one run of 80-column cells in the two colours R7 holds. Both are constant down the whole row, so
   this is the one text path that needs no colour memo: one lookup turns a pattern byte into all six
   pixels and the pair of colours is applied to it whole, the same expansion renderText80Row uses.
   Three byte stores a cell, so a run can start anywhere - which is what lets the row's wrap be a
   second call rather than a test on every cell. */
static inline uint8_t* text80TwoTone(const uint8_t* __restrict names, const uint8_t* __restrict patternTable,
                                     const uint32_t bgWord, const uint32_t diffWord,
                                     uint8_t* __restrict pixels, uint32_t cells)
{
  while (cells--)
  {
    const uint32_t pixelWord = bgWord ^ (diffWord & text80MaskWord[patternTable[*names++ * PATTERN_BYTES]]);

    *pixels++ = pixelWord;
    *pixels++ = pixelWord >> 8;
    *pixels++ = pixelWord >> 16;
  }
  return pixels;
}

/**
 * \brief generate a 40- or 80-column text mode scanline.
 *
 * Text has a path of its own, and hardware says so: it selects a different name address, a
 * different attribute address, a different colour source, a different flip, a different horizontal
 * scroll counter and a different expansion width - six pixels against eight. What it shares with
 * the graphics modes is the fetch schedule, which for us is the address generator above, the sprite
 * pass and the backdrop.
 *
 * The emitters write the finished line rather than a layer buffer, so the composite runs only where
 * something has to be arbitrated - a bitmap layer, or a priority second layer.
 */
static EMITTER_NOINLINE void __time_critical_func(text_scan_line)(PICO9918_INST_ARG uint16_t y,
                                                                            uint8_t pixels[TMS9918_PIXELS_X])
{
  const bool wide             = tmsCachedMode == TMS_MODE_TEXT80;
  const uint8_t numCols       = wide ? TEXT80_NUM_COLS : TEXT_NUM_COLS;
  const uint8_t nameTableMask = (wide && !PICO9918_UNLOCKED(tms9918)) ? 0x0c : 0x0f;

  const bool attrPerPos = PICO9918_UNLOCKED(tms9918) && (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) & 0x02);

  TileRowAddr addr;
  uint16_t rowNamesAddr, colorTableAddr;
  tileLayerAddr(PICO9918_INST y, &T1_CONFIG, numCols, nameTableMask, true, false, false, attrPerPos, &addr,
                &rowNamesAddr, &colorTableAddr);

  const uint8_t* patternTable     = addr.pattern;
  const pico9918_color_t bgColor = tmsMainBgColor(tms9918);
  uint32_t* border                = (uint32_t*)pixels;

  pixels += TEXT_PADDING_PX;

  const TextScroll t1 = textScroll80(PICO9918_INST & T1_CONFIG);

  PICO9918_FILL32_WAIT(PICO9918_FILL_LINE);

  if (attrPerPos)
  {
    const bool tilesDisabled = TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) & 0x10;
    if (!tilesDisabled)
      renderText80Layer(PICO9918_INST tms9918->vram.bytes + rowNamesAddr, patternTable,
                        tms9918->vram.bytes + colorTableAddr, true, pixels - 4 * t1.bytes, t1.startCol,
                        t1.cells, t1.scrolled);

    if (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED1) & 0x80)
    {
      const TextScroll t2 = textScroll80(PICO9918_INST & T2_CONFIG);
      tileLayerAddr(PICO9918_INST y, &T2_CONFIG, numCols, nameTableMask, true, false, false, attrPerPos, &addr,
                    &rowNamesAddr, &colorTableAddr);

      renderText80Layer(PICO9918_INST tms9918->vram.bytes + rowNamesAddr, addr.pattern,
                        tms9918->vram.bytes + colorTableAddr, false, pixels - 4 * t2.bytes, t2.startCol,
                        t2.cells, t2.scrolled);
    }
  }
  else // just plain old two-tone
  {
    const pico9918_color_t fgColor = tmsMainFgColor(tms9918);
    const uint8_t* rowNamesTable    = tms9918->vram.bytes + rowNamesAddr;

    if (wide)
    {
      const uint32_t bgWord   = text80ColorWord[bgColor];
      const uint32_t diffWord = bgWord ^ text80ColorWord[fgColor];

      /* three byte stores a cell, so this loop needs no alignment: it starts on the very cell the
         scroll names, and the wrap is where one run ends and the next begins rather than a test */
      const uint32_t cells = t1.bytes ? TEXT80_NUM_COLS + 1 : TEXT80_NUM_COLS;
      uint32_t run         = cells;
      if (t1.startCell < TEXT80_NUM_COLS && t1.startCell + run > TEXT80_NUM_COLS)
        run = TEXT80_NUM_COLS - t1.startCell;

      pixels =
        text80TwoTone(rowNamesTable + t1.startCell, patternTable, bgWord, diffWord, pixels - t1.bytes, run);
      if (run < cells) text80TwoTone(rowNamesTable, patternTable, bgWord, diffWord, pixels, cells - run);
    }
    else
    {
      /* Locked 40-column text: one colour pair for the whole screen, so the same expansion again -
         four pixels a lookup rather than a bit at a time. Six bytes is a word
         and a halfword and a pair of cells covers both alignments, exactly as renderTextRow does;
         the palette is the plain one, there being no transparent sub-palette entry when locked. */
      const uint32_t bgWord   = repeatedPalette(bgColor);
      const uint32_t diffWord = bgWord ^ repeatedPalette(fgColor);
      uint32_t* pix32         = (uint32_t*)PICO9918_ASSUME_ALIGNED(pixels, 4);

      for (uint8_t tileX = 0; tileX < TEXT_NUM_COLS; tileX += 2)
      {
        uint16_t* pix16 = (uint16_t*)pix32;
        uint32_t patt   = patternTable[*rowNamesTable++ * PATTERN_BYTES];

        pix32[0] = bgWord ^ (diffWord & maskExpandNibbleToWordRev[patt >> 4]);
        pix16[2] = bgWord ^ (diffWord & maskExpandNibbleToWordRev[patt & 0x0f]);

        patt              = patternTable[*rowNamesTable++ * PATTERN_BYTES];
        const uint32_t lo = bgWord ^ (diffWord & maskExpandNibbleToWordRev[patt >> 4]);
        const uint32_t hi = bgWord ^ (diffWord & maskExpandNibbleToWordRev[patt & 0x0f]);

        pix16[3] = lo;
        pix32[2] = (lo >> 16) | (hi << 16);
        pix32 += 3;
      }
    }
  }

  /* a scrolled row is emitted whole cells at a time, so it runs into both side borders. Restoring
     them costs four stores and is the whole price of not having to end the row on a cell */
  border[0] = border[1] = bg;
  border[62] = border[63] = bg;
}

/** \brief Write full tile to aligned buffer - 8 pixels at once */
static inline void writeToAlignedBuffer(uint8_t* buffer, uint32_t xPos, const uint32_t left,
                                        const uint32_t right)
{
  uint32_t* buffer_words = (uint32_t*)(buffer + xPos);
  buffer_words[0]        = left;
  buffer_words[1]        = right;
}

/**
 * \brief render an ECM0 (enhanced color mode) graphics I tile. basically the same as original, but can scroll
 *
 * INLINE: so will be different versions generated, depending on hard-coded (or known at compile-time) arguments
 */
static inline void renderEcm0Tile(PICO9918_INST_ARG uint8_t* buffer, const uint32_t xPos,
                                  const uint8_t pattIdx, const uint8_t patternTable[],
                                  const uint32_t colorTableAddr, const uint32_t pal, const bool isTile2,
                                  const bool gm2Color, const bool mcm)
{
  /* is the pixel mask already full here? then nothing of this tile can show */
  if (!isTile2 && !tmsTestRowBitsMaskAligned(xPos, 0xffu << 24, tms9918->finalMask))
  {
    writeToAlignedBuffer(buffer, xPos, transparentPixels[0], transparentPixels[1]);
    return;
  }

  /* grab the attributes for this tile. Graphics II colours a tile row rather than a group of
     eight names, and its colour table pointer already carries the row. Multicolor has no colour
     table at all: the pattern byte is the colour pair, over a pattern hardware fakes as two solid
     nibbles - which the mask expansion below folds away to nothing. */
  const uint32_t pattByte = patternTable[pattIdx * PATTERN_BYTES];
  const uint32_t colorByte =
    mcm ? pattByte
        : tms9918->vram.bytes[colorTableAddr + (gm2Color ? pattIdx * PATTERN_BYTES : (pattIdx >> 3))];
  const uint32_t patt = mcm ? 0xf0 : pattByte;

  const uint32_t bgColor = colorByte & 0x0f;
  const uint32_t fgColor = colorByte >> 4;

  /* colour 0 of each sub-palette is the transparent entry here, so a zero colour costs no test of
     its own - it neither claims the pixel nor paints palette entry 0 */
  const uint32_t bgPalette = ecm0Palette[pal | bgColor];
  const uint32_t fgPalette = ecm0Palette[pal | fgColor];

  uint32_t pattMask = 0xff;
  if (!bgColor) pattMask &= patt;
  if (!fgColor) pattMask ^= patt;

  pattMask <<= 24;
  if (isTile2) tmsUpdateRowBitsMaskAligned(xPos, pattMask, tms9918->layerSelectionMask);

  if (!pattMask)
  {
    if (!isTile2)
    {
      writeToAlignedBuffer(buffer, xPos, transparentPixels[0], transparentPixels[1]);
    }
    return;
  }

  const uint32_t rightMask = maskExpandNibbleToWordRev[patt & 0xf];
  const uint32_t leftMask  = maskExpandNibbleToWordRev[patt >> 4];

  writeToAlignedBuffer(buffer, xPos, (fgPalette & leftMask) | (bgPalette & ~leftMask),
                       (fgPalette & rightMask) | (bgPalette & ~rightMask));
}


/** \brief render one ECM tile into the layer buffer */
static inline void
renderEcmTileToAlignedBuffer(PICO9918_INST_ARG uint8_t* buffer, const uint32_t xPos, const uint32_t pixelOffset,
                             const uint8_t pattIdx, const uint8_t patternTable[],
                             const uint32_t colorTableAddr, const uint32_t ecm, const uint32_t ecmOffset,
                             const uint32_t ecmColorMask, const uint32_t ecmColorOffset, const uint32_t pal,
                             const bool attrPerPos, const int32_t flipY, const uint32_t tileIndex,
                             uint32_t* lastEmpty, const bool isTile2, const bool alwaysOnTop)
{
  /* Empty pattern, or a pixel layer 2 has already covered: either way there is nothing to
     draw. The coverage test is layer 1's alone - layer 2 runs first, so the mask it would
     read is still the scanline's zeroed one. */
  if ((*lastEmpty == pattIdx) ||
      (!isTile2 && !tmsTestRowBitsMaskAligned(xPos, 0xffu << 24, tms9918->finalMask)))
  {
    // T1 must still write: transparentPixels is the backdrop, or 0 under a bitmap layer
    if (!isTile2)
    {
      writeToAlignedBuffer(buffer, xPos, transparentPixels[0], transparentPixels[1]);
    }
    return;
  }

  /* grab the attributes for this tile */
  uint32_t colorTableOffset = attrPerPos ? tileIndex : pattIdx;
  uint32_t pattOffset       = pattIdx * PATTERN_BYTES;

  const uint32_t colorByte = tms9918->vram.bytes[colorTableAddr + colorTableOffset];

  const uint8_t* pattData = patternTable + pattOffset;

  /* the pattern pointer already carries this row, so a Y flip only has to step to its mirror */
  if (colorByte & 0x20) pattData += flipY;

  uint32_t pattMask = (colorByte & 0x10) ? 0 : 0xff; // handle transparency flag
  uint32_t index    = 0;

  /* One byte per bitplane, and their union: a set bit in the mask is a pixel with a
     colour, and a clear one is a pixel the layer below shows through. */
  uint8_t patt[3] = {0}; // indexes into this are reversed. ecm3 is in index 0

  switch (ecm)
  {
  case 3: patt[0] = pattData[ecmOffset * 2]; pattMask |= patt[0];
  case 2: patt[1] = pattData[ecmOffset]; pattMask |= patt[1];
  default: patt[2] = *pattData; pattMask |= patt[2];
  }

  /* have we any pixels to draw? */
  if (pattMask)
  {
    if (colorByte & 0x40) // flipX
    {
      patt[0]  = reversedBits[patt[0]];
      patt[1]  = reversedBits[patt[1]];
      patt[2]  = reversedBits[patt[2]];
      pattMask = reversedBits[pattMask];
    }

    const uint32_t priority = alwaysOnTop || (colorByte & 0x80);
    pattMask <<= 24;

    if (isTile2) tmsUpdateRowBitsMaskAligned(xPos, pattMask, tms9918->layerSelectionMask);
    if (priority && tms9918->scanlineHasSprites)
    {
      /* the sprite mask is in screen pixels and the tile in buffer ones, and a finely scrolled row
         begins its first tile left of the screen: that one clears from zero, its off-screen bits
         shifted out. The position is unsigned, so subtracting past it wrapped rather than clipped */
      const uint32_t offScreen = xPos ? 0 : pixelOffset;
      tmsClearRowBitsMask(xPos - pixelOffset + offScreen, pattMask << offScreen, 8, rowMasks.rowSpriteBits);
    }

    /* the cell's two indices are the same six nibbles differently grouped, so one accumulator
       carries both - the left quad above bit 16, the right below - and each plane is placed by a
       single shift. Two indices and their four mask constants were four live values in a body that
       had none to spare */
    switch (ecm)
    {
    case 3: index = ecmSplitQuads(patt[0]) << 8;
    case 2: index |= ecmSplitQuads(patt[1]) << 4;
    default: index |= ecmSplitQuads(patt[2]);
    }

    const uint32_t palette = repeatedPalette(pal | ((colorByte & ecmColorMask) << ecmColorOffset));
    const uint32_t left    = ecmLookup[index >> 16] | palette;
    const uint32_t right   = ecmLookup[(uint16_t)index] | palette;

    if (!isTile2 && (colorByte & 0x10))
    {
      /* where every plane is zero the pixel is not a pixel at all, so
         the backdrop shows rather than the sub-palette's colour 0 - which is what ECM0 gets from
         `ecm0Palette`, over the same expansion the pattern already drives. One test a tile: an
         opaque tile has no such pixel, and layer 2's coverage mask above excludes them */
      const uint32_t clear = transparentPixels[0];
      writeToAlignedBuffer(buffer, xPos, clear ^ ((left ^ clear) & maskExpandNibbleToWordRev[pattMask >> 28]),
                           clear ^ ((right ^ clear) & maskExpandNibbleToWordRev[(pattMask >> 24) & 0x0f]));
    }
    else
    {
      // Write to aligned buffer instead of doing expensive bit shifting
      writeToAlignedBuffer(buffer, xPos, left, right);
    }
  }
  else
  {
    // T1 must still write even when the tile is empty, for the same reason
    if (!isTile2)
    {
      writeToAlignedBuffer(buffer, xPos, transparentPixels[0], transparentPixels[1]);
    }
    *lastEmpty = pattIdx;
  }
}

/* A tile's colour pair as the two words the nibble expansion wants: the background repeated, and
   what to flip in it where a pattern bit is set. Four pixels come out of one lookup and one xor,
   and it is the same expansion the F18A tile and text paths use rather than a second way of drawing
   the same thing. */
static inline void lockedFgBg(PICO9918_INST_ARG uint32_t fgbg[2], const uint8_t pal, const uint32_t colorByte)
{
  fgbg[0] = repeatedPalette(pal | tmsBgColor(tms9918, colorByte));
  fgbg[1] = fgbg[0] ^ repeatedPalette(pal | tmsFgColor(tms9918, colorByte));
}

/** \brief generate a locked (plain TMS9918) tile row, straight into the scanline */
PICO9918_INLINE_HOT void
renderTileRowLocked(PICO9918_INST_ARG uint16_t rowNamesAddr, uint16_t colorTableAddr, uint8_t tileIndex,
                    uint8_t pal, uint8_t pixels[TMS9918_PIXELS_X], const TileRowAddr* addr, const bool gm2,
                    const bool mcm)
{
  const uint8_t* pattTableRow = addr->pattern;
  const uint8_t nameMask      = addr->nameMask;

  /* locked mode has no horizontal scroll, so 32 tiles cover the screen exactly */
  uint32_t numTiles = GRAPHICS_NUM_COLS;

  uint32_t* pix32     = (uint32_t*)PICO9918_ASSUME_ALIGNED(pixels, 4);
  uint8_t* pattPtr    = tms9918->vram.bytes + rowNamesAddr + tileIndex;
  uint32_t pattOffset = 0;

  /* the memo starts out holding tile 0, so tile 0's colours have to be in it too - priming only
     the pattern half drew palette entry 0 for any row that began with tile 0 */
  uint8_t lastPattIdx    = 0;
  uint32_t pattByte      = mcm ? 0xf0 : (uint8_t)pattTableRow[0];
  uint32_t lastColorByte = mcm ? (uint8_t)pattTableRow[0] : tms9918->vram.bytes[colorTableAddr];
  uint32_t fgbg[2];
  lockedFgBg(PICO9918_INST fgbg, pal, lastColorByte);

  while (numTiles--)
  {
    uint8_t pattIdx = *pattPtr++;
    if (gm2) pattIdx &= nameMask;

    if (lastPattIdx != pattIdx)
    {
      lastPattIdx = pattIdx;
      pattOffset  = lastPattIdx * PATTERN_BYTES;
      /* Multicolor has no colour table: the pattern byte is the colour pair, over a pattern
         hardware fakes as two solid nibbles */
      const uint32_t colorByte =
        mcm ? pattTableRow[pattOffset]
            : tms9918->vram.bytes[colorTableAddr + (gm2 ? pattOffset : (pattIdx >> 3))];
      if (!mcm) pattByte = (uint8_t)pattTableRow[pattOffset];
      if (lastColorByte != colorByte)
      {
        lastColorByte = colorByte;
        lockedFgBg(PICO9918_INST fgbg, pal, colorByte);
      }
    }

    pix32[0] = fgbg[0] ^ (fgbg[1] & maskExpandNibbleToWordRev[pattByte >> 4]);
    pix32[1] = fgbg[0] ^ (fgbg[1] & maskExpandNibbleToWordRev[pattByte & 0x0f]);
    pix32 += 2;
  }
}

#define LOCKED_ROW_CLONE(name, g, m) \
  static void __time_critical_func(name)(PICO9918_INST_ARG uint16_t rowNamesAddr, uint16_t colorTableAddr, \
                                         uint8_t tileIndex, uint8_t pal, uint8_t pixels[TMS9918_PIXELS_X], \
                                         const TileRowAddr* addr) \
  { \
    renderTileRowLocked(PICO9918_INST rowNamesAddr, colorTableAddr, tileIndex, pal, pixels, addr, g, m); \
  }

LOCKED_ROW_CLONE(rowLockedGm1, false, false)
LOCKED_ROW_CLONE(rowLockedGm2, true, false)
LOCKED_ROW_CLONE(rowLockedMcm, false, true)

/* one F18A tile row into the layer buffer.
 *
 * isTile2, ecm and gm2 arrive as literals from the wrappers below, so both per-tile switch chains
 * and every layer test inside the tile path fold away at compile time, and each wrapper gets
 * its own body. The attribute is belt and braces: Priv.h already redefines inline as
 * __force_inline for Pico builds.
 *
 * Everything mode-specific reaches this loop through `addr`, which the caller fills once per layer
 * per scanline. Only two things do not fold into it and so ride the clone instead: the name mask,
 * which Graphics II applies and Graphics I does not, and Graphics II's colour address, which
 * indexes by tile row where Graphics I indexes by a group of eight names.
 */
#define TILE_ROW_PARAMS \
  PICO9918_INST_ARG const bool hpSize, uint16_t rowNamesAddr, uint16_t colorTableAddr, uint8_t tileIndex, \
    uint8_t startPattBit, const bool attrPerPos, uint8_t pal, const bool alwaysOnTop, \
    const TileRowAddr *addr
#define TILE_ROW_ARGS \
  PICO9918_INST hpSize, rowNamesAddr, colorTableAddr, tileIndex, startPattBit, attrPerPos, pal, alwaysOnTop, \
    addr

PICO9918_INLINE_HOT void renderTileRow(TILE_ROW_PARAMS, const bool isTile2,
                                       const uint32_t ecm, const bool gm2, const bool mcm)
{
  uint32_t xPos      = 0;
  uint32_t lastEmpty = -1;

  const int32_t flipY         = addr->flipY;
  const uint8_t* patternTable = addr->pattern;
  const uint8_t nameMask      = addr->nameMask;
  uint8_t* targetBuffer       = isTile2 ? tms9918->tileLayer2Buffer : tms9918->tileLayer1Buffer;

  /* 32 tiles cover the screen exactly. A fine scroll makes the row start part-way into the first
     one and the buffer is read back that far along, so it needs one more to fill the far edge -
     which is the same count the text emitters take from `scrollOffset` */
  uint32_t numTiles = GRAPHICS_NUM_COLS + (startPattBit != 0);

  uint32_t ecmColorOffset = 0, ecmColorMask = 0, ecmOffset = 0;
  if (ecm)
  {
    ecmColorOffset = (ecm == 3) ? 2 : ecm;
    ecmColorMask   = (ecm == 3) ? 0x0e : 0x0f;
    ecmOffset      = 0x800 >> ((TMS_REGISTER(tms9918, PICO9918_REG_PAGE_SIZE) & 0x0c) >> 2);
    pal            = (ecm == 1) ? (pal & 0x20) : 0;
  }

  /* the row crosses into the other name page at most once, so it is two runs rather than a test on
     every tile - and inside a run the name table is a walking pointer instead of a base plus an
     index. The same shape the text emitters take for their wrap */
  while (numTiles)
  {
    uint32_t run = GRAPHICS_NUM_COLS - tileIndex;
    if (run > numTiles) run = numTiles;
    numTiles -= run;

    const uint8_t* names = tms9918->vram.bytes + rowNamesAddr + tileIndex;

    while (run--)
    {
      uint8_t pattIdx = *names++;
      if (gm2 || ecm) pattIdx &= nameMask;
      if (ecm)
      {
        renderEcmTileToAlignedBuffer(PICO9918_INST targetBuffer, xPos, startPattBit, pattIdx, patternTable,
                                     colorTableAddr, ecm, ecmOffset, ecmColorMask, ecmColorOffset, pal,
                                     attrPerPos, flipY, tileIndex, &lastEmpty, isTile2, alwaysOnTop);
      }
      else
      {
        renderEcm0Tile(PICO9918_INST targetBuffer, xPos, pattIdx, patternTable, colorTableAddr, pal,
                       isTile2, gm2, mcm);
      }
      ++tileIndex;
      xPos += 8;
    }

    if (hpSize)
    {
      /* the name table just toggles its page bit, but the attribute address gained that bit by
         addition, so it has to lose it the same way or a carry never comes back */
      rowNamesAddr ^= 0x400;
      if (attrPerPos) colorTableAddr = (colorTableAddr + ((rowNamesAddr & 0x400) << 1) - 0x400) & VRAM_MASK;
    }
    tileIndex = 0;
  }
}

#define TILE_ROW_CLONE(name, t2, e, g, m) \
  static void __time_critical_func(name)(TILE_ROW_PARAMS) \
  { \
    renderTileRow(TILE_ROW_ARGS, t2, e, g, m); \
  }

TILE_ROW_CLONE(rowT1Ecm0, false, 0, false, false)
TILE_ROW_CLONE(rowT1Ecm1, false, 1, false, false)
TILE_ROW_CLONE(rowT1Ecm2, false, 2, false, false)
TILE_ROW_CLONE(rowT1Ecm3, false, 3, false, false)
TILE_ROW_CLONE(rowT1Gm2, false, 0, true, false)
TILE_ROW_CLONE(rowT1Mcm, false, 0, false, true)
TILE_ROW_CLONE(rowT2Ecm0, true, 0, false, false)
TILE_ROW_CLONE(rowT2Ecm1, true, 1, false, false)
TILE_ROW_CLONE(rowT2Ecm2, true, 2, false, false)
TILE_ROW_CLONE(rowT2Ecm3, true, 3, false, false)
TILE_ROW_CLONE(rowT2Gm2, true, 0, true, false)
TILE_ROW_CLONE(rowT2Mcm, true, 0, false, true)

/* [layer][ecm], with slot 4 the Graphics II ECM0 body: ECM composes with mode through the plane 1
   address alone, so ECM1-3 needs no mode of its own */
#define TILE_ROW_GM2 4
#define TILE_ROW_MCM 5
static void (*const tileRowClones[2][6])(TILE_ROW_PARAMS) = {
  {rowT1Ecm0, rowT1Ecm1, rowT1Ecm2, rowT1Ecm3, rowT1Gm2, rowT1Mcm},
  {rowT2Ecm0, rowT2Ecm1, rowT2Ecm2, rowT2Ecm3, rowT2Gm2, rowT2Mcm}};

/** \brief generate a tile mode scanline for either T1 or T2 layer */
static void __time_critical_func(f18a_tile_layer_scan_line)(PICO9918_INST_ARG uint16_t y,
                                                             const TileLayerConfig* config, const bool blend)
{
  const uint32_t ecm     = (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED1) & 0x30) >> 4;
  const bool gm2         = tmsCachedMode == TMS_MODE_GRAPHICS_II;
  const bool mcm         = tmsCachedMode == TMS_MODE_MULTICOLOR;
  const bool wide        = TEXT80_WIDE_ROW;
  const bool text        = wide || tmsCachedMode == TMS_MODE_TEXT;
  const uint8_t textCols = wide ? TEXT80_NUM_COLS : TEXT_NUM_COLS;

  /* text takes its colour per cell at ECM0 too; a graphics mode there does not (D6) */
  const bool attrPerPos = (text || ecm) && (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) & 0x02);

  TileRowAddr addr;
  uint16_t rowNamesAddr, colorTableAddr;
  tileLayerAddr(PICO9918_INST y, config, text ? textCols : GRAPHICS_NUM_COLS, 0x0f, text, gm2, mcm, attrPerPos,
                &addr, &rowNamesAddr, &colorTableAddr);

  const uint8_t pal = (TMS_REGISTER(tms9918, PICO9918_REG_PALETTE_SELECT) & config->paletteMask) << config->paletteShift;

  const bool alwaysOnTop =
    config->priorityReg ? !(TMS_REGISTER(tms9918, config->priorityReg) & config->priorityMask) : false;

  if (text)
  {
    /* six-pixel cells, so a row of its own - but the same layer buffer, the same coverage mask and
       the same composite. Only the emitter differs, which is all that ever differed */
    const uint8_t fixed = (tmsMainFgColor(tms9918) << 4) | tmsMainBgColor(tms9918);
    /* the attribute table is read even where positions do not index it: ECM1-3 takes it by name
       and only ECM0 has an fg/bg pair to fall back on */
    const uint8_t* colors = (attrPerPos || ecm) ? tms9918->vram.bytes + colorTableAddr : &fixed;
    uint8_t* dest         = (config->isTile2 ? tms9918->tileLayer2Buffer : tms9918->tileLayer1Buffer) +
                    (wide ? TEXT80_PADDING_PX : TEXT_PADDING_PX);

#if PICO9918_TEXT80_8BPP
    if (blend)
    {
      /* into layer 1's line, at the offset layer 2's own scroll puts it there */
      dest = tms9918->tileLayer1Buffer + TEXT80_PADDING_PX +
             textPixelOffset(TMS_REGISTER(tms9918, PICO9918_REG_T1_HSCROLL), true) -
             textPixelOffset(TMS_REGISTER(tms9918, PICO9918_REG_T2_HSCROLL), true);
      text80RowT2Blend(PICO9918_INST tms9918->vram.bytes + rowNamesAddr, &addr, colors, attrPerPos, pal,
                       dest, TMS_REGISTER(tms9918, config->startPattReg), alwaysOnTop);
      return;
    }
#endif

#if PICO9918_TEXT80_8BPP
    if (wide)
    {
      text80RowClones[config->isTile2][ecm](PICO9918_INST tms9918->vram.bytes + rowNamesAddr, &addr,
                                            colors, attrPerPos, pal, dest,
                                            TMS_REGISTER(tms9918, config->startPattReg), alwaysOnTop);
      return;
    }
#endif
    textRowClones[config->isTile2][ecm](PICO9918_INST tms9918->vram.bytes + rowNamesAddr, &addr, colors,
                                        attrPerPos, pal, dest,
                                        TMS_REGISTER(tms9918, config->startPattReg), alwaysOnTop);
    return;
  }

  const uint8_t startPattBit = TMS_REGISTER(tms9918, config->startPattReg) & 0x07;
  const uint8_t tileIndex    = (TMS_REGISTER(tms9918, config->startPattReg) >> 3);
  const bool hpSize          = TMS_REGISTER(tms9918, PICO9918_REG_PAGE_SIZE) & config->hpSizeMask;

  uint32_t slot = ecm;
  if (!ecm) slot = gm2 ? TILE_ROW_GM2 : (mcm ? TILE_ROW_MCM : 0);

  /* two indirect calls per scanline buys a body per (isTile2, ecm, mode) with no per-tile dispatch */
  tileRowClones[config->isTile2][slot](PICO9918_INST hpSize, rowNamesAddr, colorTableAddr, tileIndex,
                                       startPattBit, attrPerPos, pal, alwaysOnTop, &addr);
}

/** \brief generate a Graphics I mode scanline for the T1 layer */
static void __time_critical_func(f18a_tile1_scan_line)(PICO9918_INST_ARG uint16_t y)
{
  f18a_tile_layer_scan_line(PICO9918_INST y, &T1_CONFIG, false);
}

/** \brief generate a Graphics I mode scanline for the T2 layer */
static void __time_critical_func(f18a_tile2_scan_line)(PICO9918_INST_ARG uint16_t y, const bool blend)
{
  f18a_tile_layer_scan_line(PICO9918_INST y, &T2_CONFIG, blend);
}

static bool underLayer = false;

/* Which buffer holds the finished line. Normally the one the caller passed, which the composite
   merges into. Where there is nothing to arbitrate it is tile layer 1's own buffer, and the merged
   line is then neither written nor read back - so the caller must ask rather than assume, which
   `pico9918_line_source` is for. */
static const uint8_t* lineSource = 0;
/**
 * \brief generate an F18A bitmap layer scanline
 *
 * INLINE: so will be different versions generated, depending on hard-coded (or known at compile-time) arguments
 */
static inline bool __time_critical_func(renderBitmapLayer)(PICO9918_INST_ARG uint16_t y, bool opaque,
                                                           const uint8_t width, const uint16_t addr,
                                                           const uint8_t bmlCtl,
                                                           uint8_t pixels[TMS9918_PIXELS_X])
{
  bool writeMask = bmlCtl & 0x40;
  underLayer     = !writeMask;

  bool returnVal = true;

  if (writeMask && opaque && (width == 64))
  {
    for (int i = 0; i < TMS9918_PIXELS_X / 32; ++i) rowMasks.rowBits[i] = -1;
    writeMask = false;
    returnVal = false;
  }

  uint32_t currentMask = 0;
  /* a byte, so a layer running past the right edge comes back at column zero of the same
     line rather than being cropped - which is what makes R33 a horizontal scroll */
  uint8_t xPos = TMS_REGISTER(tms9918, PICO9918_REG_BML_X);

  if (bmlCtl & 0x10) // fat 4bpp pixels?
  {
    const uint8_t colorMask   = 0xf0;
    const uint8_t colorOffset = 4;
    const uint8_t colorCount  = 2;
    const uint8_t colorSize   = 4;
    uint32_t maskPixelMask    = 0x3u << 30;
    uint32_t maskX            = xPos;

    uint8_t pal = (bmlCtl & 0xc) << 2;

    for (int xOff = 0; xOff < width; ++xOff)
    {
      uint8_t data = tms9918->vram.bytes[addr + xOff];
      for (int sp = 0; sp < colorCount; ++sp)
      {
        uint8_t color = (data & colorMask);
        if (opaque || color)
        {
          uint8_t finalColour = pal | (color >> colorOffset);
          pixels[xPos]        = finalColour;
          pixels[xPos + 1]    = finalColour;
          currentMask |= maskPixelMask;
        }
        xPos += 2;
        data <<= colorSize;
        maskPixelMask >>= 2;
      }
      if (writeMask && !maskPixelMask && currentMask)
      {
        tmsTestRowBitsMask(maskX, currentMask, 32, true, false, false);
        maskX         = xPos;
        maskPixelMask = 0x3u << 30;
        currentMask   = 0;
      }
    }
    if (writeMask && currentMask)
    {
      tmsTestRowBitsMask(maskX, currentMask, xPos - maskX, true, false, false);
    }
  }
  else // regular 2bpp pixels
  {
    const uint8_t colorMask   = 0xc0;
    const uint8_t colorOffset = 6;
    const uint8_t colorCount  = 4;
    const uint8_t colorSize   = 2;
    uint32_t maskPixelMask    = 0x1u << 31;
    uint32_t maskX            = xPos;

    uint8_t pal = (bmlCtl & 0xf) << 2;

    for (int xOff = 0; xOff < width; ++xOff)
    {
      uint8_t data = tms9918->vram.bytes[addr + xOff];
      for (int sp = 0; sp < colorCount; ++sp)
      {
        uint8_t color = (data & colorMask);
        if (opaque || color)
        {
          pixels[xPos] = pal | (color >> colorOffset);
          currentMask |= maskPixelMask;
        }
        ++xPos;
        data <<= colorSize;
        maskPixelMask >>= 1;
      }

      if (writeMask && !maskPixelMask && currentMask)
      {
        tmsTestRowBitsMask(maskX, currentMask, 32, true, false, false);
        maskX         = xPos;
        maskPixelMask = 0x1u << 31;
        currentMask   = 0;
      }
    }
    if (writeMask && currentMask)
    {
      tmsTestRowBitsMask(maskX, currentMask, xPos - maskX, true, false, false);
    }
  }
  return returnVal;
}


/** \brief generate an F18A bitmap layer scanline */
static bool __time_critical_func(bitmap_layer_scan_line)(PICO9918_INST_ARG uint16_t y,
                                                                  uint8_t pixels[TMS9918_PIXELS_X])
{
  /* bml enabled? */
  const uint8_t bmlCtl = TMS_REGISTER(tms9918, PICO9918_REG_BML_CONTROL);
  if (!(bmlCtl & 0x80)) return true;

  /* bml on this scanline? */
  const uint8_t top = TMS_REGISTER(tms9918, PICO9918_REG_BML_TOP_ROW);
  if (top > y) return true;

  y -= top;
  if (y >= TMS_REGISTER(tms9918, PICO9918_REG_BML_HEIGHT)) return true;

  /* row stride in bytes, four pixels each, rounded up so every row starts on a byte */
  const uint8_t bmlWidth = TMS_REGISTER(tms9918, PICO9918_REG_BML_WIDTH);
  const uint8_t width    = bmlWidth ? ((bmlWidth + 3) >> 2) : 64;
  const uint16_t addr = (TMS_REGISTER(tms9918, PICO9918_REG_BML_BASE) << 6) + (y * width);

  return renderBitmapLayer(PICO9918_INST y, !(bmlCtl & 0x20), width, addr, bmlCtl, pixels);
}

/* One 32-pixel chunk of the composite, four pixels at a time. Selecting a layer per pixel is a byte
 * mask over two words, and the nibble-to-word lookup the tile emitters use is exactly that mask - so
 * a chunk is eight selects rather than thirty-two.
 *
 * `hasSprites` arrives as a literal, so a chunk no sprite touches compiles to a plain store and one
 * that a sprite crosses to a merge, with no test in either. The caller can only use this where both
 * layer pointers are word-aligned, which a scroll that is not a multiple of four denies.
 */
static inline void compositeChunkWide(uint32_t* __restrict pix32, const uint32_t* __restrict l1,
                                      const uint32_t* __restrict l2, uint32_t sel, uint32_t open,
                                      const bool hasSprites)
{
  for (int i = 0; i < 8; ++i)
  {
    const uint32_t layers = maskExpandNibbleToWordRev[sel >> 28];
    uint32_t merged       = l1[i] ^ ((l1[i] ^ l2[i]) & layers);

    if (hasSprites)
    {
      const uint32_t old = pix32[i];
      merged             = old ^ ((old ^ merged) & maskExpandNibbleToWordRev[open >> 28]);
      open <<= 4;
    }

    pix32[i] = merged;
    sel <<= 4;
  }
}

/* the same chunk with a bitmap layer under the tiles, which is the one case that has to stay per
 * pixel: zero means transparent here, so every byte needs its own test and there is no word to
 * select whole. `hasSprites` is the same literal the other two chunks take, and it is worth more
 * here than anywhere - without it every pixel of every chunk tests and shifts a mask that is zero.
 */
static inline void compositeChunkUnder(uint8_t* __restrict pixels, const uint8_t* __restrict layer1,
                                       const uint8_t* __restrict layer2, uint32_t mask, uint32_t spriteMask,
                                       const bool hasSprites)
{
  for (int i = 0; i < 8; ++i)
  {
#define UNDER_PIXEL(n) \
  if (!hasSprites || !(spriteMask & MASK_NEXT_PIXEL)) \
  { \
    const uint8_t pixel = (mask & MASK_NEXT_PIXEL) ? layer2[n] : layer1[n]; \
    if (pixel) pixels[n] = pixel; \
  } \
  mask <<= 1; \
  if (hasSprites) spriteMask <<= 1;

    UNDER_PIXEL(0)
    UNDER_PIXEL(1)
    UNDER_PIXEL(2)
    UNDER_PIXEL(3)
#undef UNDER_PIXEL

    pixels += 4;
    layer1 += 4;
    layer2 += 4;
  }
}

/* the same chunk a byte at a time, for a scroll that leaves a layer unaligned */
static inline void compositeChunkBytes(uint8_t* __restrict pixels, const uint8_t* __restrict layer1,
                                       const uint8_t* __restrict layer2, uint32_t mask, uint32_t spriteMask,
                                       const bool hasSprites)
{
  for (int i = 0; i < 8; ++i)
  {
#define MIXED_PIXEL(n) \
  if (!hasSprites || !(spriteMask & MASK_NEXT_PIXEL)) \
  { \
    pixels[n] = (mask & MASK_NEXT_PIXEL) ? layer2[n] : layer1[n]; \
  } \
  mask <<= 1; \
  if (hasSprites) spriteMask <<= 1;

    MIXED_PIXEL(0)
    MIXED_PIXEL(1)
    MIXED_PIXEL(2)
    MIXED_PIXEL(3)
#undef MIXED_PIXEL

    pixels += 4;
    layer1 += 4;
    layer2 += 4;
  }
}

/* Sprites and the bitmap layer are on the 256-pixel grid whatever the mode, so a wide
   row's chunk of 32 tile pixels is only 16 of theirs: half a mask word, doubled bit by bit through
   the table the magnified sprite emitter already uses. `wide` is a clone parameter rather than a
   count, because that read is inside the chunk loop. */
static inline uint32_t spriteGridWord(const uint32_t maskWord, const BitMask mask, const bool wide)
{
  if (!wide) return mask[maskWord];

  const uint32_t half = (maskWord & 1) ? (mask[maskWord >> 1] & 0xffff) : (mask[maskWord >> 1] >> 16);
  return ((uint32_t)doubledBits[half >> 8] << 16) | doubledBits[half & 0xff];
}

/* A line the zero-copy path would have handed out whole, but for the sprites drawn into pixels[].
 * Rather than copy the tile line over them and merge the sprites back, put the sprites into the
 * tile line and hand that out: a word no sprite touches then costs nothing at all, and there is
 * no second layer or selection mask to read for the ones that do. Every other zero-copy condition
 * already holds here, so nothing else in pixels[] has to survive.
 */
static inline void overlaySpritesOnTile1(uint32_t* __restrict dst, const uint32_t* __restrict src,
                                         const bool wide)
{
  const uint32_t maskWords = (wide ? SCANLINE_BYTES_MAX : TMS9918_PIXELS_X) / 32;

  for (uint32_t maskWord = 0; maskWord < maskWords; ++maskWord, dst += 8, src += 8)
  {
    uint32_t open = spriteGridWord(maskWord, rowMasks.rowSpriteBits, wide);

    for (int i = 0; open; ++i, open <<= 4)
    {
      const uint32_t nibble = open >> 28;
      if (nibble)
      {
        const uint32_t take = maskExpandNibbleToWordRev[nibble];
        dst[i]              = dst[i] ^ ((dst[i] ^ src[i]) & take);
      }
    }
  }
}

PICO9918_INLINE_HOT void
compositeAlignedBody(PICO9918_INST_ARG uint8_t pixels[TMS9918_PIXELS_X], const int t1Scroll, const int t2Scroll,
                     const bool wide)
{
  uint8_t* layer1          = tms9918->tileLayer1Buffer + t1Scroll;
  uint8_t* layer2          = tms9918->tileLayer2Buffer + t2Scroll;
  uint32_t* selectionMask  = tms9918->layerSelectionMask;
  const uint32_t maskWords = (wide ? SCANLINE_BYTES_MAX : TMS9918_PIXELS_X) / 32;

  // For regions with only one layer active, use DMA copy
  // For mixed regions, use CPU compositing

  /* the layer buffers are word-aligned, so scrolls that are multiples of 4 leave both copy
     sources aligned and let each chunk go out as 8 words rather than 32 bytes. Decided once
     per scanline so the chunk path costs no more than a byte-wide one. dmaCopy is idle here:
     pico9918_scan_line drains it before returning */
  const bool wordAligned    = ((t1Scroll | t2Scroll) & 3) == 0;
  const uint32_t chunkCount = wordAligned ? 32 / sizeof(uint32_t) : 32;
  PICO9918_COPY_SET_WIDTH(PICO9918_COPY, wordAligned);

  // Process in 32-pixel chunks (1 mask word at a time)
  for (uint32_t maskWord = 0; maskWord < maskWords; maskWord++)
  {
    uint32_t mask = selectionMask[maskWord];
    /* a priority bitmap layer wins over T1 only - T2 still draws over it */
    uint32_t spriteMask = spriteGridWord(maskWord, rowMasks.rowSpriteBits, wide) |
                          (spriteGridWord(maskWord, rowMasks.rowBits, wide) & ~mask);

    if (spriteMask == 0xffffffffu)
    {
      layer1 += 32;
      layer2 += 32;
      pixels += 32;
      continue;
    }

    if (!underLayer && !spriteMask)
    {
      if (mask == 0)
      {
        // All T1 pixels - use DMA copy for speed
        PICO9918_COPY_WAIT(PICO9918_COPY);
        PICO9918_COPY_SET_SRC(PICO9918_COPY, layer1);
        PICO9918_COPY_SET_DST(PICO9918_COPY, pixels);
        PICO9918_COPY_TRIGGER(PICO9918_COPY, chunkCount);
        pixels += 32;
        layer1 += 32;
        layer2 += 32;
        continue;
      }
      else if (mask == 0xffffffffu)
      {
        // All T2 pixels - use DMA copy for speed
        PICO9918_COPY_WAIT(PICO9918_COPY);
        PICO9918_COPY_SET_SRC(PICO9918_COPY, layer2);
        PICO9918_COPY_SET_DST(PICO9918_COPY, pixels);
        PICO9918_COPY_TRIGGER(PICO9918_COPY, chunkCount);
        pixels += 32;
        layer1 += 32;
        layer2 += 32;
        continue;
      }
    }


    // mixed - process 4 pixels at a time with individual byte access
    if (underLayer)
    {
      if (spriteMask)
        compositeChunkUnder(pixels, layer1, layer2, mask, spriteMask, true);
      else
        compositeChunkUnder(pixels, layer1, layer2, mask, 0, false);

      pixels += 32;
      layer1 += 32;
      layer2 += 32;
    }
    else if (wordAligned)
    {
      uint32_t* pix32    = (uint32_t*)PICO9918_ASSUME_ALIGNED(pixels, 4);
      const uint32_t* l1 = (const uint32_t*)PICO9918_ASSUME_ALIGNED(layer1, 4);
      const uint32_t* l2 = (const uint32_t*)PICO9918_ASSUME_ALIGNED(layer2, 4);

      if (spriteMask)
        compositeChunkWide(pix32, l1, l2, mask, ~(uint32_t)spriteMask, true);
      else
        compositeChunkWide(pix32, l1, l2, mask, 0, false);

      pixels += 32;
      layer1 += 32;
      layer2 += 32;
    }
    else
    {
      if (spriteMask)
        compositeChunkBytes(pixels, layer1, layer2, mask, spriteMask, true);
      else
        compositeChunkBytes(pixels, layer1, layer2, mask, 0, false);

      pixels += 32;
      layer1 += 32;
      layer2 += 32;
    }
  }
}


/* One body per line width. The chunk loop reads the sprite grid on every pass, so the rate it reads
   it at has to be a literal there rather than a value carried in - which is also what keeps the
   256-pixel modes paying nothing for the tier. */
#if PICO9918_TEXT80_8BPP
static EMITTER_NOINLINE
#else
/* one width, one caller, so it inlines: standing it out of line costs RP2040 two-layer lines dearly */
static inline
#endif
  void __time_critical_func(compositeAligned40)(PICO9918_INST_ARG uint8_t pixels[TMS9918_PIXELS_X],
                                                const int t1Scroll, const int t2Scroll)
{
  compositeAlignedBody(PICO9918_INST pixels, t1Scroll, t2Scroll, false);
}

#if PICO9918_TEXT80_8BPP
static EMITTER_NOINLINE void
__time_critical_func(compositeAligned80)(PICO9918_INST_ARG uint8_t pixels[TMS9918_PIXELS_X], const int t1Scroll,
                                         const int t2Scroll)
{
  compositeAlignedBody(PICO9918_INST pixels, t1Scroll, t2Scroll, true);
}
#endif

static inline void compositeAlignedTileBuffers(PICO9918_INST_ARG uint8_t pixels[TMS9918_PIXELS_X],
                                               const int t1Scroll, const int t2Scroll, const bool wide)
{
#if PICO9918_TEXT80_8BPP
  if (wide)
  {
    compositeAligned80(PICO9918_INST pixels, t1Scroll, t2Scroll);
    return;
  }
#endif
  compositeAligned40(PICO9918_INST pixels, t1Scroll, t2Scroll);
}

/**
 * \brief Composite with tile layer 1 disabled (reg 0x32 bit 4). Layer 1 contributes no pixel at all, so
 * every position the selection mask leaves to it must keep whatever the backdrop, bitmap layer and
 * sprites already put there - the colour stage falls through to the backdrop for a transparent
 * merged pixel. Writing layer 1's buffer, or a zero buffer, would paint palette entry 0 instead.
 */
static PICO9918_NOINLINE void
__time_critical_func(compositeTile2OnlyBuffer)(PICO9918_INST_ARG uint8_t pixels[TMS9918_PIXELS_X],
                                               const int t2Scroll, const bool wide)
{
  const uint8_t* layer2    = tms9918->tileLayer2Buffer + t2Scroll;
  const uint32_t maskWords = (wide ? SCANLINE_BYTES_MAX : TMS9918_PIXELS_X) / 32;

  for (uint32_t maskWord = 0; maskWord < maskWords; ++maskWord)
  {
    /* only layer 2 can write, so sprite coverage folds into the mask once per chunk instead of
       being tested per pixel. A priority bitmap layer outranks layer 1 only, and layer 1 is not
       here, so rowBits does not participate */
    uint32_t mask =
      tms9918->layerSelectionMask[maskWord] & ~spriteGridWord(maskWord, rowMasks.rowSpriteBits, wide);

    if (!mask) /* nothing of layer 2 reaches the screen here */
    {
      layer2 += 32;
      pixels += 32;
      continue;
    }

    for (int i = 0; i < 8; ++i)
    {
      if (mask & MASK_NEXT_PIXEL) pixels[0] = layer2[0];
      mask <<= 1;

      if (mask & MASK_NEXT_PIXEL) pixels[1] = layer2[1];
      mask <<= 1;

      if (mask & MASK_NEXT_PIXEL) pixels[2] = layer2[2];
      mask <<= 1;

      if (mask & MASK_NEXT_PIXEL) pixels[3] = layer2[3];
      mask <<= 1;

      pixels += 4;
      layer2 += 4;
    }
  }
}

/**
 * \brief A text row draws 240 of the 256 pixels and the composite copies all of them, so layer 1's buffer
 * has to carry the side borders itself. The row is emitted on cell boundaries and read back
 * `t1Scroll` pixels along, so the border moves with the scroll - and the cells that fall outside it
 * either side are overwritten here rather than never written.
 */
static void __time_critical_func(textRowBorder)(PICO9918_INST_ARG const int t1Scroll, const uint32_t padding,
                                                const uint32_t width)
{
  uint8_t* left  = tms9918->tileLayer1Buffer + t1Scroll;
  uint8_t* right = left + width - padding;

  for (uint32_t i = 0; i < padding; ++i)
  {
    left[i]  = bg;
    right[i] = bg;
  }
}

/** \brief generate a Graphics I or Graphics II mode scanline */
static uint8_t __time_critical_func(graphics_i_scan_line)(PICO9918_INST_ARG uint16_t y,
                                                                   uint8_t pixels[TMS9918_PIXELS_X])
{
  uint8_t tempStatus = 0;

  /* locked or unlocked is decided once here, not re-tested per row */
  if (PICO9918_UNLOCKED(tms9918))
  {
    /* the background fill owns pixels[] until it completes */
    PICO9918_FILL32_WAIT(PICO9918_FILL_LINE);

    bool writeMask = bitmap_layer_scan_line(PICO9918_INST y, pixels);

    /* where a tile draws nothing: the backdrop, unless a bitmap layer is under the tiles and the
       composite's zero-is-transparent path is the only thing that can let it through */
    const uint32_t transparent = underLayer ? 0 : bg;
    transparentPixels[0] = transparentPixels[1] = transparent;
    ecm0Palette[0x00] = ecm0Palette[0x10] = ecm0Palette[0x20] = ecm0Palette[0x30] = transparent;

    tempStatus = pico9918_output_sprites(PICO9918_INST y, pixels);

    if (writeMask) // bitmap layer completely masked it?
    {
      /* how far into its first cell each layer starts. A text cell is six pixels rather than
         eight, so the register divides instead of splitting on a bit boundary */
      const bool wide         = TEXT80_WIDE_ROW;
      const bool textRow      = wide || tmsCachedMode == TMS_MODE_TEXT;
      const int t1Scroll      = scrollOffset(TMS_REGISTER(tms9918, PICO9918_REG_T1_HSCROLL), textRow, wide);
      const int t2Scroll      = scrollOffset(TMS_REGISTER(tms9918, PICO9918_REG_T2_HSCROLL), textRow, wide);
      const bool tile2Enabled = TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED1) & 0x80;
      const bool tile1Enabled = !(TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) & 0x10);

      /* An 8bpp 80-column ECM0 line can have layer 2 merge into layer 1 as it emits rather than into
         a coverage mask for the composite to arbitrate. Above ECM0 it cannot: priority is attr(0)
         per tile there rather than a scanline constant. Layer 1 must have written the line first,
         which is what fixes the order of the three passes below - layer 1, then layer 2 into it,
         then the border over whatever ran past the picture.

         A bitmap layer keeps the composite: a priority one would mask layer 2 along with layer 1
         once they share a buffer, and its coverage is on the 256-pixel grid where layer 2's is on the
         512-pixel one, so "clear it where layer 2 drew" is not expressible bit for bit - a BML pixel
         spans two tile pixels, and a half-covered one is not something to guess at. */
      const bool blend = wide && tile1Enabled && tile2Enabled &&
                         !((TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED1) & 0x30) >> 4) &&
                         !(TMS_REGISTER(tms9918, PICO9918_REG_BML_CONTROL) & 0x80);

      if (tile2Enabled && !blend)
      {
        f18a_tile2_scan_line(PICO9918_INST y, false);
        tmsCopyAlignMask(tms9918->finalMask, tms9918->layerSelectionMask, t1Scroll - t2Scroll);
      }

      if (tile1Enabled)
      {
        f18a_tile1_scan_line(PICO9918_INST y);
        if (blend) f18a_tile2_scan_line(PICO9918_INST y, true);
        if (textRow)
          textRowBorder(PICO9918_INST t1Scroll, wide ? TEXT80_PADDING_PX : TEXT_PADDING_PX,
                        wide ? SCANLINE_BYTES_MAX : TMS9918_PIXELS_X);
      }

      /* the mask has to reach screen space whether or not layer 1 ran - inside that branch, a
         disabled layer 1 left the composite reading it in layer 2's buffer positions. With no
         layer 2 there is no coverage to place: nothing has written either mask since they were
         zeroed, so the shift would copy zeros onto zeros */
      if (tile2Enabled && !blend)
        tmsCopyAlignMask(tms9918->layerSelectionMask, tms9918->finalMask, -t1Scroll);

      /* text is emitted in whole cells either side of the picture it shows, so trim layer 2's
         coverage back to them: nothing outside may claim a border pixel. The picture runs from the
         padding for six pixels a column, whichever count and depth this is */
      if (textRow && !blend)
      {
        const uint32_t pad = wide ? TEXT80_PADDING_PX : TEXT_PADDING_PX;
        const uint32_t end = pad + (wide ? TEXT80_NUM_COLS : TEXT_NUM_COLS) * TEXT_CHAR_WIDTH;
        tms9918->layerSelectionMask[0] &= 0xffffffffu >> pad;
        tms9918->layerSelectionMask[(end - 1) >> 5] &= ~(0xffffffffu >> (end & 0x1f));
      }

      /* Nothing to arbitrate: no second layer and no bitmap layer, so no tile has anything to rank
         against. Layer 1's buffer already carries the backdrop where a tile drew nothing and the
         borders `textRowBorder` wrote, so it *is* the finished line - merging it into another one
         would copy 256 or 512 bytes to change nothing.

         The fine scroll has to leave it word-aligned. A caller is entitled to read the line a word
         at a time, and on Cortex-M0+ an unaligned word load is a HardFault rather than a slow one -
         so a scroll of 3 handed straight out took the RP2040 down, where the RP2350 never noticed. */
      if (tile1Enabled && (!tile2Enabled || blend) && !underLayer && !(t1Scroll & 3) &&
          !(TMS_REGISTER(tms9918, PICO9918_REG_BML_CONTROL) & 0x80))
      {
        uint8_t* line = tms9918->tileLayer1Buffer + t1Scroll;

        /* sprites are the one thing in pixels[] a line like this still owes, and they reach the
           tile buffer for less than the buffer reaches them */
        if (tms9918->scanlineHasSprites)
          overlaySpritesOnTile1((uint32_t*)PICO9918_ASSUME_ALIGNED(line, 4),
                                (const uint32_t*)PICO9918_ASSUME_ALIGNED(pixels, 4), wide);

        lineSource = line;
      }
      else if (tile1Enabled)
      {
        compositeAlignedTileBuffers(PICO9918_INST pixels, t1Scroll, t2Scroll, wide);
      }
      else if (tile2Enabled)
      {
        compositeTile2OnlyBuffer(PICO9918_INST pixels, t2Scroll, wide);
      }
      /* both layers off: the backdrop, bitmap layer and sprites are already in pixels[] */
    }
  }
  else
  {
    const uint8_t tileY = y >> 3; /* which name table row (0 - 23)... or 29 */

    /* address in name table at the start of this row */
    const uint16_t rowOffset = tileY * GRAPHICS_NUM_COLS;
    uint16_t rowNamesAddr    = tmsNameTableAddr(tms9918) + rowOffset;
    uint16_t colorTableAddr  = tmsColorTableAddr(tms9918);

    const bool gm2 = tmsCachedMode == TMS_MODE_GRAPHICS_II;
    const bool mcm = tmsCachedMode == TMS_MODE_MULTICOLOR;
    TileRowAddr addr;
    tileRowAddr(PICO9918_INST y, y, TMS_REGISTER(tms9918, TMS_REG_COLOR_TABLE), gm2, mcm, &addr,
                &colorTableAddr);

    PICO9918_FILL32_WAIT(PICO9918_FILL_LINE);

    if (gm2)
      rowLockedGm2(PICO9918_INST rowNamesAddr, colorTableAddr, 0, 0, pixels, &addr);
    else if (mcm)
      rowLockedMcm(PICO9918_INST rowNamesAddr, colorTableAddr, 0, 0, pixels, &addr);
    else
      rowLockedGm1(PICO9918_INST rowNamesAddr, colorTableAddr, 0, 0, pixels, &addr);

    tempStatus = pico9918_output_sprites(PICO9918_INST y, pixels);
  }

  return tempStatus;
}

/** \brief generate a scanline */
PICO9918_DLLEXPORT uint8_t __time_critical_func(pico9918_scan_line)(PICO9918_INST_ARG uint16_t y)
{
  uint8_t* const pixels = scanlineBuffer;
  uint8_t tempStatus    = 0;

  /* Guarded here as well as inside: initLookups is noinline and flash-resident, so an
     unconditional call puts an XIP fetch and a stack frame on every scanline. */
  if (!lookupsReady) initLookups();

  pico9918_mode_t currentCachedMode = tmsMode(tms9918);
  if (currentCachedMode != tmsCachedMode)
  {
    tmsCachedMode     = currentCachedMode;
    tms9918->palDirty = 1;
  }

  /* clear the buffer with background color. The backdrop is a six-bit palette address too and tile
     layer 1 selects its sub-palette, which is also what a tile writes where it
     draws nothing. Four bits a pixel have no room for the selector, so 80 columns hold
     the plain index in both nibbles instead. */
  const uint8_t bgc        = tmsMainBgColor(tms9918);
  const bool packedNibbles = tmsCachedMode == TMS_MODE_TEXT80 && !TEXT80_WIDE_ROW;
  bg = repeatedPalette(bgc | (packedNibbles ? bgc << 4 : (TMS_REGISTER(tms9918, PICO9918_REG_PALETTE_SELECT) & 0x03) << 4));
#if PICO9918_TEXT80_8BPP
  /* a wide row is twice the line to fill, and the count is per mode rather than per build */
  PICO9918_FILL32_SET_COUNT(PICO9918_FILL_LINE, pico9918_line_bytes(PICO9918_INST_ONLY) / 4);
#endif
  PICO9918_FILL32_TRIGGER(PICO9918_FILL_LINE, pixels);
  lineSource = pixels;
  underLayer = false;

  bool dispActive = (TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_DISP_ACTIVE);

  if (dispActive)
  {
    /* the three row masks go out as one transfer; the instance masks below cover its latency */
    PICO9918_FILL32_TRIGGER(PICO9918_FILL_MASKS, &rowMasks);

    for (int i = 0; i < SCANLINE_MASK_WORDS; ++i)
    {
      tms9918->layerSelectionMask[i] = 0; // Default to all T1 pixels
      tms9918->finalMask[i]          = 0;
    }
    tms9918->scanlineHasSprites = false;

    PICO9918_FILL32_WAIT(PICO9918_FILL_MASKS);

    switch (tmsCachedMode)
    {
    case TMS_MODE_GRAPHICS_I:
    case TMS_MODE_GRAPHICS_II:
    case TMS_MODE_MULTICOLOR: tempStatus = graphics_i_scan_line(PICO9918_INST y, pixels); break;

    case TMS_MODE_TEXT:
    case TMS_MODE_TEXT80:
      /* one byte a pixel means the layer buffers and the composite fit, which is 40 columns always
           and 80 only where the tier gives them the depth: at four bits one mask bit covers two
           pixels and a layer cannot be selected per pixel at all. The line is bit depth,
           not cell width - and locked text has nothing to arbitrate either way */
      if (PICO9918_UNLOCKED(tms9918) && (tmsCachedMode == TMS_MODE_TEXT || TEXT80_WIDE_ROW))
      {
        tempStatus = graphics_i_scan_line(PICO9918_INST y, pixels);
        break;
      }

      text_scan_line(PICO9918_INST y, pixels);
      if (PICO9918_UNLOCKED(tms9918)) tempStatus = pico9918_output_sprites(PICO9918_INST y, pixels);
      break;
    }
  }

  /* pixels[] must be complete, and owned by nobody, when we return */
  PICO9918_FILL32_WAIT(PICO9918_FILL_LINE);
  PICO9918_COPY_WAIT(PICO9918_COPY);

  return tempStatus;
}

/** \brief return a register value */
PICO9918_DLLEXPORT
uint8_t __time_critical_func(pico9918_reg_value)(PICO9918_INST_ARG pico9918_register_t reg)
{
  return TMS_REGISTER(tms9918, reg & tms9918->lockedMask); // was 0x07
}

/** \brief write a register value */
PICO9918_DLLEXPORT
void __time_critical_func(pico9918_write_reg_value)(PICO9918_INST_ARG pico9918_register_t reg, uint8_t value)
{
  if (PICO9918_HAS(tms9918, PICO9918_FEAT_UNLOCK) && PICO9918_UNLOCK_WRITE(reg, value))
  {
    TMS_REGISTER(tms9918, PICO9918_REG_UNLOCK) = 0x1c; // Allow this one through even when locked
    if (++tms9918->unlockCount == 2)
    {
      tms9918->unlockCount        = 0;
      tms9918->isUnlocked         = true;
      tms9918->lockedMask         = 0x3f;
      TMS_REGISTER(tms9918, PICO9918_REG_MAX_SCAN_SPRITES) = MAX_SPRITES - 1; // scanline sprite limit
      /* the 80-column line is a byte a pixel once unlocked, and the LUT is built to
         match it. The mode has not changed, so nothing else raises this - and the
         rebuild must land before this line renders, not on the next one. */
      tms9918->palDirty = 1;
    }
  }
  else
  {
    tms9918->unlockCount = 0;

    int regIndex = reg & tms9918->lockedMask; // was 0x07

    /* Locked only, since reg is 0x80-0xBF: a 9918A latches three address bits, so VR8 is
       VR0. An F18A keeps VR57 reachable, and with M4 set ignores VR8+ rather than aliasing. */
    if ((reg & ~tms9918->lockedMask) != 0x80)
    {
      if (reg == (0x80 | 0x39) && PICO9918_CAN_UNLOCK(tms9918))
        regIndex = 0x39;
      else if (PICO9918_M4(tms9918))
        return;
    }

    TMS_REGISTER(tms9918, regIndex) = value;
    if (regIndex < 0x0f) return;

    if ((regIndex == 0x37) || ((regIndex == 0x38) && ((value & 1) == 0)))
    {
      tms9918->gpuAddress = ((TMS_REGISTER(tms9918, PICO9918_REG_GPU_PC_MSB) << 8) | TMS_REGISTER(tms9918, PICO9918_REG_GPU_PC_LSB)) & 0xFFFE;
      if (regIndex == 0x37)
      {
        TMS_REGISTER(tms9918, PICO9918_REG_GPU_CONTROL) = 0;
        tms9918->gpuStatus          = 0; /* a new program, not a resumed one */
        tms9918->restart            = 1;
        /* Here, not a scanline later: an F18A probe reads its result back within a
           handful of host cycles. A no-op unless the host set a GPU rate. */
        pico9918_gpu_service(PICO9918_INST_ONLY);
      }
    }
    else if ((regIndex == 0x38) && (value & 1))
    {
      tms9918->restart = 1;
      pico9918_gpu_service(PICO9918_INST_ONLY);
    }
    else if (regIndex == 0x3F && PICO9918_HAS(tms9918, PICO9918_FEAT_CONFIG)) // firmware update
    {
      // b7      : 0 = idle:   1 = execute
      // b6      : 0 = verify: 1 = write
      // b5 - b0 : address to read firmware data (256 byte boundaries)
      //           reads one UF2 frame (512 bytes)
      if (TMS_REGISTER(tms9918, PICO9918_REG_GPU_CONTROL) == 0)
      {
        TMS_STATUS(tms9918, 2) = 0x80; // set gpu processing flag
        tms9918->flash         = 1;
      }
      else
      {
        TMS_STATUS(tms9918, 2) = 0x14; // error - busy
      }
    }
    else if (regIndex == 0x1e && value == 0)
    {
      TMS_REGISTER(tms9918, PICO9918_REG_MAX_SCAN_SPRITES) = MAX_SPRITES - 1;
    }
    else if ((regIndex == 0x32) && (value & 0x80))
    { // reset all registers?
      vdpRegisterReset(tms9918);

      // reset palette, etc as well?
      if (value & 0x40)
      {
        tms9918->configDirty    = true;
        tms9918->configVdpDirty = true;
      }
    }
    else if (regIndex == 0x0F)
    {
      uint8_t statReg           = (value & 0x0f);
      TMS_STATUS(tms9918, 0x0F) = statReg; // is this right? or should this be the read-ahead value?
      if (value & 0x40) tms9918->startTime = PICO9918_HOST_TIME_US(); // reset
      if (value & 0x20)
        tms9918->currentTime = PICO9918_HOST_TIME_US(); // snap
      else if (value & 0x10)
        tms9918->startTime += (tms9918->stopTime - tms9918->startTime);
      else
        tms9918->currentTime = tms9918->stopTime = PICO9918_HOST_TIME_US();

      if (statReg > 3 && statReg < 12)
      {
        uint32_t elapsed = tms9918->currentTime - tms9918->startTime;
        uint32_t microQ, microR;
        PICO9918_DIVMOD_U32(elapsed, 1000, microQ, microR);
        uint32_t milliQ, milliR;
        PICO9918_DIVMOD_U32(microQ, 1000, milliQ, milliR);

        TMS_STATUS(tms9918, 0x06) = microR & 0x0ff;
        TMS_STATUS(tms9918, 0x07) = microR >> 8;
        TMS_STATUS(tms9918, 0x08) = milliR & 0x0ff;
        TMS_STATUS(tms9918, 0x09) = milliR >> 8;
        TMS_STATUS(tms9918, 0x0a) = milliQ & 0x00ff;
        TMS_STATUS(tms9918, 0x0b) = milliQ >> 8;
      }
    }
    // SR12 holds the value of the option in VR58 (options)
    else if (regIndex == 58 && PICO9918_HAS(tms9918, PICO9918_FEAT_CONFIG))
    {
      TMS_STATUS(tms9918, 12) = tms9918->config[TMS_REGISTER(tms9918, PICO9918_REG_CONFIG_INDEX)];
    }
    // option number in reg 58, value in 59 (options)
    else if (regIndex == 59 && PICO9918_HAS(tms9918, PICO9918_FEAT_CONFIG) &&
             TMS_REGISTER(tms9918, PICO9918_REG_CONFIG_INDEX) >= 8)
    {
      tms9918->config[TMS_REGISTER(tms9918, PICO9918_REG_CONFIG_INDEX)] = value;
      TMS_STATUS(tms9918, 12)                    = value;
      tms9918->configDirty                       = true;
    }
  }
}


/** \brief return a value from vram */
PICO9918_DLLEXPORT
uint8_t __time_critical_func(pico9918_vram_value)(PICO9918_INST_ARG uint16_t addr)
{
  return tms9918->vram.bytes[addr & VRAM_MASK];
}

/** \brief check BLANK flag */
PICO9918_DLLEXPORT
bool __time_critical_func(pico9918_display_enabled)(PICO9918_INST_ONLY_ARG)
{
  return (TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_DISP_ACTIVE);
}

/** \brief current display mode */
PICO9918_DLLEXPORT
pico9918_mode_t __time_critical_func(pico9918_display_mode)(PICO9918_INST_ONLY_ARG)
{
  return tmsCachedMode;
}

/**
 * \brief how many bytes of pixels[] this mode fills. Every mode is 256 but unlocked 80-column text on a
 * board with the 8bpp tier, which is 512 - so the palette expansion, the backdrop fill and anything
 * reading the line ask here rather than each deciding it again.
 */
PICO9918_DLLEXPORT
uint32_t __time_critical_func(pico9918_line_bytes)(PICO9918_INST_ONLY_ARG)
{
  return TEXT80_WIDE_ROW ? SCANLINE_BYTES_MAX : TMS9918_PIXELS_X;
}

/**
 * \brief where the scanline just generated actually is. Usually the buffer that was passed in, but on a
 * line with nothing to arbitrate it is a tile layer's own buffer and the passed one holds only the
 * backdrop fill - so read the line from here rather than from what was handed over.
 */
PICO9918_DLLEXPORT
const uint8_t* __time_critical_func(pico9918_line_source)(PICO9918_INST_ONLY_ARG)
{
  return lineSource;
}

/** \brief a default palette entry, 0xargb */
PICO9918_DLLEXPORT
uint16_t pico9918_default_palette(int index)
{
  return defaultPalette[index & 0x3f];
}
