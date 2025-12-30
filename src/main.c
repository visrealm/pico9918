/*
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 */

#include "vga.h"
#include "vga-modes.h"

#include "clocks.pio.h"

#define PALCONV 0

#if PALCONV
#include "palconv.pio.h"
#endif


#include "impl/vrEmuTms9918Priv.h"
#include "vrEmuTms9918Util.h"

#include "display.h"
#include "diag.h"
#include "gpio.h"
#include "gpu.h"
#include "config.h"
#include "splash.h"
#include "temperature.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/structs/ssi.h"



#define TMS_CRYSTAL_FREQ_HZ 10738635.0f

#define TMS_PIO pio1
#define CLOCK_PIO pio1

#define TMS_WRITE_IRQ PIO1_IRQ_0
#define TMS_READ_IRQ PIO1_IRQ_1

/* file globals */

static uint8_t nextValue = 0;     /* TMS9918A read-ahead value */
static bool currentInt = false;   /* current interrupt state */
static uint8_t currentStatus = 0x1f; /* current status register value */

static __attribute__((section(".scratch_y.buffer"))) uint32_t bg; 

static __attribute__((section(".scratch_x.buffer"))) uint8_t __aligned(4) tmsScanlineBuffer[TMS9918_PIXELS_X + 8];

const uint tmsWriteSm = 0;
const uint tmsReadSm = 1;
const uint tmsGromClkSm = 2;
const uint tmsCpuClkSm = 3;

#if PALCONV
#define PAL_PIO         pio0  // which pio are we using for vga?
const uint palconvSm = 2;
static const uint32_t dmapalOut = 5; // palette dma
static const uint32_t dmapalIn  = 6; // palette dma
#endif

#define SHOW_DIAGNOSTICS_FRAMES 600

static int frameCount = 0;
static bool validWrites = false;  // has the VDP display been enabled at all?
static bool doneInt = false;      // interrupt raised this frame?

static bool droppedFrames[16] = {0};
int droppedFramesCount = 0;

#define R0_DOUBLE_ROWS 0x08

static const uint32_t dma32 = 2;  // memset 32bit
/*
 * update the value send to the read PIO
 */
static void updateTmsReadAhead()
{
  uint32_t readAhead = 0xff;              // pin direction
  readAhead |= nextValue << 8;
  if (tms9918->isUnlocked)
  {
    int vr = TMS_REGISTER(tms9918, 0x0F) & 0x0F;
    readAhead |= (TMS_STATUS(tms9918, vr)) << 16;
    readAhead |= vr << 24;
  }
  else
  {
    readAhead |= (TMS_STATUS(tms9918, 0)) << 16;
  }
  pio_sm_put(TMS_PIO, tmsReadSm, readAhead);
}

/*
 * handle read interrupts from the TMS9918<->CPU interface
 */
void __not_in_flash_func(tmsReadIrqHandler)()
{
  uint32_t readVal = TMS_PIO->rxf[tmsReadSm];

  if ((readVal & 0x01) == 0) // read data
  {
    nextValue = vrEmuTms9918ReadAheadDataImpl();
  }
  else // read status
  {
    readVal >>= (1 + 16);        // Extract status that was actually read
    int readReg = (readVal >> 8); // What status register was read?
    tms9918->regWriteStage = 0;
    
    // Standard mode or F18A status register 0
    if (!tms9918->isUnlocked || readReg == 0)
    {
      readVal &= (STATUS_INT | STATUS_5S | STATUS_COL);
      currentStatus &= ~readVal; // Clear only the flags that were set
      if (readVal & STATUS_5S)   // Was 5th Sprite flag set?
        currentStatus |= 0x1f;   // Set sprite number to 31
      vrEmuTms9918SetStatusImpl(currentStatus);
      if (readVal & STATUS_INT)  // Was Interrupt flag set?
      {
        currentInt = false;
        gpio_put(GPIO_INT, !currentInt);
      }
    }
    else if (readReg == 1)
    {
      // F18A status register 1
      if (readVal & 0x01)
        TMS_STATUS(tms9918, 0x01) &= ~0x01;
    }
  }

  updateTmsReadAhead();
}

/*
 * handle write interrupts from the TMS9918<->CPU interface
 */
void __not_in_flash_func(tmsWriteIrqHandler)()
{
  uint32_t writeVal = TMS_PIO->rxf[tmsWriteSm];
  uint8_t dataVal = writeVal & 0xff;
  writeVal >>= ((GPIO_MODE - GPIO_CD7) + 16);

  if (writeVal & 0x01) // write reg/addr
  {
    vrEmuTms9918WriteAddrImpl(dataVal);
    
    bool newInt = vrEmuTms9918InterruptStatusImpl();
    if (newInt != currentInt)
    {
      currentInt = newInt;
      gpio_put(GPIO_INT, !currentInt);
    }
  }
  else // write data
  {
    vrEmuTms9918WriteDataImpl(dataVal);
  }

  nextValue = vrEmuTms9918ReadDataNoIncImpl();
  updateTmsReadAhead();
}


/*
 * enable gpio interrupts inline
 */
static inline void enableTmsPioInterrupts()
{
  __dmb();
  irq_set_enabled(TMS_WRITE_IRQ, true);
  irq_set_enabled(TMS_READ_IRQ, true);
}

/*
 * disable gpio interrupts inline
 */
static inline void disableTmsPioInterrupts()
{
  irq_set_enabled(TMS_WRITE_IRQ, false);
  irq_set_enabled(TMS_READ_IRQ, false);
  __dmb();
}

/*
 * handle reset pin going active (low)
 */
void __not_in_flash_func(gpioIrqHandler)()
{
  gpio_acknowledge_irq(GPIO_RESET, GPIO_IRQ_EDGE_FALL);

  disableTmsPioInterrupts();

  vrEmuTms9918Reset();  // resets palette to factory

  readConfig(tms9918->config);  // re-load config palette

  irq_clear(TMS_WRITE_IRQ);
  irq_clear(TMS_READ_IRQ);
  pio_sm_clear_fifos(TMS_PIO, tmsReadSm);
  pio_sm_clear_fifos(TMS_PIO, tmsWriteSm);

  nextValue = 0;
  currentStatus = 0x1f;
  vrEmuTms9918SetStatusImpl(currentStatus);
  currentInt = false;
  doneInt = true;
  updateTmsReadAhead();  
  
  frameCount = 0;
  resetSplash();
  gpio_put(GPIO_INT, !currentInt);
  enableTmsPioInterrupts();
}

typedef struct
{
  int pll;
  int pllDiv1;
  int pllDiv2;
  int voltage;
  int clockHz;
} ClockSettings;

#define CLOCK_PRESET(PLL,PD1,PD2,VOL) {PLL, PD1, PD2, VOL, PLL / PD1 / PD2}

static const ClockSettings clockPresets[] = {
  CLOCK_PRESET(1512000000, 6, 1, VREG_VOLTAGE_1_15),    // 252
  CLOCK_PRESET(1512000000, 5, 1, VREG_VOLTAGE_1_20),    // 302.4
  CLOCK_PRESET(1056000000, 3, 1, VREG_VOLTAGE_1_30)     // 352
};

static int clockPresetIndex = 0;
static bool testingClock = false;

static void eofInterrupt()
{
  doneInt = true;
  TMS_STATUS(tms9918, 0x01) |=  0x02;
  if (TMS_REGISTER(tms9918, 0x32) & 0x20)
  {
    gpuTrigger();
  }

  static float tempC = 0.0f;
  tempC += coreTemperatureC();
  if ((frameCount & 0x3f) == 0) // every 16th frame
  {
    tempC /= 64.0f;
    diagSetTemperature(tempC);
    uint8_t t4 = (uint8_t)(tempC * 4.0f + 0.5f);
    TMS_STATUS(tms9918, 13) = t4;
    tempC = 0.0f;
  }

  if (tms9918->configDirty)
  {
    tms9918->configDirty = false;
    applyConfig();  // apply config option to device now
    diagnosticsConfigUpdated();
  }
}

static void updateInterrupts(uint8_t tempStatus)
{
  disableTmsPioInterrupts();
  if ((currentStatus & STATUS_INT) == 0)
  {
    currentStatus = (currentStatus & 0xe0) | tempStatus;

    vrEmuTms9918SetStatusImpl(currentStatus);
    updateTmsReadAhead();  // Update read-ahead before changing pin state

    currentInt = vrEmuTms9918InterruptStatusImpl();
    gpio_put(GPIO_INT, !currentInt);
  }
  else
  {
    // Status already has INT set, but ensure pin state is correct
    // (in case R1 was modified to disable interrupts)
    bool shouldInt = vrEmuTms9918InterruptStatusImpl();
    if (shouldInt != currentInt)
    {
      currentInt = shouldInt;
      gpio_put(GPIO_INT, !currentInt);
    }
  }
  enableTmsPioInterrupts();
}


/* F18A palette entries are big-endian 0x0RGB which looks like
   0xGB0R to our RP2040. our vga code is expecting 0x0BGR
   so we've got B and R correct by pure chance. just need to shift G
   over. this function does that.   */

inline uint32_t bigRgb2LittleBgr(uint32_t val)
{
  val &= 0xff0f;
  return val | ((val >> 12) << 4);
}


void updateClock(uint pioSm, float freqHz)
{
  float clockDiv = ((float)clockPresets[clockPresetIndex].clockHz) / (freqHz * 2.0f);
  pio_sm_set_clkdiv(CLOCK_PIO, pioSm, clockDiv);
  pio_sm_set_enabled(CLOCK_PIO, pioSm, true);
  diagSetClockHz(clockPresets[clockPresetIndex].clockHz);
}

/*
 * initialise a clock output using PIO
 */
void initClock(uint gpio, uint pioSm, float freqHz)
{
  static uint clocksPioOffset = -1;

  if (clocksPioOffset == -1)
  {
    clocksPioOffset = pio_add_program(CLOCK_PIO, &clock_program);
  }

  pio_gpio_init(CLOCK_PIO, gpio);
  pio_sm_set_consecutive_pindirs(CLOCK_PIO, pioSm, gpio, 1, true);
  pio_sm_config c = clock_program_get_default_config(clocksPioOffset);
  sm_config_set_set_pins(&c, gpio, 1);

  pio_sm_init(CLOCK_PIO, pioSm, clocksPioOffset, &c);

  updateClock(pioSm, freqHz);
}

static void tmsPorch()
{
  tms9918->vram.map.blanking = 1; // V
  tms9918->vram.map.scanline = 255; // F18A value for vsync
  TMS_STATUS(tms9918, 0x03) = 255;
}

static void tmsEndOfFrame(uint32_t frameNumber)
{
  ++frameCount;
  
  if (!validWrites)
  {
    // has the display been enabled?
    if (validWrites = (TMS_REGISTER(tms9918, 1) & 0x40))
    {
      allowSplashHide();
      if (frameCount > SHOW_DIAGNOSTICS_FRAMES)
      {
        // reset diganostics and other settings back to defaults
        readConfig(tms9918->config);
      }
    }
  }

  if (tms9918->config[CONF_DIAG])
    updateDiagnostics(frameCount);


  // here, we catch the case where the last row(s) were
  // missed and we never raised an interrupt. do it now  
  if (!doneInt)
  {
    eofInterrupt();
    updateInterrupts(STATUS_INT);
  }

#if DISPLAY_YSCALE > 1
  vgaCurrentParams()->params.vPixelScale = DISPLAY_YSCALE - (bool)(TMS_REGISTER(tms9918, 0) & R0_DOUBLE_ROWS);
  vgaCurrentParams()->params.vVirtualPixels = VIRTUAL_PIXELS_Y << (bool)(TMS_REGISTER(tms9918, 0) & R0_DOUBLE_ROWS);
#endif
}


void renderDiag(int y, uint16_t *pixels)
{
  if (tms9918->config[CONF_DIAG])
  {
    dma_channel_wait_for_finish_blocking(dma32);
    renderDiagnostics(y, pixels);
  }
}

/*
 * cache color lookup from color index to BGR16
 */
static __attribute__((section(".scratch_y.buffer"))) 
uint32_t __aligned(4) pram [64];


static __attribute__((noinline))  void generateRgbCache()
{
  /* convert from  palette to bgr12 */
  uint32_t data;
  tms9918->palDirty = 0;

#if PALCONV
  dma_channel_set_read_addr(dmapalOut, tms9918->vram.map.pram, true);
  dma_channel_set_write_addr(dmapalIn, pram, true);
#else
  for (int i = 0; i < 64; i += 8)
  {
    data = tms9918->vram.map.pram [i + 0] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 0] = data | (data << 16);
    data = tms9918->vram.map.pram [i + 1] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 1] = data | (data << 16);
    data = tms9918->vram.map.pram [i + 2] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 2] = data | (data << 16);
    data = tms9918->vram.map.pram [i + 3] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 3] = data | (data << 16);
    data = tms9918->vram.map.pram [i + 4] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 4] = data | (data << 16);
    data = tms9918->vram.map.pram [i + 5] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 5] = data | (data << 16);
    data = tms9918->vram.map.pram [i + 6] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 6] = data | (data << 16);
    data = tms9918->vram.map.pram [i + 7] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 7] = data | (data << 16);
  }
#endif
}

/*
 * generate a single VGA scanline (called by vgaLoop(), runs on proc1)
 */
static void __time_critical_func(tmsScanline)(uint16_t y, VgaParams* params, uint16_t* pixels)
{
  int vPixels = (TMS_REGISTER(tms9918, 0x31) & 0x40) ? 30 * 8 : 24 * 8;
  if (TMS_REGISTER(tms9918, 0) & R0_DOUBLE_ROWS)
    vPixels <<= 1;

  const uint32_t vBorder = (vgaCurrentParams()->params.vVirtualPixels - vPixels) / 2;
  const bool pixelsDoubled = vrEmuTms9918DisplayMode(tms9918) != TMS_MODE_TEXT80;

  const uint32_t halfHBorder = (VIRTUAL_PIXELS_X - TMS9918_PIXELS_X * 2) / 4;

  uint32_t* dPixels = (uint32_t*)pixels;
  bg = pram[vrEmuTms9918RegValue(TMS_REG_FG_BG_COLOR) & 0x0f];

  if (y == 0)
  {
    doneInt = false;
  }

  dma_channel_wait_for_finish_blocking(dma32);

  /*** top and bottom borders ***/
  if (y < vBorder || y >= (vBorder + vPixels))  // TODO: Note of this runs in ROW30 mode
  {
    dma_channel_set_write_addr(dma32, dPixels, false);
    dma_channel_set_trans_count(dma32, VIRTUAL_PIXELS_X / 2, true);
    tms9918->vram.map.blanking = 1; // V
    if ((y >= vBorder + vPixels))
    {
      tms9918->vram.map.scanline = y - vBorder;
      TMS_STATUS(tms9918, 0x03) = tms9918->vram.map.scanline;
    }

    if (!validWrites || (frameCount < 600))
    {
      dma_channel_wait_for_finish_blocking(dma32);

      outputSplash(y, frameCount, vBorder, vPixels, pixels);

      if (testingClock)
      {
        if (frameCount < 400)
        {
          renderText(y, "TESTING NEW CLOCK FREQUENCY...", 231, 8, 0x0fff, 0x0222, pixels);
        }
        else if (frameCount < 500)
        {
          renderText(y, "TEST COMPLETED SUCCESSFULLY!", 234, 8, 0x07f7, 0x0444, pixels);
        }
        else if (frameCount < 599)
        {
          renderText(y, "WRITING CONFIGURATION TO FLASH...", 222, 8, 0x0fff, 0x0444, pixels);
        }
        else
        {
          // new clock lasted 10 seconds... let's accept it
          tms9918->config[CONF_CLOCK_TESTED] = tms9918->config[CONF_CLOCK_PRESET_ID];
          tms9918->config[CONF_SAVE_TO_FLASH] = 1;
          testingClock = false;
        }
      }
      
      if (frameCount > SHOW_DIAGNOSTICS_FRAMES)
      {
        tms9918->config[CONF_DIAG] = true;
        tms9918->config[CONF_DIAG_REGISTERS] = true;
        tms9918->config[CONF_DIAG_PERFORMANCE] = true;
        tms9918->config[CONF_DIAG_PALETTE] = true;
        tms9918->config[CONF_DIAG_ADDRESS] = true;
      }
    }

    if (TMS_REGISTER(tms9918, 0x32) & 0x40)
    {
      gpuTrigger();
    }

    y -= vBorder;
  }
  else
  {
    uint32_t frameStart = time_us_32();

    y -= vBorder;
    tms9918->vram.map.blanking = 0;
    tms9918->vram.map.scanline = y;
    TMS_STATUS(tms9918, 0x03) = y;

    /*** left border ***/
    dma_channel_set_write_addr(dma32, dPixels, false);
    dma_channel_set_trans_count(dma32, halfHBorder, true);

    /*** main display region ***/
    if (tms9918->palDirty || (TMS_STATUS(tms9918, 2) & 0x80))
      generateRgbCache();

    /* generate the scanline */
    uint32_t renderTime  = time_us_32();
    uint8_t tempStatus = vrEmuTms9918ScanLine(y, tmsScanlineBuffer);
    renderTime = time_us_32() - renderTime;

    /*** interrupt signal? ***/
    const int interruptThreshold = 1;
    if (y < vPixels - interruptThreshold)
    {
      TMS_STATUS(tms9918, 0x01) &= ~0x03;
    }
    else
    {
      TMS_STATUS(tms9918, 0x01) &= ~0x01;
    }

    if (tms9918->vram.map.scanline && (TMS_REGISTER(tms9918, 0x13) == tms9918->vram.map.scanline))
    {
      TMS_STATUS(tms9918, 0x01) |= 0x01;
      tempStatus |= STATUS_INT;
    }

    if (TMS_REGISTER(tms9918, 0x32) & 0x40)
    {
      gpuTrigger();
    }

    if (!doneInt && y >= vPixels - interruptThreshold)
    {
      eofInterrupt();
      tempStatus |= STATUS_INT;

      // keep track of the number of dropped frames in the past 64 frames
      // NOTE: this is unrelated to the PICO9918 - the same would occur on a 
      // standard VDP when an interrupt is missed
      bool droppedFrame = currentStatus & STATUS_INT;
      droppedFramesCount += droppedFrame - droppedFrames[frameCount & 0xf];
      droppedFrames[frameCount & 0xf] = droppedFrame;
    }

    updateInterrupts(tempStatus);

    dma_channel_wait_for_finish_blocking(dma32);

    uint8_t* src = tmsScanlineBuffer;
    uint8_t* end = tmsScanlineBuffer + TMS9918_PIXELS_X;
    uint32_t* dP = (uint32_t*)(pixels) + halfHBorder;

    tms9918->vram.map.blanking = 1; // H

    if (pixelsDoubled)
    {
      // convert all pixel data from color index to BGR16
      while (src < end)
      {
        dP [0] = pram[src[0]];
        dP [1] = pram[src[1]];
        dP [2] = pram[src[2]];
        dP [3] = pram[src[3]];
        dP [4] = pram[src[4]];
        dP [5] = pram[src[5]];
        dP [6] = pram[src[6]];
        dP [7] = pram[src[7]];
        dP += 8;
        src += 8;
      }
    }
    else
    {
      while (src < end)
      {
        register char p80;
        p80 = src[0]; dP [0] = (pram[p80 & 0xf] << 16) | (pram[p80 >> 4] & 0xffff);
        p80 = src[1]; dP [1] = (pram[p80 & 0xf] << 16) | (pram[p80 >> 4] & 0xffff);
        p80 = src[2]; dP [2] = (pram[p80 & 0xf] << 16) | (pram[p80 >> 4] & 0xffff);
        p80 = src[3]; dP [3] = (pram[p80 & 0xf] << 16) | (pram[p80 >> 4] & 0xffff);
        p80 = src[4]; dP [4] = (pram[p80 & 0xf] << 16) | (pram[p80 >> 4] & 0xffff);
        p80 = src[5]; dP [5] = (pram[p80 & 0xf] << 16) | (pram[p80 >> 4] & 0xffff);
        p80 = src[6]; dP [6] = (pram[p80 & 0xf] << 16) | (pram[p80 >> 4] & 0xffff);
        p80 = src[7]; dP [7] = (pram[p80 & 0xf] << 16) | (pram[p80 >> 4] & 0xffff);
        dP += 8;
        src += 8;
      }
    }

    // right border
    dma_channel_set_write_addr(dma32, dPixels + halfHBorder + TMS9918_PIXELS_X, true);    

    if (tms9918->config[CONF_DIAG_PERFORMANCE] || 1)
      updateRenderTime(renderTime,  time_us_32() - frameStart);    
  }

  renderDiag(y + vBorder, pixels);
}

/*
 * Set up PIOs for TMS9918 <-> CPU interface
 */
void tmsPioInit()
{
  // Set up separate interrupt handlers for read and write
  irq_set_exclusive_handler(TMS_WRITE_IRQ, tmsWriteIrqHandler);
  irq_set_enabled(TMS_WRITE_IRQ, true);
  
  irq_set_exclusive_handler(TMS_READ_IRQ, tmsReadIrqHandler);
  irq_set_enabled(TMS_READ_IRQ, true);

  uint tmsWriteProgram = pio_add_program(TMS_PIO, &tmsWrite_program);

  pio_sm_config writePioConfig = tmsWrite_program_get_default_config(tmsWriteProgram);
  sm_config_set_in_pins(&writePioConfig, GPIO_CD7);
  sm_config_set_in_shift(&writePioConfig, false, true, 32); // L shift, autopush @ 32 bits
  sm_config_set_jmp_pin(&writePioConfig, GPIO_CSW);
  sm_config_set_clkdiv(&writePioConfig, 1.0f);

  pio_sm_init(TMS_PIO, tmsWriteSm, tmsWriteProgram, &writePioConfig);
  pio_sm_set_enabled(TMS_PIO, tmsWriteSm, true);
  pio_set_irq0_source_enabled(TMS_PIO, pis_sm0_rx_fifo_not_empty, true);

  uint tmsReadProgram = pio_add_program(TMS_PIO, &tmsRead_program);

  for (uint i = 0; i < 8; ++i)
  {
    pio_gpio_init(TMS_PIO, GPIO_CD7 + i);
  }

  pio_sm_config readPioConfig = tmsRead_program_get_default_config(tmsReadProgram);
  sm_config_set_jmp_pin(&readPioConfig, GPIO_CSR);
  sm_config_set_in_pins(&readPioConfig, GPIO_MODE);
  sm_config_set_out_pins(&readPioConfig, GPIO_CD7, 8);
  sm_config_set_in_shift(&readPioConfig, false, false, 32); // L shift
  sm_config_set_out_shift(&readPioConfig, true, false, 32); // R shift
  sm_config_set_clkdiv(&readPioConfig, 1.0f);

  pio_sm_init(TMS_PIO, tmsReadSm, tmsReadProgram, &readPioConfig);
  pio_sm_set_enabled(TMS_PIO, tmsReadSm, true);
  pio_set_irq1_source_enabled(TMS_PIO, pis_sm1_rx_fifo_not_empty, true);

  pio_sm_put(TMS_PIO, tmsReadSm, 0x000000ff);
}


/*
 * 2nd CPU core (proc1) entry
 */
void proc1Entry()
{
  // set up gpio pins
  gpio_init_mask(GPIO_CD_MASK | GPIO_CSR_MASK | GPIO_CSW_MASK | GPIO_MODE_MASK | GPIO_MODE1_MASK | GPIO_INT_MASK | GPIO_RESET_MASK);
  gpio_put_all(0); // we want to kep /INT held low for now
  gpio_set_dir_all_bits(GPIO_INT_MASK); // /INT is an output

  tmsPioInit();

  gpio_put_all(GPIO_INT_MASK);	// ok, we can release /INT now
  
  Pico9918HardwareVersion hwVersion = currentHwVersion();

  if (hwVersion != HWVer_0_3)
  {
    // set up reset gpio interrupt handler
    irq_set_exclusive_handler(IO_IRQ_BANK0, gpioIrqHandler);
    gpio_set_irq_enabled(GPIO_RESET, GPIO_IRQ_EDGE_FALL, true);
    irq_set_enabled(IO_IRQ_BANK0, true);
  }

  tms9918->config[CONF_HW_VERSION] = hwVersion;

  // wait until everything else is ready, then run the vga loop
  multicore_fifo_pop_blocking();
  vgaLoop();
}

/*
 * main entry point
 */
int main(void)
{
  /* currently, VGA hard-coded to 640x480@60Hz. We want a high clock frequency
   * that comes close to being divisible by 25.175MHz. 252.0 is close... enough :)
   * I do have code which sets the best clock baased on the chosen VGA mode,
   * but this'll do for now. */

  /* the initial "safe" clock speed */
  ClockSettings clockSettings = clockPresets[clockPresetIndex];
  vreg_set_voltage(clockSettings.voltage);
  sleep_ms(2);
  set_sys_clock_pll(clockSettings.pll, clockSettings.pllDiv1, clockSettings.pllDiv2);

  /* we need one of these. it's the main guy */
  vrEmuTms9918Init();

  /* launch core 1 which handles TMS9918<->CPU and rendering scanlines */
  multicore_launch_core1(proc1Entry);

  /* we could set clock freq here from options */
  readConfig(tms9918->config);
  
  /*
   * if we're trying out a new clock rate, we need to have a failsafe 
   * we test it first 
   */
  if (tms9918->config[CONF_CLOCK_PRESET_ID] != 0 && 
      tms9918->config[CONF_CLOCK_PRESET_ID] != tms9918->config[CONF_CLOCK_TESTED])
  {
    testingClock = true;
    int wantedClock = tms9918->config[CONF_CLOCK_PRESET_ID];
    tms9918->config[CONF_CLOCK_PRESET_ID] = 0;
    tms9918->config[CONF_CLOCK_TESTED] = 0;
    writeConfig(tms9918->config);
    tms9918->config[CONF_CLOCK_PRESET_ID] = wantedClock;
  }

  if (tms9918->config[CONF_CLOCK_PRESET_ID] != clockPresetIndex)
  {
    clockPresetIndex = tms9918->config[CONF_CLOCK_PRESET_ID];
    clockSettings = clockPresets[clockPresetIndex];

    vreg_set_voltage(clockSettings.voltage);
    sleep_ms(1);
    set_sys_clock_pll(clockSettings.pll, clockSettings.pllDiv1, clockSettings.pllDiv2);
  }

  Pico9918HardwareVersion hwVersion = currentHwVersion();
  initClock((hwVersion == HWVer_0_3) ? GPIO_GROMCL_V03 : GPIO_GROMCL, tmsGromClkSm, TMS_CRYSTAL_FREQ_HZ / 24.0f);
  initClock((hwVersion == HWVer_0_3) ? GPIO_CPUCL_V03 : GPIO_CPUCL, tmsCpuClkSm, TMS_CRYSTAL_FREQ_HZ / 3.0f);

  dma_channel_config cfg = dma_channel_get_default_config(dma32);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, true);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
  dma_channel_set_config(dma32, &cfg, false);
  dma_channel_set_read_addr(dma32, &bg, false);

#if PALCONV
  uint palConvProgram = pio_add_program(PAL_PIO, &palconv_program);

  pio_sm_config palconvPioConfig = palconv_program_get_default_config(palConvProgram);

  sm_config_set_in_shift(&palconvPioConfig, false, true, 24);  // L shift
  sm_config_set_out_shift(&palconvPioConfig, true, false, 12);  // R shift
  pio_sm_init(PAL_PIO, palconvSm, palConvProgram, &palconvPioConfig);

  cfg = dma_channel_get_default_config(dmapalOut);
  channel_config_set_read_increment(&cfg, true);
  channel_config_set_write_increment(&cfg, false);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
  channel_config_set_dreq(&cfg, pio_get_dreq(PAL_PIO, palconvSm, true)); 
  dma_channel_set_read_addr(dmapalOut, tms9918->vram.map.pram, false);
  dma_channel_set_write_addr(dmapalOut, &PAL_PIO->txf[palconvSm], false);
  dma_channel_set_trans_count(dmapalOut, 64, false);
  dma_channel_set_config(dmapalOut, &cfg, false);

  cfg = dma_channel_get_default_config(dmapalIn);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, true);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
  channel_config_set_dreq(&cfg, pio_get_dreq(PAL_PIO, palconvSm, false)); 

  dma_channel_set_read_addr(dmapalIn, &PAL_PIO->rxf[palconvSm], false);
  dma_channel_set_write_addr(dmapalIn, pram, false);
  dma_channel_set_trans_count(dmapalIn, 64, false);
  dma_channel_set_config(dmapalIn, &cfg, false);

  pio_sm_set_enabled(PAL_PIO, palconvSm, true); 
#endif

  tms9918->palDirty = 1;

  /* then set up VGA output */
  VgaInitParams params = { 0 };
  params.params = vgaGetParams(DISPLAY_MODE);
  setVgaParamsScaleY(&params.params, 1);

  /* set vga scanline callback to generate tms9918 scanlines */
  params.scanlineFn = tmsScanline;
  params.endOfFrameFn = tmsEndOfFrame;
  params.porchFn = tmsPorch;

  const char *version = PICO9918_VERSION;

  vgaInit(params);

  initTemperature();

  initDiagnostics();

  /* signal proc1 that we're ready to start the display */
  multicore_fifo_push_blocking(0);

  /* initialize the GPU */
  gpuInit();

  /* pass control of core0 over to the TMS9900 GPU */
  gpuLoop();

  return 0;
}
