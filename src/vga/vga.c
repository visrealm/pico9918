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
#include "../../submodules/vrEmuTms9918/src/vrEmuTms9918.h"
#include "../../submodules/vrEmuTms9918/src/impl/vrEmuTms9918Priv.h"

 #define VGA_COMBINE_SYNC PICO9918_SCART_RGBS

#define VGA_NO_MALLOC 1

#define VGA_SCANLINE_CB_ENABLE 0

// avoid bringing in math.h
int roundflt(float x)
{
  if (x < 0.0f)
    return (int)(x - 0.5f);
  else
    return (int)(x + 0.5f);
}


#define SYNC_PINS_START 0        // first sync pin gpio number
#define SYNC_PINS_COUNT 2        // number of sync pins (h and v)

#define RGB_PINS_START  2        // first rgb pin gpio number
#define RGB_PINS_COUNT 12        // number of rgb pins

#define VGA_PIO         pio0_hw  // which pio are we using for vga?
#define SYNC_SM         0        // vga sync state machine index
#define RGB_SM          1        // vga rgb state machine index

#define FRONT_PORCH_MSG 0x20000000
#define END_OF_SCANLINE_MSG 0x40000000
#define END_OF_FRAME_MSG    0x80000000

#if VGA_COMBINE_SYNC
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
uint32_t __aligned(8) syncDataActive[4];  // active display area
uint32_t __aligned(8) syncDataPorch[4];   // vertical porch
uint32_t __aligned(8) syncDataSync[4];    // vertical sync

#if VGA_NO_MALLOC
__attribute__((section(".scratch_y.lookup"))) uint16_t __aligned(4) rgbDataBuffer[2][VIRTUAL_PIXELS_X] = { 0 };   // two scanline buffers (odd and even)
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
  rgbDataBuffer[0] = malloc(VIRTUAL_PIXELS_X * sizeof(uint16_t));
  rgbDataBuffer[1] = malloc(VIRTUAL_PIXELS_X * sizeof(uint16_t));
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

#if VGA_COMBINE_SYNC
  const uint32_t HoffVoff = hSyncOff | vSyncOff;
  const uint32_t HonVoff = hSyncOn | vSyncOn;
  const uint32_t HoffVon = hSyncOn | vSyncOn;
  const uint32_t HonVon = hSyncOff | vSyncOff;
#else
  const uint32_t HoffVoff = hSyncOff | vSyncOff;
  const uint32_t HonVoff = hSyncOn | vSyncOff;
  const uint32_t HoffVon = hSyncOff | vSyncOn;
  const uint32_t HonVon = hSyncOn | vSyncOn;
#endif

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
  dma_channel_configure(syncDmaChan, &syncDmaChanConfig, &VGA_PIO->txf[SYNC_SM], syncDataSync, 4, false);
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
    gpio_set_drive_strength(RGB_PINS_START + i, GPIO_DRIVE_STRENGTH_8MA); 
  }

  // add rgb pio program
  pio_sm_set_consecutive_pindirs(VGA_PIO, RGB_SM, RGB_PINS_START, RGB_PINS_COUNT, true);
  pio_set_y(VGA_PIO, RGB_SM, VIRTUAL_PIXELS_X - 1);

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

  // setup the dma channel and set it going
  dma_channel_configure(rgbDmaChan, &rgbDmaChanConfig, &VGA_PIO->txf[RGB_SM], rgbDataBuffer[0], VIRTUAL_PIXELS_X / 2, false);
  dma_channel_set_irq0_enabled(rgbDmaChan, true);
}

/*
 * dma interrupt handler
 */
static void __isr __time_critical_func(dmaIrqHandler)(void)
{
  static int currentTimingLine = -1;
  static int currentDisplayLine = -1;
  const bool bModeText80_8 = tmsMode(tms9918) == TMS_MODE_TEXT80_8;

  if (dma_hw->ints0 & syncDmaChanMask)
  {
    dma_hw->ints0 = syncDmaChanMask;

    if (++currentTimingLine >= vgaParams.params.vSyncParams.totalPixels)
    {
      currentTimingLine = 0;
      currentDisplayLine = 0;
    }

    if (currentTimingLine < vgaParams.params.vSyncParams.syncPixels)
    {
      dma_channel_set_read_addr(syncDmaChan, syncDataSync, true);
    }
    else if (currentTimingLine < (vgaParams.params.vSyncParams.syncPixels + vgaParams.params.vSyncParams.backPorchPixels))
    {
      dma_channel_set_read_addr(syncDmaChan, syncDataPorch, true);
      if (currentTimingLine + 2 == (vgaParams.params.vSyncParams.syncPixels + vgaParams.params.vSyncParams.backPorchPixels))
      {
        multicore_fifo_push_timeout_us(0, 0);
        multicore_fifo_push_timeout_us(1, 0);
      }
    }
    else if (currentTimingLine < (vgaParams.params.vSyncParams.totalPixels - vgaParams.params.vSyncParams.frontPorchPixels))
    {
      dma_channel_set_read_addr(syncDmaChan, syncDataActive, true);
    }
    else
    {
      dma_channel_set_read_addr(syncDmaChan, syncDataPorch, true);
      if (currentTimingLine == (vgaParams.params.vSyncParams.totalPixels - vgaParams.params.vSyncParams.frontPorchPixels) + 2)
      {
        multicore_fifo_push_timeout_us(FRONT_PORCH_MSG, 0);
      }
    }
#if VGA_SCANLINE_CB_ENABLE    
    multicore_fifo_push_timeout_us(END_OF_SCANLINE_MSG | currentTimingLine, 0);
#endif
  }

  if (dma_hw->ints0 & rgbDmaChanMask)
  {
    dma_hw->ints0 = rgbDmaChanMask;


    uint32_t pxLine = currentDisplayLine / DISPLAY_YSCALE;
    uint32_t pxLineRpt = currentDisplayLine & (DISPLAY_YSCALE - 1);
    if (bModeText80_8)
    {
      pxLine = currentDisplayLine;
      pxLineRpt = 0;
    }

    uint32_t* currentBuffer = (uint32_t*)rgbDataBuffer[pxLine & 0x01];
    currentDisplayLine++;

    // crt effect?
    if (vgaParams.scanlines && pxLineRpt != 0)
    {
      for (int i = 0; i < 5; ++i)
        currentBuffer[i] >>= 1;
    }
    dma_channel_set_read_addr(rgbDmaChan, currentBuffer, true);

    pio_sm_set_pindirs_with_mask(VGA_PIO, RGB_SM, (vgaParams.scanlines && pxLineRpt != 0) - 1, (1 << 5) | (1 << 9) | (1 << 13));

    // need a new line every X display lines
    if (pxLineRpt == 0)
    {
      uint32_t requestLine = pxLine + 1;
      const uint32_t maxLines = (!bModeText80_8 ? 1 : 2) * VIRTUAL_PIXELS_Y;
      if (requestLine >= maxLines)
        requestLine -= maxLines;

        multicore_fifo_push_timeout_us(requestLine, 0);

      if (requestLine == maxLines - 1)
      {
        multicore_fifo_push_timeout_us(END_OF_FRAME_MSG, 0);
      }
    }
    else if (vgaParams.scanlines) // apply a lame CRT effect, darkening every 2nd scanline
    {
      int end = VIRTUAL_PIXELS_X / 2;
      for (int i = 5; i < end; ++i)
        currentBuffer[i] >>= 1;
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
#if VGA_SCANLINE_CB_ENABLE    
    else if ((message & END_OF_SCANLINE_MSG) != 0)
    {
      if (vgaParams.endOfScanlineFn)
      {
        vgaParams.endOfScanlineFn();
      }
    }
#endif
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
      vgaParams.scanlineFn(message & 0xfff, &vgaParams.params, rgbDataBuffer[message & 0x01]);
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