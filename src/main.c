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

#include "palette.h"

#include "impl/vrEmuTms9918Priv.h"
#include "vrEmuTms9918Util.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

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

//extern "C" 
uint16_t run9900 (uint8_t * memory, uint16_t pc, uint16_t wp, uint8_t * regx38);

/* file globals */

static uint8_t nextValue = 0;     /* TMS9918A read-ahead value */
static bool currentInt = false;   /* current interrupt state */
static uint8_t currentStatus = 0x1f; /* current status register value */

static __attribute__((section(".scratch_x.buffer"))) uint8_t __aligned(8) tmsScanlineBuffer[TMS9918_PIXELS_X + 8];

const uint tmsWriteSm = 0;
const uint tmsReadSm = 1;
static int frameCount = 0;
static int logoOffset = 100;


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

/*
 * generate a single VGA scanline (called by vgaLoop(), runs on proc1)
 */
static void __time_critical_func(tmsScanline)(uint16_t y, VgaParams* params, uint16_t* pixels)
{

#if 1
  // better compile-time optimizations if we hard-code these
#define VIRTUAL_PIXELS_X 320
#define VIRTUAL_PIXELS_Y 240
#else 
#define VIRTUAL_PIXELS_X params->hVirtualPixels
#define VIRTUAL_PIXELS_Y params->vVirtualPixels
#endif

  int vPixels = ((tms9918->registers[0x31] & 0x40) ? 30 : 24) * 8;

  const uint32_t vBorder = (VIRTUAL_PIXELS_Y - vPixels) / 2;
  const uint32_t hBorder = (VIRTUAL_PIXELS_X - TMS9918_PIXELS_X * 1) / 2;

  static bool doneInt = false;

  uint32_t* dPixels = (uint32_t*)pixels;
  uint32_t bg = bigRgb2LittleBgr(tms9918->pram[vrEmuTms9918RegValue(TMS_REG_FG_BG_COLOR) & 0x0f]);
  bg = bg | (bg << 16);

  if (y < 10) doneInt = false;

  /*** top and bottom borders ***/
  if (y < vBorder || y >= (vBorder + vPixels))
  {
    tms9918->scanline = 0;
    tms9918->blanking = 1;
    tms9918->status [0x01] &= ~0x01;
    tms9918->status [0x01] |=  0x02;
    tms9918->status [0x03] = 0;
    for (int x = 0; x < VIRTUAL_PIXELS_X / 2; ++x)
    {
      dPixels[x] = bg;
    }

    /* source: C:/Users/troy/OneDrive/Documents/projects/pico9918/src/res/splash.png
     * size  : 172px x 10px
     *       : 430 bytes
     * format: 16-bit abgr palette, 2bpp indexed image
     */
#if !PICO9918_NO_SPLASH
    if (frameCount < 600)
    {
      if (y == 0)
      {
        ++frameCount;
        if (frameCount & 0x01)
        {
          if (frameCount < 200 && logoOffset > 12) --logoOffset;
          else if (frameCount > 500) ++logoOffset;
        }
      }

      if (y < (VIRTUAL_PIXELS_Y - 1))
      {
        y -= vBorder + vPixels + logoOffset;
        if (y < splashHeight)
        {
          uint8_t* splashPtr = splash + (y * splashWidth / 4);
          for (int x = 4; x < 4 + splashWidth / 2; x += 2)
          {
            uint8_t c = *(splashPtr++);
            uint8_t p0 = (c & 0xc0);
            uint8_t p1 = (c & 0x30);
            uint8_t p2 = (c & 0x0c);
            uint8_t p3 = (c & 0x03);

            if (p0) { pixels[x] = splash_pal[(p0 >> 6)]; }
            //if (p1) { pixels[x + 1] = splash_pal[(p1 >> 4)]; }
            if (p2) { pixels[x + 1] = splash_pal[(p2 >> 2)]; }
            //if (p3) { pixels[x + 3] = splash_pal[p3]; }
          }
        }
      }
    }
#endif
    return;
  }

  y -= vBorder;
  /*** main display region ***/

  /* generate the scanline */
  uint8_t tempStatus = vrEmuTms9918ScanLine(y, tmsScanlineBuffer);

  /*** interrupt signal? ***/
  if (!doneInt && y > vPixels - 6)
  {
    tempStatus |= STATUS_INT;
    doneInt = true;
    if (tms9918->registers[0x32] & 0x20)
    {
        tms9918->restart = 1;
    }
  }

  if (tms9918->scanline && (tms9918->registers[0x13] == tms9918->scanline))
  {
    tms9918->status [0x01] |= 0x01;
    tempStatus |= STATUS_INT;
  }

  if (tms9918->registers[0x32] & 0x40)
  {
      tms9918->restart = 1;
  }

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

  /*** left border ***/
  for (int x = 0; x < hBorder / 2; ++x)
  {
    dPixels[x] = bg;
  }

  /* convert from  palette to bgr12 */
  int tmsX = 0;
  if (tmsScanlineBuffer[0] & 0xc0)
  {
    for (int x = hBorder; x < hBorder + TMS9918_PIXELS_X * 1; x += 1, ++tmsX)
    {
      pixels[x] = bigRgb2LittleBgr(tms9918->pram[(tmsScanlineBuffer[tmsX] & 0xf0) >> 4]);
      //pixels[x + 1] = rgb12tobgr12[tms9918->pram[tmsScanlineBuffer[tmsX] & 0x0f]];
    }
  }
  else
  {
    /*for (int x = hBorder; x < hBorder + TMS9918_PIXELS_X * 1; x += 1, ++tmsX)
    {
      pixels[x] = bigRgb2LittleBgr(tms9918->pram[tmsScanlineBuffer[tmsX]]);
      //pixels[x + 1] = pixels[x];
    }*/

    uint8_t* src = &(tmsScanlineBuffer [0]);
    uint8_t* end = &(tmsScanlineBuffer[TMS9918_PIXELS_X * 1]);
    uint32_t* dP = (uint32_t*)&(pixels [hBorder]);
    while (src < end)
    {
      uint32_t data;
      data = tms9918->pram[src [0]] | (tms9918->pram[src [1]] << 16);
      dP [0] = data | ((data >> 8) & 0x00f000f0);
      data = tms9918->pram[src [2]] | (tms9918->pram[src [3]] << 16);
      dP [1] = data | ((data >> 8) & 0x00f000f0);
      data = tms9918->pram[src [4]] | (tms9918->pram[src [5]] << 16);
      dP [2] = data | ((data >> 8) & 0x00f000f0);
      data = tms9918->pram[src [6]] | (tms9918->pram[src [7]] << 16);
      dP [3] = data | ((data >> 8) & 0x00f000f0);
      dP += 4;
      src += 8;
    }
  }


  /*** right border ***/
  for (int x = (hBorder + TMS9918_PIXELS_X * 1) / 2; x < VIRTUAL_PIXELS_X / 2; ++x)
  {
    dPixels[x] = bg;
  }
  
  tms9918->scanline = y + 1;
  tms9918->blanking = 0; // Is it even possible to support H blanking?
  tms9918->status [0x01] &= ~0x03;
  tms9918->status [0x03] = tms9918->scanline;  
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

static uint8_t preload [] = {
 0x02, 0x0F, 0x47, 0xFE, 0x10, 0x0D, 0x40, 0x36, 0x40, 0x5A, 0x40, 0x94, 0x40, 0xB4, 0x40, 0xFA,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0x0C, 0xA0, 0x41, 0x1C, 0x03, 0x40, 0x04, 0xC1, 0xD0, 0x60, 0x3F, 0x00, 0x09, 0x71, 0xC0, 0x21,
 0x40, 0x06, 0x06, 0x90, 0x10, 0xF7, 0xC0, 0x20, 0x3F, 0x02, 0xC0, 0x60, 0x3F, 0x04, 0xC0, 0xA0,
 0x3F, 0x06, 0xD0, 0xE0, 0x3F, 0x01, 0x13, 0x05, 0xD0, 0x10, 0xDC, 0x40, 0x06, 0x02, 0x16, 0xFD,
 0x10, 0x03, 0xDC, 0x70, 0x06, 0x02, 0x16, 0xFD, 0x04, 0x5B, 0x0D, 0x0B, 0x06, 0xA0, 0x40, 0xB4,
 0x0F, 0x0B, 0xC1, 0xC7, 0x13, 0x16, 0x04, 0xC0, 0xD0, 0x20, 0x60, 0x04, 0x0A, 0x30, 0xC0, 0xC0,
 0x04, 0xC1, 0x02, 0x02, 0x04, 0x00, 0xCC, 0x01, 0x06, 0x02, 0x16, 0xFD, 0x04, 0xC0, 0xD0, 0x20,
 0x41, 0x51, 0x06, 0xC0, 0x0A, 0x30, 0xA0, 0x03, 0x0C, 0xA0, 0x41, 0xAE, 0xD8, 0x20, 0x41, 0x51,
 0xB0, 0x00, 0x04, 0x5B, 0xD8, 0x20, 0x41, 0x1A, 0x3F, 0x00, 0x02, 0x00, 0x41, 0xD6, 0xC8, 0x00,
 0x3F, 0x02, 0x02, 0x00, 0x40, 0x06, 0xC8, 0x00, 0x3F, 0x04, 0x02, 0x00, 0x40, 0x10, 0xC8, 0x00,
 0x3F, 0x06, 0x04, 0x5B, 0x04, 0xC7, 0xD0, 0x20, 0x3F, 0x01, 0x13, 0x13, 0xC0, 0x20, 0x41, 0x18,
 0x06, 0x00, 0x0C, 0xA0, 0x41, 0x52, 0x02, 0x04, 0x00, 0x05, 0x02, 0x05, 0x3F, 0x02, 0x02, 0x06,
 0x41, 0x42, 0x8D, 0xB5, 0x16, 0x03, 0x06, 0x04, 0x16, 0xFC, 0x10, 0x09, 0x06, 0x00, 0x16, 0xF1,
 0x10, 0x09, 0xC0, 0x20, 0x3F, 0x02, 0x0C, 0xA0, 0x41, 0x52, 0x80, 0x40, 0x14, 0x03, 0x0C, 0xA0,
 0x41, 0x9A, 0x05, 0x47, 0xD8, 0x07, 0xB0, 0x00, 0x04, 0x5B, 0x0D, 0x0B, 0x06, 0xA0, 0x40, 0xB4,
 0x0F, 0x0B, 0xC1, 0xC7, 0x13, 0x04, 0xC0, 0x20, 0x3F, 0x0C, 0x0C, 0xA0, 0x41, 0xAE, 0x04, 0x5B,
 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x41, 0x10,
 0x02, 0x01, 0x41, 0x15, 0x02, 0x02, 0x0B, 0x00, 0x03, 0xA0, 0x32, 0x02, 0x32, 0x30, 0x32, 0x30,
 0x32, 0x30, 0x36, 0x00, 0x02, 0x02, 0x00, 0x06, 0x36, 0x31, 0x06, 0x02, 0x16, 0xFD, 0x03, 0xC0,
 0x0C, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x88, 0x00, 0x41, 0x18, 0x1A, 0x03, 0xC0, 0x60, 0x41, 0x18, 0x0C, 0x00, 0x0D, 0x00,
 0x0A, 0x40, 0x02, 0x01, 0x0B, 0x00, 0xA0, 0x20, 0x41, 0x16, 0x17, 0x01, 0x05, 0x81, 0xA0, 0x60,
 0x41, 0x14, 0x02, 0x03, 0x41, 0x42, 0x02, 0x02, 0x00, 0x10, 0x03, 0xA0, 0x32, 0x01, 0x06, 0xC1,
 0x32, 0x01, 0x32, 0x00, 0x06, 0xC0, 0x32, 0x00, 0x36, 0x00, 0x36, 0x33, 0x06, 0x02, 0x16, 0xFD,
 0x03, 0xC0, 0x0F, 0x00, 0xC0, 0x60, 0x41, 0x18, 0x0C, 0x00, 0x02, 0x00, 0x3F, 0x00, 0x02, 0x01,
 0x41, 0x42, 0x02, 0x02, 0x00, 0x08, 0xCC, 0x31, 0x06, 0x02, 0x16, 0xFD, 0x0C, 0x00, 0x02, 0x01,
 0x41, 0x4C, 0xD0, 0xA0, 0x41, 0x50, 0x06, 0xC2, 0xD0, 0xA0, 0x41, 0x4F, 0x02, 0x03, 0x0B, 0x00,
 0x03, 0xA0, 0x32, 0x03, 0x32, 0x31, 0x32, 0x31, 0x32, 0x31, 0x36, 0x01, 0x36, 0x30, 0x06, 0x02,
 0x16, 0xFD, 0x03, 0xC0, 0x0C, 0x00, 0x03, 0x40
};

static void __attribute__ ((noinline)) volatileHack () {
  tms9918->restart = 0;
  if ((tms9918->gpuAddress & 1) == 0) { // Odd addresses will cause the RP2040 to crash
    tms9918->registers [0x38] = 1;
    tms9918->status [2] |= 0x80; // Running
    uint16_t lastAddress = run9900 (&(tms9918->vram [0]), tms9918->gpuAddress, 0xFFFE, &(tms9918->registers [0x38]));
    if (tms9918->registers [0x38] & 1) { // GPU program decided to stop itself?
      tms9918->gpuAddress = lastAddress;
      tms9918->restart = 0;
    }
  }
  tms9918->status [2] &= ~0x80; // Stopped
  tms9918->registers [0x38] = 0;
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

  /* then set up VGA output */
  VgaInitParams params = { 0 };
  params.params = vgaGetParams(VGA_640_480_60HZ);

  /* virtual size will be 320 x 240 */
  setVgaParamsScale(&params.params, 2);

  /* set vga scanline callback to generate tms9918 scanlines */
  params.scanlineFn = tmsScanline;

  vgaInit(params);

  /* signal proc1 that we're ready to start the display */
  multicore_fifo_push_blocking(0);

  /* twiddle our thumbs - everything from this point on
     is handled by interrupts and PIOs */;

  memcpy (tms9918->gram1, preload, sizeof (preload));
  memcpy (tms9918->gram1 + 0x800, preload, sizeof (preload));
  tms9918->gpuAddress = 0x4000;
  while (1)
  {
    if (tms9918->restart)
      volatileHack ();
  }

  return 0;
}
