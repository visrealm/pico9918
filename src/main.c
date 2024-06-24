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

#define GPIO_CD_REVERSED 0  /* unset for v0.3 PCB */
#define LED_BLINK_ON_WRITE 0


#if GPIO_CD_REVERSED

  /* In revision 0.2 of my prototype PCB, CD0 through CD7 are inconveniently reversed into the
     Pi Pico GPIO pins. Quickest way to deal with that is this lookup table.
     v0.3+ of the pico9918 hardware will have this fix in hardware, so will be removed soon
   */

static uint8_t  __aligned(4) reversed[] =
{
  0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
  0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
  0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
  0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
  0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
  0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
  0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
  0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
  0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
  0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
  0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
  0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
  0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
  0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
  0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
  0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};
#define REVERSE(x) reversed[x]
#else
#define REVERSE(x) x
#endif

  /* file globals */

static VrEmuTms9918* tms = NULL;  /* our vrEmuTms9918 instance handle */
static uint32_t nextValue = 0; /* TMS9918A read-ahead value */
static uint32_t currentInt = GPIO_INT_MASK; /* current interrupt pin state */

static uint8_t __aligned(4) tmsScanlineBuffer[TMS9918_PIXELS_X];

/*
 * RP2040 exclusive GPIO interrupt callback for PROC1
 * Called whenever CSR or CSW changes and reads or writes the data
 *
 * Note: This needs to be extremely responsive. No dilly-dallying here.
 */
void __time_critical_func(gpioExclusiveCallbackProc1)()
{
  uint32_t gpios = sio_hw->gpio_in;

  /* interrupt handled */
  iobank0_hw->intr[GPIO_CSR >> 3u] = iobank0_hw->proc1_irq_ctrl.ints[GPIO_CSR >> 3u];

  if ((gpios & GPIO_CSR_MASK) == 0) /* read? */
  {
    sio_hw->gpio_oe_set = GPIO_CD_MASK;

    if (gpios & GPIO_MODE_MASK) /* read status register */
    {
      sio_hw->gpio_out = ((uint32_t)REVERSE(vrEmuTms9918ReadStatus(tms)) << GPIO_CD0) | currentInt;
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
    uint8_t value = REVERSE((sio_hw->gpio_in >> GPIO_CD0) & 0xff);

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

  /* update read-ahead */
  nextValue = REVERSE(vrEmuTms9918ReadDataNoInc(tms)) << GPIO_CD0;
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
  irq_set_mask_enabled(1u << IO_IRQ_BANK0, false);
  uint8_t status = vrEmuTms9918PeekStatus(tms);
  irq_set_mask_enabled(1u << IO_IRQ_BANK0, true);

  /* generate the scanline */
  status = vrEmuTms9918ScanLine(tms, y, tmsScanlineBuffer, status);

  irq_set_mask_enabled(1u << IO_IRQ_BANK0, false);
  vrEmuTms9918SetStatus(tms, (status & 0x7f) | (currentInt ? 0 : 0x80));
  irq_set_mask_enabled(1u << IO_IRQ_BANK0, true);

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
      pixels[x] = pixels[x + 1] = tms9918PaletteBGR12[tmsScanlineBuffer[tmsX] & 0x0f];
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
    irq_set_mask_enabled(1u << IO_IRQ_BANK0, false);
    vrEmuTms9918InterruptSet(tms);
    currentInt = vrEmuTms9918InterruptStatus(tms) ? 0 : GPIO_INT_MASK;
    gpio_put(GPIO_INT, !!currentInt);
    irq_set_mask_enabled(1u << IO_IRQ_BANK0, true);
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