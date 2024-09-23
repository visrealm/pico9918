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
#include "tms9918.pio.h"

#include "impl/vrEmuTms9918Priv.h"
#include "vrEmuTms9918Util.h"

#include "gpu.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

 /*
  * Pin mapping (PCB v0.3)
  *
  * Pin  | GPIO | Name      | TMS9918A Pin
  * -----+------+-----------+-------------
  *  19  |  14  |  CD7      |  24
  *  20  |  15  |  CD6      |  23
  *  21  |  16  |  CD5      |  22
  *  22  |  17  |  CD4      |  21
  *  24  |  18  |  CD3      |  20
  *  25  |  19  |  CD2      |  19
  *  26  |  20  |  CD1      |  18
  *  27  |  21  |  CD0      |  17
  *  29  |  22  |  /INT     |  16
  *  30  |  RUN |  RST      |  34
  *  31  |  26  |  /CSR     |  15
  *  32  |  27  |  /CSW     |  14
  *  34  |  28  |  MODE     |  13
  *  35  |  29  |  GROMCLK  |  37
  *  37  |  23  |  CPUCLK   |  38
  *
  * Note: Due to GROMCLK and CPUCLK using GPIO23 and GPIO29
  *       a genuine Raspberry Pi Pico can't be used.
  *       v0.3 of the PCB is designed for the DWEII?
  *       RP2040 USB-C module which exposes these additional
  *       GPIOs. A future pico9918 revision (v0.4+) will do without
  *       an external RP2040 board and use the RP2040 directly.
  *
  * Purchase links:
  *       https://www.amazon.com/RP2040-Board-Type-C-Raspberry-Micropython/dp/B0CG9BY48X
  *       https://www.aliexpress.com/item/1005007066733934.html
  */

#define PCB_MAJOR_VERSION 0
#define PCB_MINOR_VERSION 4

// compile-options to ease development between Jason and I
#ifndef PICO9918_NO_SPLASH
#define PICO9918_NO_SPLASH      0
#endif

#if !PICO9918_NO_SPLASH
#include "splash.h"
#endif

#define GPIO_CD7 14
#define GPIO_CSR tmsRead_CSR_PIN  // defined in tms9918.pio
#define GPIO_CSW tmsWrite_CSW_PIN // defined in tms9918.pio
#define GPIO_MODE 28
#define GPIO_INT 22

#if PCB_MAJOR_VERSION != 0
#error "Time traveller?"
#endif

  // pin-mapping for gromclk and cpuclk changed in PCB v0.4
  // in order to have MODE and MODE1 sequential
#if PCB_MINOR_VERSION < 4
#define GPIO_GROMCL 29
#define GPIO_CPUCL 23
#else
#define GPIO_GROMCL 25
#define GPIO_CPUCL 24
#define GPIO_RESET 23
#define GPIO_MODE1 29
#endif


#define GPIO_CD_MASK (0xff << GPIO_CD7)
#define GPIO_CSR_MASK (0x01 << GPIO_CSR)
#define GPIO_CSW_MASK (0x01 << GPIO_CSW)
#define GPIO_MODE_MASK (0x01 << GPIO_MODE)
#define GPIO_INT_MASK (0x01 << GPIO_INT)

#define TMS_CRYSTAL_FREQ_HZ 10738635.0f

#define PICO_CLOCK_PLL 1260000000
#define PICO_CLOCK_PLL_DIV1 4
#define PICO_CLOCK_PLL_DIV2 1
#define PICO_CLOCK_HZ (PICO_CLOCK_PLL / PICO_CLOCK_PLL_DIV1 / PICO_CLOCK_PLL_DIV2)

#define TMS_PIO pio1
#define TMS_IRQ PIO1_IRQ_0

/* file globals */

static uint8_t nextValue = 0;     /* TMS9918A read-ahead value */
static bool currentInt = false;   /* current interrupt state */
static uint8_t currentStatus = 0x1f; /* current status register value */

static __attribute__((section(".scratch_y.buffer"))) uint32_t bg; 

static __attribute__((section(".scratch_x.buffer"))) uint8_t __aligned(8) tmsScanlineBuffer[TMS9918_PIXELS_X + 8];

const uint tmsWriteSm = 0;
const uint tmsReadSm = 1;
static int frameCount = 0;
static int logoOffset = 100;

static uint32_t dma32; // memset 32bit


/* F18A palette entries are big-endian 0x0RGB which looks like
   0xGB0R to our RP2040. our vga code is expecting 0x0BGR
   so we've got B and R correct by pure chance. just need to shift G
   over. this function does that. note: msbs are ignore so... */

inline uint32_t bigRgb2LittleBgr(uint32_t val)
{
  return val | ((val >> 12) << 4);
}

/*
 * update the value send to the read PIO
 */
inline static void updateTmsReadAhead()
{
  uint32_t readAhead = 0xff;              // pin direction
  readAhead |= nextValue << 8;
  readAhead |= (tms9918->status [tms9918->registers [0x0F] & 0x0F]) << 16;
  pio_sm_put(TMS_PIO, tmsReadSm, readAhead);
}


#ifdef GPIO_RESET

/*
 * handle reset pin going active (low)
 */
void __not_in_flash_func(gpioIrqHandler)()
{
  gpio_acknowledge_irq(GPIO_RESET, GPIO_IRQ_EDGE_FALL);
  vrEmuTms9918Reset();

  nextValue = 0;
  updateTmsReadAhead();  
  
  frameCount = 0;
  logoOffset = 100;
  currentInt = false;
  gpio_put(GPIO_INT, !currentInt);
}

#endif

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
      tms9918->regWriteStage = 0;
      switch (tms9918->registers [0x0F] & 0x0F)
      {
        case 0:
          currentStatus = 0x1f;
          vrEmuTms9918SetStatusImpl(currentStatus);
          currentInt = false;
          gpio_put(GPIO_INT, !currentInt);
          break;
        case 1:
          tms9918->status [0x01] &= ~0x01;
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
  __dmb();
  *((io_rw_32*)(PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET)) = 1u << TMS_IRQ;
  *((io_rw_32*)(PPB_BASE + M0PLUS_NVIC_ISER_OFFSET)) = 1u << TMS_IRQ;
}

/*
 * disable gpio interrupts inline
 */
static inline void disableTmsPioInterrupts()
{
  *((io_rw_32*)(PPB_BASE + M0PLUS_NVIC_ICER_OFFSET)) = 1u << TMS_IRQ;
  __dmb();
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


static void tmsEndOfFrame(uint32_t frameNumber)
{
  ++frameCount;
}

/*
 * output the PICO9918 splash logo / firmware version at the bottom of the screen
 */
static void outputSplash(uint16_t y, uint32_t vBorder, uint32_t vPixels, uint16_t* pixels)
{
#if !PICO9918_NO_SPLASH
  #define VIRTUAL_PIXELS_Y 240

  if (y == 0)
  {
    if (frameCount & 0x01)
    {
      if (frameCount < 180 && logoOffset > 12) --logoOffset;
      else if (frameCount > 480) ++logoOffset;
    }
  }

  if (y < (VIRTUAL_PIXELS_Y - 1))
  {
    y -= vBorder + vPixels + logoOffset;
    if (y < splashHeight)
    {
      /* the source image is 2bpp, so 4 pixels in a byte
       * this doesn't need to be overly performant as it only
       * gets called in the first few seconds of startup (or reset)
       */
      const int leftBorderPx = 4;
      const int splashBpp = 2;
      const int splashPixPerByte = 8 / splashBpp;
      uint8_t* splashPtr = splash + (y * splashWidth / splashPixPerByte);

      for (int x = leftBorderPx; x < leftBorderPx + splashWidth; x += splashPixPerByte)
      {
        uint8_t c = *(splashPtr++);
        uint8_t pixMask = 0xc0;
        uint8_t offset = 6;

        for (int px = 0; px < 4; ++px, offset -= 2, pixMask >>= 2)
        {
          uint8_t palIndex = (c & pixMask) >> offset;
          if (palIndex) pixels[x + px] = splash_pal[palIndex];
        }
      }
    }
  }
#endif
}

/*
 * generate a single VGA scanline (called by vgaLoop(), runs on proc1)
 */
static void __time_critical_func(tmsScanline)(uint16_t y, VgaParams* params, uint16_t* pixels)
{

#if 1
  // better compile-time optimizations if we hard-code these
#define VIRTUAL_PIXELS_X 640
#define VIRTUAL_PIXELS_Y 240
#else 
#define VIRTUAL_PIXELS_X params->hVirtualPixels
#define VIRTUAL_PIXELS_Y params->vVirtualPixels
#endif

  int vPixels = (tms9918->registers[0x31] & 0x40) ? 30 * 8 - 1 : 24 * 8;

  const uint32_t vBorder = (VIRTUAL_PIXELS_Y - vPixels) / 2;
  const uint32_t hBorder = (VIRTUAL_PIXELS_X - TMS9918_PIXELS_X * 2) / 2;

  static bool doneInt = false;

  uint32_t* dPixels = (uint32_t*)pixels;
  bg = bigRgb2LittleBgr(tms9918->pram[vrEmuTms9918RegValue(TMS_REG_FG_BG_COLOR) & 0x0f]);
  bg = bg | (bg << 16);

  if (y == 0) doneInt = false;

  /*** top and bottom borders ***/
  if (y < vBorder || y >= (vBorder + vPixels))
  {
    dma_channel_wait_for_finish_blocking(dma32);
    dma_channel_set_write_addr(dma32, dPixels, false);
    dma_channel_set_trans_count(dma32, VIRTUAL_PIXELS_X / 2, true);
    tms9918->scanline = 0;
    tms9918->blanking = 1;
    tms9918->status [0x03] = 0;
    dma_channel_wait_for_finish_blocking(dma32);

    if (frameCount < 600)
      outputSplash(y, vBorder, vPixels, pixels);

    return;
  }

  /*** left border ***/
  dma_channel_set_write_addr(dma32, dPixels, false);
  dma_channel_set_trans_count(dma32, hBorder / 2, true);

  y -= vBorder;
  /*** main display region ***/

  /* generate the scanline */
  uint8_t tempStatus = vrEmuTms9918ScanLine(y, tmsScanlineBuffer);

  /*** interrupt signal? ***/
  const int interruptThreshold = 4;
  if (y < vPixels - interruptThreshold)
  {
    tms9918->status [0x01] &= ~0x03;
  }
  else
  {
    tms9918->status [0x01] &= ~0x01;
  }

  if (tms9918->scanline && (tms9918->registers[0x13] == tms9918->scanline))
  {
    tms9918->status [0x01] |= 0x01;
    tempStatus |= STATUS_INT;
  }

  if (tms9918->registers[0x32] & 0x40)
  {
    gpuTrigger();
  }

  if (!doneInt && y >= vPixels - interruptThreshold)
  {
    doneInt = true;
    tms9918->status [0x01] |=  0x02;
    if (tms9918->registers[0x32] & 0x20)
    {
      gpuTrigger();
    }

    tempStatus |= STATUS_INT;
  }

  updateInterrupts(tempStatus);

  /* convert from  palette to bgr12 */
  if (vrEmuTms9918DisplayMode(tms9918) == TMS_MODE_TEXT80)
  {
    int tmsX = 0;
    for (int x = hBorder; tmsX < TMS9918_PIXELS_X; x += 2, ++tmsX)
    {
      uint8_t doublePix = tmsScanlineBuffer[tmsX];
      pixels[x] = bigRgb2LittleBgr(tms9918->pram[doublePix >> 4]);
      pixels[x + 1] = bigRgb2LittleBgr(tms9918->pram[doublePix & 0x0f]);
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
      data = tms9918->pram [i + 0]; data = data | ((data >> 12) << 4); pram [i + 0] = data | (data << 16);
      data = tms9918->pram [i + 1]; data = data | ((data >> 12) << 4); pram [i + 1] = data | (data << 16);
      data = tms9918->pram [i + 2]; data = data | ((data >> 12) << 4); pram [i + 2] = data | (data << 16);
      data = tms9918->pram [i + 3]; data = data | ((data >> 12) << 4); pram [i + 3] = data | (data << 16);
      data = tms9918->pram [i + 4]; data = data | ((data >> 12) << 4); pram [i + 4] = data | (data << 16);
      data = tms9918->pram [i + 5]; data = data | ((data >> 12) << 4); pram [i + 5] = data | (data << 16);
      data = tms9918->pram [i + 6]; data = data | ((data >> 12) << 4); pram [i + 6] = data | (data << 16);
      data = tms9918->pram [i + 7]; data = data | ((data >> 12) << 4); pram [i + 7] = data | (data << 16);
    }
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
  dma_channel_wait_for_finish_blocking(dma32);
  dma_channel_set_write_addr(dma32, &(dPixels [(hBorder + TMS9918_PIXELS_X * 2) / 2]), true);

  tms9918->scanline = y + 1;
  tms9918->blanking = 0; // Is it even possible to support H blanking?
  tms9918->status [0x03] = tms9918->scanline;  


#define REG_DIAG 0
#if REG_DIAG

  /* Register diagnostics. with this enabled, we overlay a binary 
   * representation of the VDP registers to the right-side of the screen */

  dma_channel_wait_for_finish_blocking(dma32);

  const int diagBitWidth = 4;
  const int diagBitSpacing = 2;
  const int diagRightMargin = 6;

  const int16_t diagBitOn = 0x2e2;
  const int16_t diagBitOff = 0x222;

  int realY = y;

  uint8_t reg = realY / 3;
  if (reg < 64)
  {
    if (realY % 3 != 0)
    {
      int8_t regValue = tms9918->registers[reg];
      int x = VIRTUAL_PIXELS_X - ((8 * (diagBitWidth + diagBitSpacing)) + diagRightMargin);
      for (int i = 0; i < 8; ++i)
      {
        const bool on = regValue < 0;
        for (int xi = 0; xi < diagBitWidth; ++xi)
        {
          pixels[x++] = on ? diagBitOn : diagBitOff;
        }
        x += diagBitSpacing + (i == 3) * diagBitSpacing;
        regValue <<= 1;
      }
    }
    else if ((reg & 0x07) == 0)
    {
      int x = VIRTUAL_PIXELS_X - diagRightMargin + 1;

      for (int xi = 0; xi < diagBitWidth; ++xi)
      {
        pixels[x++] ^= pixels[x];
      }
    }
  }

#endif  
}

/*
 * initialise a clock output using PIO
 */
uint initClock(uint gpio, float freqHz)
{
  static uint clocksPioOffset = -1;

  if (clocksPioOffset == -1)
  {
    clocksPioOffset = pio_add_program(pio0, &clock_program);
  }

  static uint clkSm = 2;

  pio_gpio_init(pio0, gpio);
  pio_sm_set_consecutive_pindirs(pio0, clkSm, gpio, 1, true);
  pio_sm_config c = clock_program_get_default_config(clocksPioOffset);
  sm_config_set_set_pins(&c, gpio, 1);

  pio_sm_init(pio0, clkSm, clocksPioOffset, &c);

  float clockDiv = (float)PICO_CLOCK_HZ / (freqHz * 2.0f);
  pio_sm_set_clkdiv(pio0, clkSm, clockDiv);
  pio_sm_set_enabled(pio0, clkSm, true);

  return clkSm++;
}

/*
 * Set up PIOs for TMS9918 <-> CPU interface
 */
void tmsPioInit()
{
  irq_set_exclusive_handler(TMS_IRQ, pio_irq_handler);
  irq_set_enabled(TMS_IRQ, true);

  uint tmsWriteProgram = pio_add_program(TMS_PIO, &tmsWrite_program);

  pio_sm_config writeConfig = tmsWrite_program_get_default_config(tmsWriteProgram);
  sm_config_set_in_pins(&writeConfig, GPIO_CD7);
  sm_config_set_in_shift(&writeConfig, false, true, 16); // L shift, autopush @ 16 bits
  sm_config_set_clkdiv(&writeConfig, 4.0f);

  pio_sm_init(TMS_PIO, tmsWriteSm, tmsWriteProgram, &writeConfig);
  pio_sm_set_enabled(TMS_PIO, tmsWriteSm, true);
  pio_set_irq0_source_enabled(TMS_PIO, pis_sm0_rx_fifo_not_empty, true);

  uint tmsReadProgram = pio_add_program(TMS_PIO, &tmsRead_program);

  for (uint i = 0; i < 8; ++i)
  {
    pio_gpio_init(TMS_PIO, GPIO_CD7 + i);
  }

  pio_sm_config readConfig = tmsRead_program_get_default_config(tmsReadProgram);
  sm_config_set_in_pins(&readConfig, GPIO_CSR);
  sm_config_set_jmp_pin(&readConfig, GPIO_MODE);
  sm_config_set_out_pins(&readConfig, GPIO_CD7, 8);
  sm_config_set_in_shift(&readConfig, false, false, 32); // L shift
  sm_config_set_out_shift(&readConfig, true, false, 32); // R shift
  sm_config_set_clkdiv(&readConfig, 4.0f);

  pio_sm_init(TMS_PIO, tmsReadSm, tmsReadProgram, &readConfig);
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
  gpio_init_mask(GPIO_CD_MASK | GPIO_CSR_MASK | GPIO_CSW_MASK | GPIO_MODE_MASK | GPIO_INT_MASK);
  gpio_put_all(GPIO_INT_MASK);
  gpio_set_dir_all_bits(GPIO_INT_MASK); // int is an output

  tmsPioInit();

  // set up the GROMCLK and CPUCLK signals
  initClock(GPIO_GROMCL, TMS_CRYSTAL_FREQ_HZ / 24.0f);
  initClock(GPIO_CPUCL, TMS_CRYSTAL_FREQ_HZ / 3.0f);

#ifdef GPIO_RESET

  // set up reset gpio interrupt handler
  irq_set_exclusive_handler(IO_IRQ_BANK0, gpioIrqHandler);
  irq_set_enabled(IO_IRQ_BANK0, true);
  gpio_set_irq_enabled(GPIO_RESET, GPIO_IRQ_EDGE_FALL, true);

#endif

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
  vreg_set_voltage(VREG_VOLTAGE_1_30);

  set_sys_clock_pll(PICO_CLOCK_PLL, PICO_CLOCK_PLL_DIV1, PICO_CLOCK_PLL_DIV2);   // 252000

  /* we need one of these. it's the main guy */
  vrEmuTms9918Init();

  /* launch core 1 which handles TMS9918<->CPU and rendering scanlines */
  multicore_launch_core1(proc1Entry);


	dma32 = 2;//dma_claim_unused_channel(true);
	dma_channel_config cfg = dma_channel_get_default_config(dma32);
	channel_config_set_read_increment(&cfg, false);
	channel_config_set_write_increment(&cfg, true);
	channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
	dma_channel_set_config(dma32, &cfg, false);

  dma_channel_set_read_addr(dma32, &bg, false);


  /* then set up VGA output */
  VgaInitParams params = { 0 };
  params.params = vgaGetParams(VGA_640_480_60HZ);

  /* virtual size will be 640 x 240 */
  setVgaParamsScaleY(&params.params, 2);

  /* set vga scanline callback to generate tms9918 scanlines */
  params.scanlineFn = tmsScanline;
  params.endOfFrameFn = tmsEndOfFrame;

  vgaInit(params);

  /* signal proc1 that we're ready to start the display */
  multicore_fifo_push_blocking(0);

  /* initialize the GPU */
  gpuInit();

  /* pass control of core0 over to the TMS9900 GPU */
  gpuLoop();

  return 0;
}
