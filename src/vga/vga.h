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

typedef struct
{
  uint32_t displayPixels;
  uint32_t frontPorchPixels;
  uint32_t syncPixels;
  uint32_t backPorchPixels;
  uint32_t totalPixels;
  float freqHz;
  bool syncHigh;
} VgaSyncParams;

// Interlaced sync line types (each is a full line = 2 half-line pulses)
typedef enum
{
  VSYNC_LSLS = 0,  // long sync + long sync
  VSYNC_LSEQ,      // long sync + short sync
  VSYNC_EQEQ,      // short sync + short sync
  VSYNC_EQLS,      // short sync + long sync (interlace transition)
  VSYNC_PORCH,      // normal hsync (porch line)
  VSYNC_TYPE_COUNT
} VgaVsyncLineType;

#define VGA_MAX_VSYNC_LINES 8
#define VGA_MAX_TRAILING_LINES 4
#define VGA_MAX_FIELDS 2

// Half-line sync pulse definition (for interlaced vsync region).
// Each half-line = shortPulsePixels + (hSyncParams.totalPixels/2 - shortPulsePixels).
// The LS (long sync) pulse width = halfLine - shortPulsePixels (by definition).
typedef struct
{
  uint32_t shortPulsePixels;   // EQ pulse low duration in pixels (e.g. 27 at 13.5MHz = 2us)
} VgaHalfLineSyncParams;

typedef struct
{
  uint32_t vsyncLines;                                    // number of vsync lines
  VgaVsyncLineType vsyncPattern[VGA_MAX_VSYNC_LINES];    // vsync line sequence
  uint32_t porchLines;                                    // back porch lines
  uint32_t activeLines;                                   // active display lines
  uint32_t trailingLines;                                 // trailing lines after active
  VgaVsyncLineType trailingPattern[VGA_MAX_TRAILING_LINES]; // trailing line types
  uint32_t totalLines;                                    // total lines this field
} VgaFieldParams;

typedef struct
{
  uint32_t pixelClockKHz;
  VgaSyncParams hSyncParams;
  VgaSyncParams vSyncParams;        // used by non-interlaced VGA path
  uint32_t hPixelScale;
  uint32_t vPixelScale;
  uint32_t hVirtualPixels;
  uint32_t vVirtualPixels;
  float pioDivider;
  float pioFreqKHz;
  float pioClocksPerPixel;
  float pioClocksPerScaledPixel;
  bool interlaced;
  uint8_t interlacedFieldOrder;             // 0 or 1: XOR'd with field number for double-row mapping
  uint32_t numFields;                       // 1 = progressive, 2 = interlaced
  VgaHalfLineSyncParams halfLineSync;       // EQ/LS pulse widths (interlaced only)
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
