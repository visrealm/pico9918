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
#include "pico/binary_info.h"

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
#define PCB_MINOR_VERSION 3

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

//#define PICO_CLOCK_PLL 1260000000
//#define PICO_CLOCK_PLL_DIV1 5
//#define PICO_CLOCK_PLL_DIV2 1

#define PICO_CLOCK_PLL 1536000000
#define PICO_CLOCK_PLL_DIV1 3
#define PICO_CLOCK_PLL_DIV2 2

#define PICO_CLOCK_HZ (PICO_CLOCK_PLL / PICO_CLOCK_PLL_DIV1 / PICO_CLOCK_PLL_DIV2)

#define TMS_PIO pio1
#define TMS_IRQ PIO1_IRQ_0

bi_decl(bi_1pin_with_name(GPIO_GROMCL, "GROM Clock"));
bi_decl(bi_1pin_with_name(GPIO_CPUCL, "CPU Clock"));
bi_decl(bi_pin_mask_with_names(GPIO_CD_MASK, "CPU Data (CD7 - CD0)"));
bi_decl(bi_1pin_with_name(GPIO_CSR, "Read"));
bi_decl(bi_1pin_with_name(GPIO_CSW, "Write"));
bi_decl(bi_1pin_with_name(GPIO_MODE, "Mode"));
bi_decl(bi_1pin_with_name(GPIO_INT, "Interrupt"));


/* file globals */

static uint8_t nextValue = 0;     /* TMS9918A read-ahead value */
static bool currentInt = false;   /* current interrupt state */
static uint8_t currentStatus = 0x1f; /* current status register value */

static uint8_t __aligned(4) tmsScanlineBuffer[TMS9918_PIXELS_X];

const uint tmsWriteSm = 0;
const uint tmsReadSm = 1;

/*
 * update the value send to the read PIO
 */
inline static void updateTmsReadAhead()
{
  uint32_t readAhead = 0xff;              // pin direction
  readAhead |= nextValue << 8;
  readAhead |= currentStatus << 16;
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
      currentStatus = 0x1f;
      vrEmuTms9918SetStatusImpl(currentStatus);
      currentInt = false;
      gpio_put(GPIO_INT, !currentInt);
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

#if 0
  // better compile-time optimizations if we hard-code these
#define VIRTUAL_PIXELS_X 640
#define VIRTUAL_PIXELS_Y 240
#else 
#define VIRTUAL_PIXELS_X params->hVirtualPixels
#define VIRTUAL_PIXELS_Y params->vVirtualPixels
#endif


  const uint32_t vBorder = (VIRTUAL_PIXELS_Y - TMS9918_PIXELS_Y) / 2;
  const uint32_t hBorder = (VIRTUAL_PIXELS_X - TMS9918_PIXELS_X * 2) / 2;

  static int frameCount = 0;
  static int logoOffset = 100;

  uint16_t bg = tms9918PaletteBGR12[vrEmuTms9918RegValue(TMS_REG_FG_BG_COLOR) & 0x0f];

  /*** top and bottom borders ***/
  if (y < vBorder || y >= (vBorder + TMS9918_PIXELS_Y))
  {
    for (int x = 0; x < VIRTUAL_PIXELS_X; ++x)
    {
      pixels[x] = bg;
    }

    /* source: C:/Users/troy/OneDrive/Documents/projects/pico9918/src/res/splash.png
     * size  : 172px x 10px
     *       : 430 bytes
     * format: 16-bit abgr palette, 2bpp indexed image
     */
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
        y -= vBorder + TMS9918_PIXELS_Y + logoOffset;
        if (y < splashHeight)
        {
          uint8_t* splashPtr = splash + (y * splashWidth / 4);
          for (int x = 4; x < 4 + splashWidth; x += 4)
          {
            uint8_t c = *(splashPtr++);
            uint8_t p0 = (c & 0xc0);
            uint8_t p1 = (c & 0x30);
            uint8_t p2 = (c & 0x0c);
            uint8_t p3 = (c & 0x03);

            if (p0) { pixels[x] = splash_pal[(p0 >> 6)]; }
            if (p1) { pixels[x + 1] = splash_pal[(p1 >> 4)]; }
            if (p2) { pixels[x + 2] = splash_pal[(p2 >> 2)]; }
            if (p3) { pixels[x + 3] = splash_pal[p3]; }
          }
        }
      }
    }
    return;
  }

  y -= vBorder;

  /*** main display region ***/

  /* generate the scanline */
  uint8_t tempStatus = vrEmuTms9918ScanLine(y, tmsScanlineBuffer);

  /*** interrupt signal? ***/
  if (y == TMS9918_PIXELS_Y - 1)
  {
    tempStatus |= STATUS_INT;
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
  for (int x = 0; x < hBorder; ++x)
  {
    pixels[x] = bg;
  }

  /* convert from palette to bgr12 */
  int tmsX = 0;
  if (tms9918->mode == TMS_MODE_TEXT80)
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
      pixels[x] = tms9918PaletteBGR12[tmsScanlineBuffer[tmsX]];
      pixels[x + 1] = pixels[x];
    }
  }


  /*** right border ***/
  for (int x = hBorder + TMS9918_PIXELS_X * 2; x < VIRTUAL_PIXELS_X; ++x)
  {
    pixels[x] = bg;
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
  sm_config_set_clkdiv(&writeConfig, 1.0f);

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

  set_sys_clock_pll(PICO_CLOCK_PLL, PICO_CLOCK_PLL_DIV1, PICO_CLOCK_PLL_DIV2);   // 252000

  /* we need one of these. it's the main guy */
  vrEmuTms9918Init();

  /* launch core 1 which handles TMS9918<->CPU and rendering scanlines */
  multicore_launch_core1(proc1Entry);

  /* then set up VGA output */
  VgaInitParams params = { 0 };
  params.params = vgaGetParams(RGBS_NTSC_720_480i_60HZ);
  //params.params = vgaGetParams(VGA_640_480_60HZ);

  /* virtual size will be 640 x 320 to accomodate 80-column mode */
  //setVgaParamsScaleY(&params.params, 2);

  /* set vga scanline callback to generate tms9918 scanlines */
  params.scanlineFn = tmsScanline;

  vgaInit(params);

  /* signal proc1 that we're ready to start the display */
  multicore_fifo_push_blocking(0);

  /* twiddle our thumbs - everything from this point on
     is handled by interrupts and PIOs */;
  while (1)
  {
    tight_loop_contents();
  }

  return 0;
}
