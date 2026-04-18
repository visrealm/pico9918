/*
 * Project: pico9918 - vga
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico-56
 *
 */

#pragma once

#include <inttypes.h>
#include <stdbool.h>

#define VGA_SYNC_PINS_START  0
#define VGA_SYNC_PINS_COUNT  2
#define VGA_RGB_PINS_START   2
#define VGA_RGB_PINS_COUNT  12

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

// Interlaced sync line types (each is a full line = 2 half-line pulses)
enum
{
  VSYNC_LSLS = 0,  // long sync + long sync
  VSYNC_LSEQ,      // long sync + short sync
  VSYNC_EQEQ,      // short sync + short sync
  VSYNC_EQLS,      // short sync + long sync (interlace transition)
  VSYNC_PORCH,     // normal hsync (porch line)
  VSYNC_TYPE_COUNT
};

#define VGA_MAX_VSYNC_LINES 8
#define VGA_MAX_TRAILING_LINES 12
#define VGA_MAX_FIELDS 2

typedef struct
{
  uint8_t vsyncLines;                                     // number of vsync lines
  uint8_t vsyncPattern[VGA_MAX_VSYNC_LINES];              // vsync line sequence
  uint8_t porchLines;                                     // back porch lines
  uint16_t activeLines;                                   // active display lines
  uint8_t trailingLines;                                  // trailing lines after active
  uint8_t trailingPattern[VGA_MAX_TRAILING_LINES];        // trailing line types
  uint16_t totalLines;                                    // total lines this field
} VgaFieldParams;

typedef struct
{
  uint32_t pixelClockKHz;
  VgaSyncParams hSyncParams;
  VgaSyncParams vSyncParams;        // used by non-interlaced VGA path
  uint16_t hVirtualPixels;
  uint16_t vVirtualPixels;
  float pioDivider;
  float pioFreqKHz;
  float pioClocksPerPixel;
  float pioClocksPerScaledPixel;
  float frameRateHz;                          // effective frame rate (e.g. 60, 50)
  bool interlaced;
  uint8_t interlacedFieldOrder;             // 0 or 1: XOR'd with field number for double-row mapping
  uint8_t numFields;                        // 1 = progressive, 2 = interlaced
  uint8_t hPixelScale;
  uint8_t vPixelScale;
  uint8_t shortPulsePixels;                 // EQ pulse low duration in pixels (interlaced only)
  VgaFieldParams fields[VGA_MAX_FIELDS];
} VgaParams;


// For interlaced modes, y encodes: bits 11:0 = virtual line (0..N-1), bit 12 = field (0 or 1)
typedef void (*vgaScanlineRgbFn)(uint16_t y, VgaParams* params, uint16_t* pixels);
typedef void (*vgaEndOfFrameFn)(uint32_t frameNumber);
typedef void (*vgaPorchFn)();
typedef void (*vgaInitFn)();
typedef void (*vgaEndOfScanlineFn)(uint32_t displayLine);

extern uint32_t vgaMinimumPioClockKHz(VgaParams* params);

typedef struct
{
  VgaParams params;
  vgaInitFn initFn;
  vgaScanlineRgbFn scanlineFn;
  vgaEndOfFrameFn endOfFrameFn;
  vgaEndOfScanlineFn endOfScanlineFn;
  vgaPorchFn porchFn;
  bool scanlines;
  uint32_t triggerScanline;  // scanline to fire endOfScanlineFn on; UINT32_MAX to disable
} VgaInitParams;

void vgaLoop();

void vgaInit(VgaInitParams params);

VgaInitParams *vgaCurrentParams();

void vgaSetTriggerScanline(uint32_t scanline);
