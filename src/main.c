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
#define TMS_IRQ PIO1_IRQ_0

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

static int frameCount = 0;
static bool validWrites = false;  // has the VDP display been enabled at all?
static bool doneInt = false;      // interrupt raised this frame?

static const uint32_t dma32 = 2; // memset 32bit
/*
 * update the value send to the read PIO
 */
inline static void updateTmsReadAhead()
{
  uint32_t readAhead = 0xff;              // pin direction
  readAhead |= nextValue << 8;
  readAhead |= (TMS_STATUS(tms9918, TMS_REGISTER(tms9918, 0x0F) & 0x0F)) << 16;
  pio_sm_put(TMS_PIO, tmsReadSm, readAhead);
}

/*
 * handle interrupts from the TMS9918<->CPU interface
 */
void  __not_in_flash_func(pio_irq_handler)()
{

  if ((TMS_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + tmsWriteSm))) == 0) // write?
  {
    uint32_t writeVal = TMS_PIO->rxf[tmsWriteSm];

    if (writeVal & (GPIO_MODE_MASK >> GPIO_CD7)) // write reg/addr
    {
      vrEmuTms9918WriteAddrImpl(writeVal & 0xff);
      currentInt = vrEmuTms9918InterruptStatusImpl();
      gpio_put(GPIO_INT, !currentInt);
    }
    else // write data
    {
      vrEmuTms9918WriteDataImpl(writeVal & 0xff);
    }

    nextValue = vrEmuTms9918ReadDataNoIncImpl();
    updateTmsReadAhead();
  }
  else if ((TMS_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + tmsReadSm))) == 0) // read?
  {
    uint32_t readVal = TMS_PIO->rxf[tmsReadSm];

    if ((readVal & 0x04) == 0) // read data
    {
      vrEmuTms9918ReadDataImpl();
      nextValue = vrEmuTms9918ReadDataNoIncImpl();
    }
    else // read status
    {
      readVal >>= (3 + 16); // What status was read?
      tms9918->regWriteStage = 0;
      switch (TMS_REGISTER(tms9918, 0x0F) & 0x0F)
      {
        case 0:
          readVal &= (STATUS_INT | STATUS_5S | STATUS_COL);
          currentStatus &= ~readVal; // Switch off any 3 high bits which have just been read
          if (readVal & STATUS_5S) // Was 5th Sprite read?
            currentStatus |= 0x1f;
          vrEmuTms9918SetStatusImpl(currentStatus);
          if (readVal & STATUS_INT)  // Was Interrupt read?
          {
            currentInt = false;
            gpio_put(GPIO_INT, !currentInt);
          }
          break;
        case 1:
          if (readVal << 31)
            TMS_STATUS(tms9918, 0x01) &= ~0x01;
          break;
      }
    }
    updateTmsReadAhead();
  }
}


/*
 * enable gpio interrupts inline
 */
static inline void enableTmsPioInterrupts()
{
  irq_set_enabled(TMS_IRQ, true);
}

/*
 * disable gpio interrupts inline
 */
static inline void disableTmsPioInterrupts()
{
  irq_set_enabled(TMS_IRQ, false);
}

/*
 * handle reset pin going active (low)
 */
void __not_in_flash_func(gpioIrqHandler)()
{
  gpio_acknowledge_irq(GPIO_RESET, GPIO_IRQ_EDGE_FALL);

  disableTmsPioInterrupts();

  readConfig(tms9918->config);

  vrEmuTms9918Reset();

  irq_clear(TMS_IRQ);
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
  CLOCK_PRESET(1260000000, 5, 1, VREG_VOLTAGE_1_15),    // 252
  CLOCK_PRESET(1512000000, 5, 1, VREG_VOLTAGE_1_15),    // 302.4
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

  if ((frameCount & 0x0f) == 0) // every 16th frame
  {
    float tempC = coreTemperatureC();
    uint32_t t = (int)(tempC * 10.0f);
    uint32_t i = (t / 100);
    t -= (i * 100);
    diagSetTemperatureBcd((i << 12) | ((t / 10) << 8) | (0x0e << 4) | (t % 10));
    uint8_t t4 = (uint8_t)(tempC * 4.0f + 0.5f);
    TMS_STATUS(tms9918, 13) = t4;
  }

  if (tms9918->configDirty)
  {
    tms9918->configDirty = false;
    applyConfig();  // apply config option to device now
  }

  // request to save config? let's do that
  if (tms9918->config[CONF_SAVE_TO_FLASH])
  {
    tms9918->config[CONF_SAVE_TO_FLASH] = 0;
    writeConfig(tms9918->config);
  }
}

static void updateInterrupts(uint8_t tempStatus)
{
  disableTmsPioInterrupts();
  if ((currentStatus & STATUS_INT) == 0)
  {
    currentStatus = (currentStatus & 0xe0) | tempStatus;

    vrEmuTms9918SetStatusImpl(currentStatus);
    updateTmsReadAhead();

    currentInt = vrEmuTms9918InterruptStatusImpl();
    gpio_put(GPIO_INT, !currentInt);
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
  pio_sm_set_clkdiv(pio0, pioSm, clockDiv);
  pio_sm_set_enabled(pio0, pioSm, true);
}

/*
 * initialise a clock output using PIO
 */
void initClock(uint gpio, uint pioSm, float freqHz)
{
  static uint clocksPioOffset = -1;

  if (clocksPioOffset == -1)
  {
    clocksPioOffset = pio_add_program(pio0, &clock_program);
  }

  pio_gpio_init(pio0, gpio);
  pio_sm_set_consecutive_pindirs(pio0, pioSm, gpio, 1, true);
  pio_sm_config c = clock_program_get_default_config(clocksPioOffset);
  sm_config_set_set_pins(&c, gpio, 1);

  pio_sm_init(pio0, pioSm, clocksPioOffset, &c);

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
    validWrites = !(TMS_REGISTER(tms9918, 1) & 0x40);

    allowSplashHide();
  }

  updateDiagnostics(frameCount);


  // here, we catch the case where the last row(s) were
  // missed and we never raised an interrupt. do it now  
  if (!doneInt)
  {
    eofInterrupt();
    updateInterrupts(STATUS_INT);
  }
}

/*
 * generate a single VGA scanline (called by vgaLoop(), runs on proc1)
 */
static void __time_critical_func(tmsScanline)(uint16_t y, VgaParams* params, uint16_t* pixels)
{
  int vPixels = (TMS_REGISTER(tms9918, 0x31) & 0x40) ? 30 * 8 : 24 * 8;

  const uint32_t vBorder = (VIRTUAL_PIXELS_Y - vPixels) / 2;
  const uint32_t hBorder = (VIRTUAL_PIXELS_X - TMS9918_PIXELS_X * 2) / 2;

  uint32_t* dPixels = (uint32_t*)pixels;
  bg = bigRgb2LittleBgr(tms9918->vram.map.pram[vrEmuTms9918RegValue(TMS_REG_FG_BG_COLOR) & 0x0f]);
  bg = bg | (bg << 16);

  if (y == 0)
  {
    doneInt = false;
  }

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
    dma_channel_wait_for_finish_blocking(dma32);

#if PICO9918_DIAG
    renderDiagnostics(y, pixels); // TODO: This won't display in ROW30 mode
#endif

    if (!validWrites || (frameCount < 600))
    {
      outputSplash(y, frameCount, vBorder, vPixels, pixels);

      if (testingClock && frameCount == 599)
      {
        // new clock lasted 10 seconds... let's accept it
        tms9918->config[CONF_CLOCK_TESTED] = tms9918->config[CONF_CLOCK_PRESET_ID];
        tms9918->config[CONF_SAVE_TO_FLASH] = 1;
        testingClock = false;
      }
    }
    if (TMS_REGISTER(tms9918, 0x32) & 0x40)
    {
      gpuTrigger();
    }
    return;
  }

  uint32_t frameStart = time_us_32();

  y -= vBorder;
  tms9918->vram.map.blanking = 0;
  tms9918->vram.map.scanline = y;
  TMS_STATUS(tms9918, 0x03) = y;

  /*** left border ***/
  dma_channel_set_write_addr(dma32, dPixels, false);
  dma_channel_set_trans_count(dma32, hBorder / 2, true);

  /*** main display region ***/

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
  }

  updateInterrupts(tempStatus);

  /* convert from  palette to bgr12 */
  if (vrEmuTms9918DisplayMode(tms9918) == TMS_MODE_TEXT80)
  {
    uint32_t pram[256];
    uint32_t data;
    for (int i = 0, x = 0; i < 16; ++i)
    {
      uint32_t c = tms9918->vram.map.pram[i] & 0xFF0F;
      for (int j = 0; j < 16; ++j, ++x)
      {
        data = ((tms9918->vram.map.pram[j] & 0xFF0F) << 16) | c; data |= ((data >> 8) & 0x00f000f0); pram[x] = data;
      }
    }
    tms9918->vram.map.blanking = 1; // H

    uint8_t* src = &(tmsScanlineBuffer [0]);
    uint8_t* end = &(tmsScanlineBuffer[TMS9918_PIXELS_X]);
    uint32_t* dP = (uint32_t*)&(pixels [hBorder]);

    while (src < end)
    {
      dP [0] = pram[src [0]];
      dP [1] = pram[src [1]];
      dP [2] = pram[src [2]];
      dP [3] = pram[src [3]];
      dP [4] = pram[src [4]];
      dP [5] = pram[src [5]];
      dP [6] = pram[src [6]];
      dP [7] = pram[src [7]];
      dP += 8;
      src += 8;
    }
  }
  else
  {
    uint8_t* src = &(tmsScanlineBuffer [0]);
    uint8_t* end = &(tmsScanlineBuffer[TMS9918_PIXELS_X]);
    uint32_t* dP = (uint32_t*)&(pixels [hBorder]);
    uint32_t pram [64];
    uint32_t data;
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
    tms9918->vram.map.blanking = 1; // H

    while (src < end)
    {
      dP [0] = pram[src [0]];
      dP [1] = pram[src [1]];
      dP [2] = pram[src [2]];
      dP [3] = pram[src [3]];
      dP [4] = pram[src [4]];
      dP [5] = pram[src [5]];
      dP [6] = pram[src [6]];
      dP [7] = pram[src [7]];
      dP += 8;
      src += 8;
    }
  }

  /*** right border ***/
  //dma_channel_wait_for_finish_blocking(dma32);
  dma_channel_set_write_addr(dma32, &(dPixels [(hBorder + TMS9918_PIXELS_X * 2) / 2]), true);

  updateRenderTime(renderTime,  time_us_32() - frameStart);

#if PICO9918_DIAG
    dma_channel_wait_for_finish_blocking(dma32);

    renderDiagnostics(y + vBorder, pixels);
#endif
}

/*
 * Set up PIOs for TMS9918 <-> CPU interface
 */
void tmsPioInit()
{
  irq_set_exclusive_handler(TMS_IRQ, pio_irq_handler);
  irq_set_enabled(TMS_IRQ, true);

  uint tmsWriteProgram = pio_add_program(TMS_PIO, &tmsWrite_program);

  pio_sm_config writePioConfig = tmsWrite_program_get_default_config(tmsWriteProgram);
  sm_config_set_in_pins(&writePioConfig, GPIO_CD7);
  sm_config_set_in_shift(&writePioConfig, false, true, 16); // L shift, autopush @ 16 bits
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
  sm_config_set_in_pins(&readPioConfig, GPIO_CSR);
  sm_config_set_jmp_pin(&readPioConfig, GPIO_MODE);
  sm_config_set_out_pins(&readPioConfig, GPIO_CD7, 8);
  sm_config_set_in_shift(&readPioConfig, false, false, 32); // L shift
  sm_config_set_out_shift(&readPioConfig, true, false, 32); // R shift
  sm_config_set_clkdiv(&readPioConfig, 4.0f);

  pio_sm_init(TMS_PIO, tmsReadSm, tmsReadProgram, &readPioConfig);
  pio_sm_set_enabled(TMS_PIO, tmsReadSm, true);
  pio_set_irq0_source_enabled(TMS_PIO, pis_sm1_rx_fifo_not_empty, true);

  pio_sm_put(TMS_PIO, tmsReadSm, 0x000000ff);
}


/*
 * 2nd CPU core (proc1) entry
 */
void proc1Entry()
{
  // set up gpio pins
  gpio_init_mask(GPIO_CD_MASK | GPIO_CSR_MASK | GPIO_CSW_MASK | GPIO_MODE_MASK | GPIO_MODE1_MASK | GPIO_INT_MASK | GPIO_RESET_MASK);
  gpio_put_all(GPIO_INT_MASK);
  gpio_set_dir_all_bits(GPIO_INT_MASK); // int is an output

  tmsPioInit();

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
    // we do this now rather than at boot to ensure a fast (133MHz?) flash clock
    // for the transfer of the firmware to RAM at startup

    ssi_hw->ssienr = 0; // change (reduce) flash SPI clock rate
    ssi_hw->baudr = 0;
    ssi_hw->baudr = 4;  // clock divider (must be even and result in a max of 133MHz)
    ssi_hw->ssienr = 1;    

    clockPresetIndex = tms9918->config[CONF_CLOCK_PRESET_ID];
    clockSettings = clockPresets[clockPresetIndex];

    vreg_set_voltage(clockSettings.voltage);
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

  /* then set up VGA output */
  VgaInitParams params = { 0 };
  params.params = vgaGetParams(DISPLAY_MODE);
  setVgaParamsScaleY(&params.params, DISPLAY_YSCALE);

  /* set vga scanline callback to generate tms9918 scanlines */
  params.scanlineFn = tmsScanline;
  params.endOfFrameFn = tmsEndOfFrame;
  params.porchFn = tmsPorch;

  const char *version = PICO9918_VERSION;

  vgaInit(params);

  /* signal proc1 that we're ready to start the display */
  multicore_fifo_push_blocking(0);

  /* initialize the GPU */
  gpuInit();

  /* pass control of core0 over to the TMS9900 GPU */
  gpuLoop();

  return 0;
}
