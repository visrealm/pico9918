/**
 * \file
 * \brief VGA and RGBs output - mode description types and driver interface
 *
 * Project: pico9918 - vga
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

#pragma once

#include <inttypes.h>
#include <stdbool.h>

// Start pins may be overridden at build time via the CMake config file
// (pico9918_config.cmake) which passes -DPICO9918_VGA_*_PINS_START defines.
#ifndef PICO9918_VGA_SYNC_PINS_START
#define PICO9918_VGA_SYNC_PINS_START 0
#endif
#define VGA_SYNC_PINS_START PICO9918_VGA_SYNC_PINS_START
#define VGA_SYNC_PINS_COUNT 2

#ifndef PICO9918_VGA_RGB_PINS_START
#define PICO9918_VGA_RGB_PINS_START 2
#endif
#define VGA_RGB_PINS_START PICO9918_VGA_RGB_PINS_START
#define VGA_RGB_PINS_COUNT 12

/** \brief one axis of a mode: active area, porches and sync, in pixels or lines */
typedef struct
{
  uint16_t displayPixels;
  uint16_t frontPorchPixels;
  uint16_t syncPixels;
  uint16_t backPorchPixels;
  uint16_t totalPixels;
  float freqHz;
  bool syncHigh;
} VgaSyncParams;

/** \brief interlaced sync line types, each a full line of two half-line pulses */
enum
{
  VSYNC_LSLS = 0, /**< long sync + long sync */
  VSYNC_LSEQ,     /**< long sync + short sync */
  VSYNC_EQEQ,     /**< short sync + short sync */
  VSYNC_EQLS,     /**< short sync + long sync (interlace transition) */
  VSYNC_PORCH,    /**< normal hsync (porch line) */
  VSYNC_TYPE_COUNT
};

#define VGA_MAX_VSYNC_LINES    8
#define VGA_MAX_TRAILING_LINES 12
#define VGA_MAX_FIELDS         2

/** \brief the line sequence of one interlaced field */
typedef struct
{
  uint8_t vsyncLines;                              /**< number of vsync lines */
  uint8_t vsyncPattern[VGA_MAX_VSYNC_LINES];       /**< vsync line sequence */
  uint8_t porchLines;                              /**< back porch lines */
  uint16_t activeLines;                            /**< active display lines */
  uint8_t trailingLines;                           /**< trailing lines after active */
  uint8_t trailingPattern[VGA_MAX_TRAILING_LINES]; /**< trailing line types */
  uint16_t totalLines;                             /**< total lines this field */
} VgaFieldParams;

/** \brief a complete display mode: timing, pixel scale and derived PIO clocking */
typedef struct
{
  uint32_t pixelClockKHz;
  VgaSyncParams hSyncParams;
  VgaSyncParams vSyncParams; /**< used by non-interlaced VGA path */
  uint16_t hVirtualPixels;
  uint16_t vVirtualPixels;
  float pioDivider;
  float pioFreqKHz;
  float pioClocksPerPixel;
  float pioClocksPerScaledPixel;
  float frameRateHz; /**< effective frame rate (e.g. 60, 50) */
  bool interlaced;
  uint8_t interlacedFieldOrder; /**< 0 or 1, XOR'd with field number for double-row mapping */
  uint8_t numFields;            /**< 1 = progressive, 2 = interlaced */
  uint8_t hPixelScale;
  uint8_t vPixelScale;
  uint8_t shortPulsePixels; /**< EQ pulse low duration in pixels (interlaced only) */
  VgaFieldParams fields[VGA_MAX_FIELDS];
} VgaParams;


/** \brief fill one scanline of rgb pixels
 *  \param y bits 11:0 are the virtual line, bit 12 is the field for interlaced modes
 */
typedef void (*vgaScanlineRgbFn)(uint16_t y, VgaParams* params, uint16_t* pixels);

/** \brief called once the last line of a frame has been requested */
typedef void (*vgaEndOfFrameFn)(uint32_t frameNumber);

/** \brief called when the vertical front porch begins */
typedef void (*vgaPorchFn)(void);

/** \brief called once the scanline set by vgaSetTriggerScanline() has been displayed */
typedef void (*vgaEndOfScanlineFn)(uint32_t displayLine);

/** \brief a mode plus the core 1 callbacks that drive it */
typedef struct
{
  VgaParams params;
  vgaScanlineRgbFn scanlineFn;
  vgaEndOfFrameFn endOfFrameFn;
  vgaEndOfScanlineFn endOfScanlineFn;
  vgaPorchFn porchFn;
  bool scanlines;
  uint32_t triggerScanline; /**< scanline to fire endOfScanlineFn on; UINT32_MAX to disable */
} VgaInitParams;

/** \brief run the core 1 render loop, servicing the sync ISR's requests; never returns */
void vgaLoop(void);

/** \brief set up the pins, PIO and DMA for \p params, and start output */
void vgaInit(VgaInitParams params);

/** \brief the live parameters, as copied by vgaInit() */
VgaInitParams* vgaCurrentParams(void);

/** \brief set the scanline endOfScanlineFn fires on */
void vgaSetTriggerScanline(uint32_t scanline);
