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

#include "palette.h"

#include "splash.h"

#include "vrEmuTms9918Util.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"

#include <stdlib.h>

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

  /* file globals */

static VrEmuTms9918* tms = NULL;  /* our vrEmuTms9918 instance handle */
static uint32_t nextValue = 0; /* TMS9918A read-ahead value */
static uint32_t currentInt = GPIO_INT_MASK; /* current interrupt pin state */
static uint32_t currentStatus = 0; /* current status register value */

static uint8_t __aligned(4) tmsScanlineBuffer[TMS9918_PIXELS_X];


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
 * RP2040 exclusive GPIO interrupt callback for PROC1
 * Called whenever CSR or CSW changes and reads or writes the data
 *
 * Note: This needs to be extremely responsive. No dilly-dallying here.
 */
void __time_critical_func(gpioExclusiveCallbackProc1)()
{
  uint32_t gpios = sio_hw->gpio_in;

  if ((gpios & GPIO_CSR_MASK) == 0) /* read? */
  {
    sio_hw->gpio_oe_set = GPIO_CD_MASK;

    if (gpios & GPIO_MODE_MASK) /* read status register */
    {
      sio_hw->gpio_out = currentStatus | currentInt;
      currentStatus = vrEmuTms9918ReadStatus(tms) << GPIO_CD0;
      currentInt = GPIO_INT_MASK;
    }
    else /* read data */
    {
      sio_hw->gpio_out = nextValue | currentInt;
      vrEmuTms9918ReadData(tms);
    }
  }
  else if ((gpios & GPIO_CSW_MASK) == 0)  /* write? */
  {
    uint8_t value = gpios >> GPIO_CD0;

    if (gpios & GPIO_MODE_MASK) /* write register/address */
    {
      vrEmuTms9918WriteAddr(tms, value);

      currentInt = vrEmuTms9918InterruptStatus(tms) ? 0 : GPIO_INT_MASK;
      sio_hw->gpio_out = nextValue | currentInt;
    }
    else /* write data */
    {
      vrEmuTms9918WriteData(tms, value);

#if LED_BLINK_ON_WRITE
      sio_hw->gpio_out = GPIO_LED_MASK | currentInt;
#endif
    }
  }
  else /* both CSR and CSW are high (inactive). Go High-Z */
  {
    sio_hw->gpio_oe_clr = GPIO_CD_MASK;
    sio_hw->gpio_out = currentInt;
  }

  /* interrupt handled */
  iobank0_hw->intr[GPIO_CSR >> 3u] = iobank0_hw->proc1_irq_ctrl.ints[GPIO_CSR >> 3u];

  /* update read-ahead */
  nextValue = vrEmuTms9918ReadDataNoInc(tms) << GPIO_CD0;
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

  // ensure CSR and CSW are high (inactive)
  while (!gpio_get(GPIO_CSW) || !gpio_get(GPIO_CSR))
    tight_loop_contents();

  // set up gpio interrupts
  irq_set_exclusive_handler(IO_IRQ_BANK0, gpioExclusiveCallbackProc1);
  gpio_set_irq_enabled(GPIO_CSW, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
  gpio_set_irq_enabled(GPIO_CSR, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
  irq_set_enabled(IO_IRQ_BANK0, true);

  // wait until everything else is ready, then run the vga loop
  multicore_fifo_pop_blocking();
  vgaLoop();
}

/*
 * generate a single VGA scanline (called by vgaLoop(), runs on proc1)
 */
static void __time_critical_func(tmsScanline)(uint16_t y, VgaParams* params, uint16_t* pixels)
{

#if 1
#define VIRTUAL_PIXELS_X 640
#define VIRTUAL_PIXELS_Y 240
#else 
#define VIRTUAL_PIXELS_X params->hVirtualPixels
#define VIRTUAL_PIXELS_Y params->vVirtualPixels
#endif


  const uint32_t vBorder = (VIRTUAL_PIXELS_Y - TMS9918_PIXELS_Y) / 2;
  const uint32_t hBorder = (VIRTUAL_PIXELS_X - TMS9918_PIXELS_X * 2) / 2;

  uint16_t bg = tms9918PaletteBGR12[vrEmuTms9918RegValue(tms, TMS_REG_FG_BG_COLOR) & 0x0f];

  /*** top and bottom borders ***/
  if (y < vBorder || y >= (vBorder + TMS9918_PIXELS_Y))
  {
    for (int x = 0; x < VIRTUAL_PIXELS_X; ++x)
    {
      pixels[x] = bg;
    }


    /* source: C:/Users/troy/OneDrive/Documents/projects/pico9918/src/res/splash.png
     * size  : 172px x 10px
     *       : 3440 bytes
     * format: 16bpp abgr image
     */
    if (y >= vBorder + TMS9918_PIXELS_Y + 12)
    {
      y -= vBorder + TMS9918_PIXELS_Y + 12;
      if (y < 10)
      {
        uint16_t* splashPtr = splash + (y * 172);
        for (int x = 4; x < 4 + 172; ++x)
        {
          uint16_t c = *(splashPtr++);
          if (c & 0xf000)
          {
            pixels[x] = c;
          }
        }
      }
    }

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
  uint8_t newStatus = vrEmuTms9918ScanLine(tms, y, tmsScanlineBuffer, currentStatus >> GPIO_CD0) & 0x7f;

  disableGpioInterrupts();
  if (!currentInt) newStatus |= 0x80;
  vrEmuTms9918SetStatus(tms, newStatus);
  currentStatus = newStatus << GPIO_CD0;
  enableGpioInterrupts();



  /* convert from tms palette to bgr12 */
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
    disableGpioInterrupts();
    vrEmuTms9918InterruptSet(tms);
    currentInt = vrEmuTms9918InterruptStatus(tms) ? 0 : GPIO_INT_MASK;
    currentStatus |= 0x80 << GPIO_CD0;
    gpio_put(GPIO_INT, !!currentInt);
    enableGpioInterrupts();
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
    clocksPioOffset = pio_add_program(pio1, &clock_program);
  }

  uint clkSm = pio_claim_unused_sm(pio1, true);
  clock_program_init(pio1, clkSm, clocksPioOffset, gpio);

  float clockDiv = (float)clock_get_hz(clk_sys) / (TMS_CRYSTAL_FREQ_HZ * 10.0f);

  pio_sm_set_clkdiv(pio1, clkSm, clockDiv);
  pio_sm_set_enabled(pio1, clkSm, true);

  pio_sm_put(pio1, clkSm, (uint)(clock_get_hz(clk_sys) / clockDiv / (2.0f * freqHz)) - 3.0f);

  return clkSm;
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
  set_sys_clock_khz(252000, false);

  /* we need one of these. it's the main guy */
  tms = vrEmuTms9918New();

  /* set up the GPIO pins and interrupt handler */
  multicore_launch_core1(proc1Entry);

  /* set up the GROMCLK and CPUCLK signals */
  initClock(GPIO_GROMCL, TMS_CRYSTAL_FREQ_HZ / 24.0f);
  initClock(GPIO_CPUCL, TMS_CRYSTAL_FREQ_HZ / 3.0f);

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