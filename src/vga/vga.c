/*
 * Project: pico9918 - vga
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 */

#include "vga.h"
#include "vga.pio.h"
#include "pio_utils.h"

#include "../display.h"

#include "pico/multicore.h"
#include "pico/binary_info.h"

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#if PICO9918_SCART_AUTODETECT
  #define VGA_COMBINE_SYNC_RUNTIME 1
#else
  #define VGA_COMBINE_SYNC PICO9918_SCART_RGBS
#endif

#define VGA_NO_MALLOC 1

// avoid bringing in math.h
int roundflt(float x)
{
  if (x < 0.0f)
    return (int)(x - 0.5f);
  else
    return (int)(x + 0.5f);
}


#define SYNC_PINS_START VGA_SYNC_PINS_START
#define SYNC_PINS_COUNT VGA_SYNC_PINS_COUNT

#define RGB_PINS_START  VGA_RGB_PINS_START
#define RGB_PINS_COUNT  VGA_RGB_PINS_COUNT

#define VGA_PIO         pio0_hw  // which pio are we using for vga?
#define SYNC_SM         0        // vga sync state machine index
#define RGB_SM          1        // vga rgb state machine index

#define FRONT_PORCH_MSG 0x20000000
#define END_OF_SCANLINE_MSG 0x40000000
#define END_OF_FRAME_MSG    0x80000000

#if VGA_COMBINE_SYNC_RUNTIME
bi_decl(bi_1pin_with_name(SYNC_PINS_START, "Sync"));
bi_decl(bi_1pin_with_name(SYNC_PINS_START + 1, "Sync"));
#elif VGA_COMBINE_SYNC
bi_decl(bi_1pin_with_name(SYNC_PINS_START, "C Sync"));
bi_decl(bi_1pin_with_name(SYNC_PINS_START + 1, "C Sync"));
#else
bi_decl(bi_1pin_with_name(SYNC_PINS_START, "H Sync"));
bi_decl(bi_1pin_with_name(SYNC_PINS_START + 1, "V Sync"));
#endif

bi_decl(bi_pin_mask_with_names(0xf << RGB_PINS_START, "Red (LSB - MSB)"));
bi_decl(bi_pin_mask_with_names(0xf << RGB_PINS_START + 4, "Green (LSB - MSB)"));
bi_decl(bi_pin_mask_with_names(0xf << RGB_PINS_START + 8, "Blue (LSB - MSB)"));

/*
 * sync pio dma data buffers
 */
uint32_t __aligned(8) syncDataActive[4];  // active display full line (4 words, 64us)
uint32_t __aligned(8) syncDataPorch[4];   // porch/blanking full line (4 words, 64us)
uint32_t __aligned(8) syncDataSync[4];    // VGA vsync full line     (4 words, non-interlaced only)

// Interlaced PAL/NTSC: full-line vsync buffers (4 words each = 64us = 2 half-lines).
// All DMA transfers are always 4 words — no transfer count switching, no FIFO starvation.
//
// Each buffer encodes two half-line sync pulses:
//   EQ (short sync): 2us low + 30us high = 32us per half-line
//   LS (long sync):  30us low + 2us high = 32us per half-line
//
// Per-field line sequences (ISR dispatches one buffer per line):
//   F1 (313 lines): LsLs LsLs LsEq EqEq EqEq [17 porch] [288 active] EqEq EqEq EqLs
//   F2 (312 lines): LsLs LsLs EqEq EqEq       [17 porch] [288 active] EqEq EqEq EqEq
//
// F1 line 312 = EqLs creates the half-line offset: left half is F1's last EQ,
// right half is F2's first LS. This is the interlace transition line.
// F2 has no EqLs — its LS starts cleanly at line 0, giving the 0.5-line offset.
//
//   F1→F2 LS interval: (313 - 0) - 0.5 + 0 = 312.5 lines ✓
//   F2→F1 LS interval: (312 - 0) + 0.5 = 312.5 lines ✓
uint32_t __aligned(4) syncDataLsLs[4];    // LS + LS
uint32_t __aligned(4) syncDataLsEq[4];    // LS + EQ  (F1 LS→EQ transition at line 2)
uint32_t __aligned(4) syncDataEqEq[4];    // EQ + EQ
uint32_t __aligned(4) syncDataEqLs[4];    // EQ + LS  (F1 last line — interlace transition)

#if VGA_NO_MALLOC
__attribute__((section(".scratch_y.lookup"))) uint16_t __aligned(4) rgbDataBuffer[2][RGB_PIXELS_X] = { 0 };   // two scanline buffers (odd and even)
#else
#include <stdlib.h>
uint16_t* __aligned(8) rgbDataBuffer[2 + VGA_SCANLINE_TIME_DEBUG] = { 0 };                          // two scanline buffers (odd and even)
#endif


/*
 * file scope
 */
static int syncDmaChan = 0;
static int rgbDmaChan = 0;
static uint syncDmaChanMask = 0;
static uint rgbDmaChanMask = 0;
static VgaInitParams vgaParams = { 0 };
static pio_sm_config rgbConfig;
uint rgbProgOffset;

uint32_t vgaMinimumPioClockKHz(VgaParams* params)
{
  if (params)
  {
    // we need a minimum of two clock cycles for each pixel
    // if you're pixel doubling, you can go slower by a multiple
    // of your doubling factor

    return params->pixelClockKHz * vga_rgb_LOOP_TICKS / params->hPixelScale;
  }
}


// Map VgaVsyncLineType enum to sync data buffer pointers.
// Populated by buildSyncData() when interlaced is true.
static const uint32_t* vsyncTypeBuffers[VSYNC_TYPE_COUNT];

/*
 * build the sync data buffers
 */
static bool buildSyncData()
{
  uint32_t sysClockKHz = clock_get_hz(clk_sys) / 1000;
  uint32_t minClockKHz = vgaMinimumPioClockKHz(&vgaParams.params);
  if (minClockKHz < 50000) minClockKHz *= 2;

  if (sysClockKHz < minClockKHz)
  {
    return false;
  }

#if !VGA_NO_MALLOC
  rgbDataBuffer[0] = malloc(RGB_PIXELS_X * sizeof(uint16_t));
  rgbDataBuffer[1] = malloc(RGB_PIXELS_X * sizeof(uint16_t));
#endif

  vgaParams.params.pioDivider = roundflt(sysClockKHz / (float)minClockKHz);
  vgaParams.params.pioFreqKHz = sysClockKHz / vgaParams.params.pioDivider;

  vgaParams.params.pioClocksPerPixel = vgaParams.params.pioFreqKHz / (float)vgaParams.params.pixelClockKHz;
  vgaParams.params.pioClocksPerScaledPixel = vgaParams.params.pioFreqKHz * vgaParams.params.hPixelScale / (float)vgaParams.params.pixelClockKHz;

  const uint32_t activeTicks = roundflt(vgaParams.params.pioClocksPerPixel * (float)vgaParams.params.hSyncParams.displayPixels) - vga_sync_SETUP_OVERHEAD;
  const uint32_t fPorchTicks = roundflt(vgaParams.params.pioClocksPerPixel * (float)vgaParams.params.hSyncParams.frontPorchPixels) - vga_sync_SETUP_OVERHEAD;
  const uint32_t syncTicks = roundflt(vgaParams.params.pioClocksPerPixel * (float)vgaParams.params.hSyncParams.syncPixels) - vga_sync_SETUP_OVERHEAD;
  const uint32_t bPorchTicks = roundflt(vgaParams.params.pioClocksPerPixel * (float)vgaParams.params.hSyncParams.backPorchPixels) - vga_sync_SETUP_OVERHEAD;

  uint32_t rgbCyclesPerPixel = roundflt(vgaParams.params.pioClocksPerScaledPixel);


  // compute sync bits
  const uint32_t hSyncOff = !vgaParams.params.hSyncParams.syncHigh << vga_sync_WORD_HSYNC_OFFSET;
  const uint32_t hSyncOn = vgaParams.params.hSyncParams.syncHigh << vga_sync_WORD_HSYNC_OFFSET;
  const uint32_t vSyncOff = !vgaParams.params.vSyncParams.syncHigh << vga_sync_WORD_VSYNC_OFFSET;
  const uint32_t vSyncOn = vgaParams.params.vSyncParams.syncHigh << vga_sync_WORD_VSYNC_OFFSET;

#if VGA_COMBINE_SYNC_RUNTIME
  const bool combinedSync = vgaParams.params.interlaced;
#elif VGA_COMBINE_SYNC
  const bool combinedSync = true;
#else
  const bool combinedSync = false;
#endif

  uint32_t HoffVoff, HonVoff, HoffVon, HonVon;
  if (combinedSync) {
    HoffVoff = hSyncOff | vSyncOff;
    HonVoff  = hSyncOn  | vSyncOn;
    HoffVon  = hSyncOn  | vSyncOn;
    HonVon   = hSyncOff | vSyncOff;
  } else {
    HoffVoff = hSyncOff | vSyncOff;
    HonVoff  = hSyncOn  | vSyncOff;
    HoffVon  = hSyncOff | vSyncOn;
    HonVon   = hSyncOn  | vSyncOn;
  }

  // compute exec instructions
  const uint32_t instIrq = pio_encode_irq_set(false, vga_rgb_RGB_IRQ) << vga_sync_WORD_EXEC_OFFSET;
  const uint32_t instNop = pio_encode_nop() << vga_sync_WORD_EXEC_OFFSET;

  // sync data for an active display scanline
  const int SYNC_LINE_ACTIVE = 0;
  const int SYNC_LINE_FPORCH = 1;
  const int SYNC_LINE_HSYNC = 2;
  const int SYNC_LINE_BPORCH = 3;

  syncDataActive[SYNC_LINE_ACTIVE] = instIrq | HoffVoff | activeTicks;
  syncDataActive[SYNC_LINE_FPORCH] = instNop | HoffVoff | fPorchTicks;
  syncDataActive[SYNC_LINE_HSYNC] = instNop | HonVoff | syncTicks;
  syncDataActive[SYNC_LINE_BPORCH] = instNop | HoffVoff | bPorchTicks;

  // sync data for a front or back porch scanline
  syncDataPorch[SYNC_LINE_ACTIVE] = instNop | HoffVoff | activeTicks;
  syncDataPorch[SYNC_LINE_FPORCH] = instNop | HoffVoff | fPorchTicks;
  syncDataPorch[SYNC_LINE_HSYNC] = instNop | HonVoff | syncTicks;
  syncDataPorch[SYNC_LINE_BPORCH] = instNop | HoffVoff | bPorchTicks;

  // sync data for a vsync scanline
  syncDataSync[SYNC_LINE_ACTIVE] = instNop | HoffVon | activeTicks;
  syncDataSync[SYNC_LINE_FPORCH] = instNop | HoffVon | fPorchTicks;
  syncDataSync[SYNC_LINE_HSYNC] = instNop | HonVon | syncTicks;
  syncDataSync[SYNC_LINE_BPORCH] = instNop | HoffVon | bPorchTicks;

  // Interlaced: full-line vsync buffers (4 words = 2 half-lines = 64us).
  // Pulse widths from halfLineSync config; guard period fills the remainder of each half-line.
  if (vgaParams.params.interlaced)
  {
    const uint32_t cLow  = HonVoff;   // csync asserted (low)
    const uint32_t cHigh = HoffVoff;  // csync idle (high)

    const float ppc = vgaParams.params.pioClocksPerPixel;
    const uint32_t shortPx = vgaParams.params.shortPulsePixels;  // EQ pulse (e.g. 27)
    const uint32_t halfLinePx = vgaParams.params.hSyncParams.totalPixels / 2; // half-line (e.g. 432)
    const uint32_t longPx  = halfLinePx - shortPx;                            // LS pulse / EQ guard
    const uint32_t eqLow  = roundflt(ppc * (float)shortPx) - vga_sync_SETUP_OVERHEAD;  // EQ pulse ticks
    const uint32_t eqHigh = roundflt(ppc * (float)longPx)  - vga_sync_SETUP_OVERHEAD;  // EQ guard / LS pulse ticks

    // LS+LS: [405px LOW][27px HIGH][405px LOW][27px HIGH] = 864px
    syncDataLsLs[0] = instNop | cLow  | eqHigh;
    syncDataLsLs[1] = instNop | cHigh | eqLow;
    syncDataLsLs[2] = instNop | cLow  | eqHigh;
    syncDataLsLs[3] = instNop | cHigh | eqLow;

    // LS+EQ: [405px LOW][27px HIGH][27px LOW][405px HIGH] = 864px
    syncDataLsEq[0] = instNop | cLow  | eqHigh;
    syncDataLsEq[1] = instNop | cHigh | eqLow;
    syncDataLsEq[2] = instNop | cLow  | eqLow;
    syncDataLsEq[3] = instNop | cHigh | eqHigh;

    // EQ+EQ: [27px LOW][405px HIGH][27px LOW][405px HIGH] = 864px
    syncDataEqEq[0] = instNop | cLow  | eqLow;
    syncDataEqEq[1] = instNop | cHigh | eqHigh;
    syncDataEqEq[2] = instNop | cLow  | eqLow;
    syncDataEqEq[3] = instNop | cHigh | eqHigh;

    // EQ+LS: [27px LOW][405px HIGH][405px LOW][27px HIGH] = 864px
    // This is the interlace transition line (F1 line 312).
    syncDataEqLs[0] = instNop | cLow  | eqLow;
    syncDataEqLs[1] = instNop | cHigh | eqHigh;
    syncDataEqLs[2] = instNop | cLow  | eqHigh;
    syncDataEqLs[3] = instNop | cHigh | eqLow;

    // Lookup table: VgaVsyncLineType enum → buffer pointer
    vsyncTypeBuffers[VSYNC_LSLS]  = syncDataLsLs;
    vsyncTypeBuffers[VSYNC_LSEQ]  = syncDataLsEq;
    vsyncTypeBuffers[VSYNC_EQEQ]  = syncDataEqEq;
    vsyncTypeBuffers[VSYNC_EQLS]  = syncDataEqLs;
    vsyncTypeBuffers[VSYNC_PORCH] = syncDataPorch;
  }

  return true;
}


/*
 * initialise the vga sync pio
 */
static void vgaInitSync()
{
  buildSyncData();

  // initalize sync pins for pio
  for (uint i = 0; i < SYNC_PINS_COUNT; ++i)
  {
    pio_gpio_init(VGA_PIO, SYNC_PINS_START + i);
  }

  // add sync pio program
  uint syncProgOffset = pio_add_program(VGA_PIO, &vga_sync_program);
  pio_sm_set_consecutive_pindirs(VGA_PIO, SYNC_SM, SYNC_PINS_START, SYNC_PINS_COUNT, true);

  // configure sync pio
  pio_sm_config syncConfig = vga_sync_program_get_default_config(syncProgOffset);
  sm_config_set_out_pins(&syncConfig, SYNC_PINS_START, 2);
  sm_config_set_clkdiv(&syncConfig, vgaParams.params.pioDivider);
  sm_config_set_out_shift(&syncConfig, true, true, 32); // R shift, autopull @ 32 bits
  sm_config_set_fifo_join(&syncConfig, PIO_FIFO_JOIN_TX); // Join FIFOs together to get an 8 entry TX FIFO
  pio_sm_init(VGA_PIO, SYNC_SM, syncProgOffset, &syncConfig);

  // initialise sync dma
  syncDmaChan = 0;//dma_claim_unused_channel(true);
  syncDmaChanMask = 0x01 << syncDmaChan;
  dma_channel_config syncDmaChanConfig = dma_channel_get_default_config(syncDmaChan);
  channel_config_set_transfer_data_size(&syncDmaChanConfig, DMA_SIZE_32);           // transfer 32 bits at a time
  channel_config_set_read_increment(&syncDmaChanConfig, true);                       // increment read
  channel_config_set_write_increment(&syncDmaChanConfig, false);                     // don't increment write 
  channel_config_set_dreq(&syncDmaChanConfig, pio_get_dreq(VGA_PIO, SYNC_SM, true)); // transfer when there's space in fifo

  // setup the dma channel and set it going
  // setup the dma channel and set it going
  uint32_t* syncInitBuf = vgaParams.params.interlaced ? syncDataLsLs : syncDataSync;
  dma_channel_configure(syncDmaChan, &syncDmaChanConfig, &VGA_PIO->txf[SYNC_SM], syncInitBuf, 4, false);
  dma_channel_set_irq0_enabled(syncDmaChan, true);
}


/*
 * initialise the vga sync pio
 */
static void vgaInitRgb()
{
  const uint32_t rgbCyclesPerPixel = roundflt(vgaParams.params.pioClocksPerScaledPixel);

  // copy the rgb program and set the appropriate pixel delay
  uint16_t rgbProgramInstr[vga_rgb_program.length];
  for (int i = 0; i < vga_rgb_program.length; ++i)
  {
    rgbProgramInstr[i] = vga_rgb_program.instructions[i];
  }
  rgbProgramInstr[vga_rgb_DELAY_INSTR] |= pio_encode_delay(rgbCyclesPerPixel - vga_rgb_LOOP_TICKS);

  pio_program_t rgbProgram = {
    .instructions = rgbProgramInstr,
    .length = vga_rgb_program.length,
    .origin = vga_rgb_program.origin
  };

  // initalize rgb pins for pio
  for (uint i = 0; i < RGB_PINS_COUNT; ++i)
  {
    pio_gpio_init(VGA_PIO, RGB_PINS_START + i);
  }

  // add rgb pio program
  pio_sm_set_consecutive_pindirs(VGA_PIO, RGB_SM, RGB_PINS_START, RGB_PINS_COUNT, true);
  // Output displayPixels + 2 pixels per line. The extra word (2 pixels) of
  // zeros absorbs the PIO's speculative autopull, preventing stale FIFO data
  // from bleeding into the next scanline. The extra pixels fall within the
  // front porch region and are blanked by the sync signal.
  pio_set_y(VGA_PIO, RGB_SM, vgaParams.params.hSyncParams.displayPixels + 1);

  rgbProgOffset = pio_add_program(VGA_PIO, &rgbProgram);
  rgbConfig = vga_rgb_program_get_default_config(rgbProgOffset);

  sm_config_set_out_pins(&rgbConfig, RGB_PINS_START, RGB_PINS_COUNT);
  sm_config_set_clkdiv(&rgbConfig, vgaParams.params.pioDivider);

  sm_config_set_out_shift(&rgbConfig, true, true, 32); // R shift, autopull @ 16 bits
  pio_sm_init(VGA_PIO, RGB_SM, rgbProgOffset, &rgbConfig);

  // initialise rgb dma
  rgbDmaChan = 1;//dma_claim_unused_channel(true);
  rgbDmaChanMask = 0x01 << rgbDmaChan;
  dma_channel_config rgbDmaChanConfig = dma_channel_get_default_config(rgbDmaChan);
  channel_config_set_transfer_data_size(&rgbDmaChanConfig, DMA_SIZE_32);  // transfer 32 bits at a time (2 pixels)
  channel_config_set_read_increment(&rgbDmaChanConfig, true);             // increment read
  channel_config_set_write_increment(&rgbDmaChanConfig, false);           // don't increment write
  channel_config_set_dreq(&rgbDmaChanConfig, pio_get_dreq(VGA_PIO, RGB_SM, true));

  // DMA transfers displayPixels/2 + 1 words (the extra word is a black guard)
  dma_channel_configure(rgbDmaChan, &rgbDmaChanConfig, &VGA_PIO->txf[RGB_SM], rgbDataBuffer[0], vgaParams.params.hSyncParams.displayPixels / 2 + 1, false);
  dma_channel_set_irq0_enabled(rgbDmaChan, true);
}

/*
 * dma interrupt handler
 *
 * Interlaced: all DMA transfers are 4 words (one full line = 64us).
 * Field structure is read from vgaParams.params.fields[currentField].
 * The vsync pattern, porch, active, and trailing EQ line counts are
 * all defined per-field in the VgaFieldParams.
 */
static void __isr __time_critical_func(dmaIrqHandler)(void)
{
  static int currentLine = -1;
  static int currentDisplayLine = -1;
  static int currentField = 0;

  if (dma_hw->ints0 & syncDmaChanMask)
  {
    dma_hw->ints0 = syncDmaChanMask;

    if (vgaParams.params.interlaced)
    {
      const VgaFieldParams* field = &vgaParams.params.fields[currentField];

      if (++currentLine >= (int)field->totalLines)
      {
        currentLine = 0;
        currentDisplayLine = 0;
        currentField ^= 1;
        field = &vgaParams.params.fields[currentField];
      }

      const int vsyncEnd  = field->vsyncLines;
      const int porchEnd  = vsyncEnd + field->porchLines;
      const int activeEnd = porchEnd + field->activeLines;

      if (currentLine < vsyncEnd)
      {
        // Vsync region: dispatch from per-field pattern
        dma_channel_set_read_addr(syncDmaChan,
          vsyncTypeBuffers[field->vsyncPattern[currentLine]], true);
      }
      else if (currentLine < porchEnd)
      {
        // Back porch
        dma_channel_set_read_addr(syncDmaChan, syncDataPorch, true);
        if (currentLine + 2 == porchEnd)
        {
          multicore_fifo_push_timeout_us((uint32_t)(currentField << 12) | 0, 0);
          multicore_fifo_push_timeout_us((uint32_t)(currentField << 12) | 1, 0);

          dma_channel_abort(rgbDmaChan);
          dma_hw->ints0 = rgbDmaChanMask;
          pio_sm_set_enabled(VGA_PIO, RGB_SM, false);
          pio_sm_clear_fifos(VGA_PIO, RGB_SM);
          pio_sm_restart(VGA_PIO, RGB_SM);
          pio_sm_exec(VGA_PIO, RGB_SM, pio_encode_jmp(rgbProgOffset));
          pio_sm_set_enabled(VGA_PIO, RGB_SM, true);
          currentDisplayLine = 0;
          dma_channel_set_read_addr(rgbDmaChan, rgbDataBuffer[0], true);
        }
      }
      else if (currentLine < activeEnd)
      {
        dma_channel_set_read_addr(syncDmaChan, syncDataActive, true);
      }
      else
      {
        // Trailing lines: dispatch from per-field trailing pattern
        int trailingIdx = currentLine - activeEnd;
        dma_channel_set_read_addr(syncDmaChan,
          vsyncTypeBuffers[field->trailingPattern[trailingIdx]], true);

        if (currentLine == activeEnd)
        {
          multicore_fifo_push_timeout_us(FRONT_PORCH_MSG, 0);
        }
      }
    }
    else
    {
      // non-interlaced VGA: original dispatch logic unchanged
      if (++currentLine >= vgaParams.params.vSyncParams.totalPixels)
      {
        currentLine = 0;
        currentDisplayLine = 0;
      }

      if (currentLine < vgaParams.params.vSyncParams.syncPixels)
      {
        dma_channel_set_read_addr(syncDmaChan, syncDataSync, true);
      }
      else if (currentLine < (vgaParams.params.vSyncParams.syncPixels + vgaParams.params.vSyncParams.backPorchPixels))
      {
        dma_channel_set_read_addr(syncDmaChan, syncDataPorch, true);
        if (currentLine + 2 == (vgaParams.params.vSyncParams.syncPixels + vgaParams.params.vSyncParams.backPorchPixels))
        {
          multicore_fifo_push_timeout_us(0, 0);
          multicore_fifo_push_timeout_us(1, 0);
        }
      }
      else if (currentLine < (vgaParams.params.vSyncParams.totalPixels - vgaParams.params.vSyncParams.frontPorchPixels))
      {
        dma_channel_set_read_addr(syncDmaChan, syncDataActive, true);
      }
      else
      {
        dma_channel_set_read_addr(syncDmaChan, syncDataPorch, true);
        if (currentLine == (vgaParams.params.vSyncParams.totalPixels - vgaParams.params.vSyncParams.frontPorchPixels) + 2)
        {
          multicore_fifo_push_timeout_us(FRONT_PORCH_MSG, 0);
        }
      }
    }
  }

  if (dma_hw->ints0 & rgbDmaChanMask)
  {
    dma_hw->ints0 = rgbDmaChanMask;

    currentDisplayLine++;

    uint32_t pxLine = currentDisplayLine;
    if (vgaParams.params.vPixelScale == 2) pxLine >>= 1;
    uint32_t pxLineRpt = currentDisplayLine & (vgaParams.params.vPixelScale - 1);

    uint32_t* currentBuffer = (uint32_t*)rgbDataBuffer[pxLine & 0x01];
    
    // crt effect?
    if (vgaParams.scanlines && pxLineRpt != 0)
    {
      for (int i = 0; i < 5; ++i)
      {
#if PICO_RP2040
        currentBuffer[i] >>= 1;
#else
        currentBuffer[i] = (currentBuffer[i] >> 1) & 0x07770777;
#endif
      }
    }
    dma_channel_set_read_addr(rgbDmaChan, currentBuffer, true);

#if PICO_RP2040
    pio_sm_set_pindirs_with_mask(VGA_PIO, RGB_SM, (vgaParams.scanlines && (currentDisplayLine & 0x01)) - 1, (1 << 5) | (1 << 9) | (1 << 13));
#endif

    // need a new line every X display lines
    if (pxLineRpt == 0)
    {
      uint32_t requestLine = pxLine + 1;
      if (requestLine < vgaParams.params.vVirtualPixels)
      {
        // bit 12 carries the current field for interlaced modes (0 or 1)
        multicore_fifo_push_timeout_us((currentField << 12) | requestLine, 0);
      }

      if (requestLine == vgaParams.params.vVirtualPixels - 1)
      {
        multicore_fifo_push_timeout_us(END_OF_FRAME_MSG, 0);
      }
    }
    else if (vgaParams.scanlines) // apply a lame CRT effect, darkening every 2nd scanline
    {
      int end = vgaParams.params.hSyncParams.displayPixels / 2;
      for (int i = 5; i < end; ++i)
      {
#if PICO_RP2040
        currentBuffer[i] >>= 1;
#else
        currentBuffer[i] = (currentBuffer[i] >> 1) & 0x07770777;
#endif
      }
    }
    if (pxLine == vgaParams.triggerScanline &&
        pxLineRpt == vgaParams.params.vPixelScale - 1)
    {
      multicore_fifo_push_timeout_us(END_OF_SCANLINE_MSG | pxLine, 0);
    }
  }
}

/*
 * initialise the pio dma
 */
static void initDma()
{
  irq_set_exclusive_handler(DMA_IRQ_0, dmaIrqHandler);
  irq_set_enabled(DMA_IRQ_0, true);

  dma_channel_start(syncDmaChan);
  dma_channel_start(rgbDmaChan);
}


/*
 * main vga loop
 */
void __time_critical_func(vgaLoop)()
{
  if (vgaParams.initFn)
  {
    vgaParams.initFn();
  }


  uint32_t frameNumber = 0;
  while (1)
  {
    uint32_t message = multicore_fifo_pop_blocking();

    if (message == FRONT_PORCH_MSG)
    {
      if (vgaParams.porchFn)
      {
        vgaParams.porchFn();
      }
    }
    else if (message == END_OF_FRAME_MSG)
    {
      if (vgaParams.endOfFrameFn)
      {
        vgaParams.endOfFrameFn(frameNumber);
        ++frameNumber;
      }
    }
    else if ((message & END_OF_SCANLINE_MSG) != 0)
    {
      if (vgaParams.endOfScanlineFn)
      {
        vgaParams.endOfScanlineFn(message & 0x0fff);
      }
    }
    else
    {
      bool doEof = false;
      if (message != 0)
      {
        while (multicore_fifo_rvalid()) // if we dropped a scanline, no point rendering it now
        {
          uint32_t nextMessage = sio_hw->fifo_rd;
          if (nextMessage == END_OF_FRAME_MSG)
          {
            doEof = true;
          }
          else
          {
            message = nextMessage;
          }
        }
      }

      // get the next scanline pixels
      // for interlaced modes, bit 12 of y carries the field number (0 or 1)
      vgaParams.scanlineFn(message & 0x1fff, &vgaParams.params,
                           rgbDataBuffer[message & 0x01]);
      if (doEof && vgaParams.endOfFrameFn)
      {
        vgaParams.endOfFrameFn(frameNumber);
        ++frameNumber;
      }

    }
  }
}

/*
 * initialise the vga
 */
void vgaInit(VgaInitParams params)
{
  vgaParams = params;

  vgaInitSync();
  vgaInitRgb();

  initDma();

  pio_sm_set_enabled(VGA_PIO, SYNC_SM, true);
  pio_sm_set_enabled(VGA_PIO, RGB_SM, true);
}

VgaInitParams *vgaCurrentParams()
{
  return &vgaParams;
}

void vgaSetTriggerScanline(uint32_t scanline)
{
  vgaParams.triggerScanline = scanline;
}