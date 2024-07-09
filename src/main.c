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

#include "splash.h"

#include "impl/vrEmuTms9918Priv.h"
#include "vrEmuTms9918Util.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

 //#include <stdlib.h>

  /*
   * Pin mapping
   *
   * Pin  | GPIO | Name      | TMS9918A Pin
   * -----+------+-----------+-------------
   *  19  |  14  |  CD0      |  24
   *  20  |  15  |  CD1      |  23
   *  21  |  16  |  CD2      |  22
   *  22  |  17  |  CD3      |  21
   *  24  |  18  |  CD4      |  20
   *  25  |  19  |  CD5      |  19
   *  26  |  20  |  CD6      |  18
   *  27  |  21  |  CD7      |  17
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
   *       GPIOs. A future pico9918 revision will do without
   *       an external RP2040 board and use the RP2040 directly.
   *
   * Purchase links:
   *       https://www.amazon.com/RP2040-Board-Type-C-Raspberry-Micropython/dp/B0CG9BY48X
   *       https://www.aliexpress.com/item/1005007066733934.html
   */

#define GPIO_CD0 14
#define GPIO_CSR 26
#define GPIO_CSW 27
#define GPIO_MODE 28
#define GPIO_INT 22
#define GPIO_GROMCL 29
#define GPIO_CPUCL 23
#define GPIO_LED 25

#define GPIO_CD_MASK (0xff << GPIO_CD0)
#define GPIO_CSR_MASK (0x01 << GPIO_CSR)
#define GPIO_CSW_MASK (0x01 << GPIO_CSW)
#define GPIO_MODE_MASK (0x01 << GPIO_MODE)
#define GPIO_INT_MASK (0x01 << GPIO_INT)
#define GPIO_LED_MASK (0x01 << GPIO_LED)

#define TMS_CRYSTAL_FREQ_HZ 10738635.0f

#define LED_BLINK_ON_WRITE 0

#define PICO_CLOCK_HZ 252000000
   //#define PICO_CLOCK_HZ 315000000


         /* file globals */

static uint8_t nextValue = 0; /* TMS9918A read-ahead value */
static bool currentInt = false;//GPIO_INT_MASK; /* current interrupt pin state */
static uint8_t currentStatus = 0; /* current status register value */
static uint32_t nextValueInt = 0; /* TMS9918A read-ahead value */

static uint8_t __aligned(4) tmsScanlineBuffer[TMS9918_PIXELS_X];

const uint tmsWriteSm = 0;//pio_claim_unused_sm(pio1, true);
const uint tmsReadSm = 1;//pio_claim_unused_sm(pio1, true);


/*
 * enable gpio interrupts inline
 */
static inline void enableGpioInterrupts()
{
  *((io_rw_32*)(PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET)) = 1u << IO_IRQ_BANK0;
  *((io_rw_32*)(PPB_BASE + M0PLUS_NVIC_ISER_OFFSET)) = 1u << IO_IRQ_BANK0;
}

/*
 * disable gpio interrupts inline
 */
static inline void disableGpioInterrupts()
{
  *((io_rw_32*)(PPB_BASE + M0PLUS_NVIC_ICER_OFFSET)) = 1u << IO_IRQ_BANK0;
}

/*
 * mark the interrupt as handled.
 * Note: all relevant GPIO pins are in the same group
 */
static inline void gpioInterruptHandled()
{
  uint32_t* addr = (uint32_t*)0x400140FC;
  *(uint32_t*)addr = *(uint32_t*)(addr + 24);
  //iobank0_hw->intr[GPIO_CSR >> 3u] = iobank0_hw->proc1_irq_ctrl.ints[GPIO_CSR >> 3u];
}

static
uint16_t bg = 0;


static void updateTmsReadAhead()
{
  uint32_t readAhead = 0xff;      // pin direction
  readAhead |= nextValue << 8;
  readAhead |= currentStatus << 16;
  //pio_sm_exec(pio1, tmsReadSm, 0x8080);
  //pio_sm_exec(pio1, tmsReadSm, 0x8080);
  pio_sm_put(pio1, tmsReadSm, readAhead);
}


/*
 * RP2040 exclusive GPIO interrupt callback for PROC1
 * Called whenever CSR or CSW changes and reads or writes the data
 *
 * Note: This needs to be extremely responsive. No dilly-dallying here.
 *       This function isn't pretty, but is done this way to guide
 *       gcc in producing optimized assembly. I considered writing
 *       this in assembly, but decided on this instead.
 */
 /*
 void __isr __scratch_x("isr") gpioExclusiveCallbackProc1()
 {
   // shift the bit mask combos down to lowest significant bits
   const int GPIO_IRQ_READ_DATA = (GPIO_CSW_MASK) >> GPIO_CSR;
   const int GPIO_IRQ_READ_STATUS = (GPIO_CSW_MASK | GPIO_MODE_MASK) >> GPIO_CSR;
   const int GPIO_IRQ_WRITE_DATA = (GPIO_CSR_MASK) >> GPIO_CSR;
   const int GPIO_IRQ_WRITE_REG = (GPIO_CSR_MASK | GPIO_MODE_MASK) >> GPIO_CSR;
   const int GPIO_IRQ_INACTIVE1 = (GPIO_CSW_MASK | GPIO_CSR_MASK) >> GPIO_CSR;
   const int GPIO_IRQ_INACTIVE2 = (GPIO_CSW_MASK | GPIO_CSR_MASK | GPIO_MODE_MASK) >> GPIO_CSR;

   // test the MODE, /CSW and /CSR pins to determine which action to take
   // shifting them down to bits 2, 1 and 0 respectively rather than testing
   // them at the higher bit range since ARM can only load 8-bit immediate
   // values in one cycle to compare
   gpioInterruptHandled();

   switch ((sio_hw->gpio_in >> GPIO_CSR) & 0x07)
   {
     case GPIO_IRQ_WRITE_DATA:
       vrEmuTms9918WriteDataImpl(sio_hw->gpio_in >> GPIO_CD0);
       break;

     case GPIO_IRQ_READ_DATA:
       sio_hw->gpio_oe_set = GPIO_CD_MASK;
       sio_hw->gpio_out = nextValueInt;
       vrEmuTms9918ReadDataImpl();
       break;

     case GPIO_IRQ_WRITE_REG:
       vrEmuTms9918WriteAddrImpl(sio_hw->gpio_in >> GPIO_CD0);
       currentInt = vrEmuTms9918InterruptStatusImpl() ? 0 : GPIO_INT_MASK;
       break;

     case GPIO_IRQ_READ_STATUS:
       sio_hw->gpio_oe_set = GPIO_CD_MASK;
       sio_hw->gpio_out = currentStatus;
       currentInt = GPIO_INT_MASK;
       currentStatus = (vrEmuTms9918ReadStatusImpl() << GPIO_CD0) | currentInt;
       break;

     case GPIO_IRQ_INACTIVE1:
     case GPIO_IRQ_INACTIVE2:
       sio_hw->gpio_oe_clr = GPIO_CD_MASK;
       sio_hw->gpio_out = currentInt;
       break;
   }

   // update read-ahead
   nextValue = vrEmuTms9918ReadDataNoIncImpl() << GPIO_CD0;
   nextValueInt = nextValue | currentInt;
 }
 */

uint32_t writeVals[16] = { 0 };
uint32_t readVals[16] = { 0 };
int nextReadVal = 0;
int nextWriteVal = 0;


void  __isr __scratch_x("isr") pio_irq_handler()
{
  if (pio1->irq & (1u << 0)) // write?
  {
    uint32_t writeVal = pio1->rxf[tmsWriteSm];;//pio_sm_get(pio1, tmsWriteSm);
    writeVals[nextWriteVal++] = writeVal;
    nextWriteVal &= 0x0f;

    if (writeVal & (1 << 14))//(GPIO_MODE_MASK >> GPIO_CD0)) // red/addr
    {
      //      bg |= 0xf00;
      vrEmuTms9918WriteAddrImpl(writeVal & 0xff);
      currentInt = vrEmuTms9918InterruptStatusImpl();
      gpio_put(GPIO_INT, !currentInt);
    }
    else // data
    {
      //bg |= 0x0f;
      vrEmuTms9918WriteDataImpl(writeVal & 0xff);
    }

    nextValue = vrEmuTms9918ReadDataNoIncImpl();
    updateTmsReadAhead();

    pio1->irq = (1u << 0);
  }
  else if (pio1->irq & (1u << 1)) // read?
  {
    uint32_t readVal = pio1->rxf[tmsReadSm];//pio_sm_get(pio1, tmsReadSm);
    readVals[nextReadVal++] = readVal;//0xf0f0f0f0;
    nextReadVal &= 0x0f;

    if ((readVal & 0x04) == 0) // data
    {
      //
      vrEmuTms9918ReadDataImpl();
      nextValue = vrEmuTms9918ReadDataNoIncImpl();
    }
    else // status
    {
      //bg |= 0xf0;
      currentStatus = vrEmuTms9918ReadStatusImpl();
      currentInt = false;
      gpio_put(GPIO_INT, !currentInt);
    }
    updateTmsReadAhead();

    pio1->irq = (1u << 1);
  }
  irq_clear(PIO1_IRQ_0);
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


  const uint32_t vBorder = (VIRTUAL_PIXELS_Y - TMS9918_PIXELS_Y) / 2;
  const uint32_t hBorder = (VIRTUAL_PIXELS_X - TMS9918_PIXELS_X * 2) / 2;

  //uint16_t bg = tms9918PaletteBGR12[vrEmuTms9918RegValue(TMS_REG_FG_BG_COLOR) & 0x0f];

  //if (y == 0) bg = 0;
  if (currentInt) bg = 0x000f; else bg = 0x00f0;


  /*** top and bottom borders ***/
  if (y < vBorder || y >= (vBorder + TMS9918_PIXELS_Y))
  {
    for (int x = 0; x < VIRTUAL_PIXELS_X; ++x)
    {
      pixels[x] = bg;
    }

    if (y < vBorder)
    {
      uint32_t writeVal = writeVals[y & 0x0f];
      for (int i = 0; i < 32; ++i)
      {
        uint16_t bitColor = ((writeVal << i) & 0x80000000) ? 0x0fff : 0x000;
        int offset = i * 16 + hBorder;
        for (int x = 0; x < 16; ++x)
        {
          pixels[offset + x] = bitColor;
        }
      }
    }
    else
    {
      uint32_t readVal = readVals[y & 0x0f];
      for (int i = 0; i < 32; ++i)
      {
        uint16_t bitColor = ((readVal << i) & 0x80000000) ? 0x0fff : 0x000;
        int offset = i * 16 + hBorder;
        for (int x = 0; x < 16; ++x)
        {
          pixels[offset + x] = bitColor;
        }
      }
    }

    /* source: C:/Users/troy/OneDrive/Documents/projects/pico9918/src/res/splash.png
     * size  : 172px x 10px
     *       : 3440 bytes
     * format: 16bpp abgr image
     *
    if (y >= vBorder + TMS9918_PIXELS_Y + 12)
    {
      y -= vBorder + TMS9918_PIXELS_Y + 12;
      if (y < splashHeight)
      {
        uint16_t* splashPtr = splash + (y * splashWidth);
        for (int x = 4; x < 4 + splashWidth; ++x)
        {
          uint16_t c = *(splashPtr++);
          if (c & 0xf000)
          {
            pixels[x] = c;
          }
        }
      }
    }
    */

    return;
  }

  y -= vBorder;

  /*** left border ***/
  for (int x = 0; x < hBorder; ++x)
  {
    pixels[x] = bg;
  }

  /*** main display region ***/

  /* generate the scanline */
  uint8_t newStatus = vrEmuTms9918ScanLine(y, tmsScanlineBuffer, currentStatus) & 0x7f;

  //disableGpioInterrupts();
  if (currentInt) newStatus |= 0x80;
  vrEmuTms9918SetStatusImpl(newStatus);
  currentStatus = newStatus;
  updateTmsReadAhead();
  //currentStatus = (newStatus << GPIO_CD0) | GPIO_INT_MASK;
  //enableGpioInterrupts();

  /* convert from  palette to bgr12 */
  int tmsX = 0;
  if (tmsScanlineBuffer[0] & 0xf0)
  {
    for (int x = hBorder; x < hBorder + TMS9918_PIXELS_X * 2; x += 2, ++tmsX)
    {
      pixels[x] = tms9918PaletteBGR12[(tmsScanlineBuffer[tmsX] & 0xf0) >> 4];
      pixels[x + 1] = tms9918PaletteBGR12[tmsScanlineBuffer[tmsX] & 0x0f];
    }
  }
  else
  {
    for (int x = hBorder; x < hBorder + TMS9918_PIXELS_X * 2; x += 2, ++tmsX)
    {
      pixels[x] = tms9918PaletteBGR12[tmsScanlineBuffer[tmsX] & 0x0f];
      pixels[x + 1] = pixels[x];
    }
  }

  /*** right border ***/
  for (int x = hBorder + TMS9918_PIXELS_X * 2; x < VIRTUAL_PIXELS_X; ++x)
  {
    pixels[x] = bg;
  }

  /*** interrupt signal? ***/
  if (y == TMS9918_PIXELS_Y - 1)
  {
    //    disableGpioInterrupts();
    vrEmuTms9918InterruptSetImpl();
    currentInt = vrEmuTms9918InterruptStatusImpl();// ? 0 : GPIO_INT_MASK;
    currentStatus |= 0x80;
    updateTmsReadAhead();

    //    enableGpioInterrupts();
    gpio_put(GPIO_INT, !currentInt);
  }
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

  static uint clkSm = 2;//pio_claim_unused_sm(pio1, true);
  clock_program_init(pio0, clkSm, clocksPioOffset, gpio);

  float clockDiv = (float)PICO_CLOCK_HZ / (TMS_CRYSTAL_FREQ_HZ * 10.0f);

  pio_sm_set_clkdiv(pio0, clkSm, clockDiv);
  pio_sm_set_enabled(pio0, clkSm, true);

  pio_sm_put(pio0, clkSm, (uint)(PICO_CLOCK_HZ / clockDiv / (2.0f * freqHz)) - 3.0f);

  return clkSm++;
}

void tmsPioInit()
{
  uint tmsWriteProgram = pio_add_program(pio1, &tmsWrite_program);


  pio_sm_config writeConfig = tmsWrite_program_get_default_config(tmsWriteProgram);
  sm_config_set_in_pins(&writeConfig, GPIO_CD0);
  sm_config_set_in_shift(&writeConfig, false, true/*true*/, 16); // R shift, autopush @ 16 bits

  pio_sm_init(pio1, tmsWriteSm, tmsWriteProgram, &writeConfig);
  pio_sm_set_enabled(pio1, tmsWriteSm, true);

  uint tmsReadProgram = pio_add_program(pio1, &tmsRead_program);

  for (uint i = 0; i < 8; ++i)
  {
    pio_gpio_init(pio1, GPIO_CD0 + i);
  }

  pio_sm_config readConfig = tmsRead_program_get_default_config(tmsReadProgram);
  sm_config_set_in_pins(&readConfig, GPIO_CSR);
  sm_config_set_jmp_pin(&readConfig, GPIO_MODE);
  sm_config_set_out_pins(&readConfig, GPIO_CD0, 8);
  sm_config_set_in_shift(&readConfig, false, false, 32); // R shift, autopush @ 16 bits
  sm_config_set_out_shift(&readConfig, true, false, 32); // R shift, autopush @ 16 bits

  pio_sm_init(pio1, tmsReadSm, tmsReadProgram, &readConfig);
  pio_sm_set_enabled(pio1, tmsReadSm, true);

  irq_set_exclusive_handler(PIO1_IRQ_0, pio_irq_handler);
  //irq_set_exclusive_handler(PIO1_IRQ_1, pio_irq_handler1);
  irq_set_enabled(PIO1_IRQ_0, true);
  //irq_set_enabled(PIO1_IRQ_1, true);
  pio_set_irq0_source_enabled(pio1, pis_interrupt0, true);
  pio_set_irq0_source_enabled(pio1, pis_interrupt1, true);
  pio_interrupt_clear(pio1, 0);
  pio_interrupt_clear(pio1, 1);

  pio_sm_put(pio1, tmsReadSm, 0x000000ff);
}


/*
 * 2nd CPU core (proc1) entry
 */
void proc1Entry()
{
  // set up gpio pins
  gpio_init_mask(GPIO_CD_MASK | GPIO_CSR_MASK | GPIO_CSW_MASK | GPIO_MODE_MASK | GPIO_INT_MASK | GPIO_LED_MASK);
  gpio_put_all(GPIO_INT_MASK);
  gpio_set_dir_all_bits(GPIO_INT_MASK | GPIO_LED_MASK); // int is an output


  tmsPioInit();
  // set up gpio interrupts
  /*
  irq_set_priority(IO_IRQ_BANK0, 0);
  irq_set_exclusive_handler(IO_IRQ_BANK0, gpioExclusiveCallbackProc1);
  gpio_set_irq_enabled(GPIO_CSW, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
  gpio_set_irq_enabled(GPIO_CSR, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
  irq_set_enabled(IO_IRQ_BANK0, true);
  */

  // set up the GROMCLK and CPUCLK signals
  initClock(GPIO_GROMCL, TMS_CRYSTAL_FREQ_HZ / 24.0f);
  initClock(GPIO_CPUCL, TMS_CRYSTAL_FREQ_HZ / 3.0f);

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
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  //set_sys_clock_khz(315000, false);
  //set_sys_clock_khz(PICO_CLOCK_HZ / 1000, false);


  set_sys_clock_pll(1260000000, 5, 1);   // 252000
  //set_sys_clock_pll(1260000000, 4, 1);   // 315000
  //set_sys_clock_pll(1512000000, 5, 1);     // 302000

  /* we need one of these. it's the main guy */
  vrEmuTms9918Init();

  /* set up the GPIO pins and interrupt handler */


  multicore_launch_core1(proc1Entry);

  /* then set up VGA output */
  VgaInitParams params = { 0 };
  params.params = vgaGetParams(VGA_640_480_60HZ);

  /* virtual size will be 640 x 320 to accomodate 80-column mode */
  setVgaParamsScaleY(&params.params, 2);

  /* set vga scanline callback to generate tms9918 scanlines */
  params.scanlineFn = tmsScanline;

  vgaInit(params);

  /* signal proc1 that we're ready to start the display */
  multicore_fifo_push_timeout_us(0, 0);

  /* twiddle our thumbs - everything from this point on
     is handled by interrupts and PIOs */
  while (1)
  {
    tight_loop_contents();
  }

  return 0;
}