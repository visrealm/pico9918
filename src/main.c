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
#include "pico/divider.h"
#include "pico/binary_info.h"

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/adc.h"

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

#define PCB_MAJOR_VERSION PICO9918_PCB_MAJOR_VER
#define PCB_MINOR_VERSION PICO9918_PCB_MINOR_VER

// compile-options to ease development between Jason and I
#ifndef PICO9918_NO_SPLASH
#define PICO9918_NO_SPLASH      0
#endif

#if !PICO9918_NO_SPLASH
#include "splash.h"
#endif


#if PICO9918_DIAG
  #include "font.h"  
#endif

#define TIMING_DIAG PICO9918_DIAG
#define REG_DIAG PICO9918_DIAG

#define GPIO_CD7 14
#define GPIO_CSR tmsRead_CSR_PIN  // defined in tms9918.pio
#define GPIO_CSW tmsWrite_CSW_PIN // defined in tms9918.pio
#define GPIO_MODE 28
#define GPIO_INT 22

#if PICO9918_PCB_MAJOR_VER != 0
#error "Time traveller? PICO9918_PCB_MAJOR_VER must be 0"
#endif

  // pin-mapping for gromclk and cpuclk changed in PCB v0.4
  // in order to have MODE and MODE1 sequential
#if PICO9918_PCB_MINOR_VER < 4
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

#ifdef GPIO_RESET
#define GPIO_RESET_MASK (0x01 << GPIO_RESET)
#else
#define GPIO_RESET_MASK 0
#endif

#define TMS_CRYSTAL_FREQ_HZ 10738635.0f

//#define PICO_CLOCK_PLL 756000000 // 252MHz - standard voltage
//#define PICO_CLOCK_PLL_DIV1 3

//#define PICO_CLOCK_PLL 828000000 // 276MHz - standard voltage
//#define PICO_CLOCK_PLL_DIV1 3

//#define PICO_CLOCK_PLL 1512000000 // 302.4MHz - standard voltage
//#define PICO_CLOCK_PLL_DIV1 5

//#define PICO_CLOCK_PLL 1308000000 // 327MHz - 1.15v
//#define PICO_CLOCK_PLL_DIV1 4

//#define PICO_CLOCK_PLL 984000000 // 328MHz - 1.15v
//#define PICO_CLOCK_PLL_DIV1 3

#define PICO_CLOCK_PLL 1056000000 // 352MHz - 1.3v
#define PICO_CLOCK_PLL_DIV1 3

//#define PICO_CLOCK_PLL 1128000000 // 376MHz - 1.3v
//#define PICO_CLOCK_PLL_DIV1 3

//#define PICO_CLOCK_PLL 1512000000 // 378MHz - 1.3v
//#define PICO_CLOCK_PLL_DIV1 4

//#define PICO_CLOCK_PLL 804000000 // 402MHz - DOES NOT WORK
//#define PICO_CLOCK_PLL_DIV1 2

//#define PICO_CLOCK_PLL 1212000000 // 404MHz - DOES NOT WORK
//#define PICO_CLOCK_PLL_DIV1 3

#define PICO_CLOCK_PLL_DIV2 1
#define PICO_CLOCK_HZ (PICO_CLOCK_PLL / PICO_CLOCK_PLL_DIV1 / PICO_CLOCK_PLL_DIV2)

bi_decl(bi_1pin_with_name(GPIO_GROMCL, "GROM Clock"));
bi_decl(bi_1pin_with_name(GPIO_CPUCL, "CPU Clock"));
bi_decl(bi_pin_mask_with_names(GPIO_CD_MASK, "CPU Data (CD7 - CD0)"));
bi_decl(bi_1pin_with_name(GPIO_CSR, "Read"));
bi_decl(bi_1pin_with_name(GPIO_CSW, "Write"));
bi_decl(bi_1pin_with_name(GPIO_MODE, "Mode"));
bi_decl(bi_1pin_with_name(GPIO_INT, "Interrupt"));

#ifdef GPIO_RESET
bi_decl(bi_1pin_with_name(GPIO_RESET, "Host Reset"));
bi_decl(bi_1pin_with_name(GPIO_MODE1, "Mode 1 (V9938)"));
#endif

#define TMS_PIO pio1
#define TMS_IRQ PIO1_IRQ_0

#define VIRTUAL_PIXELS_X 640
#define VIRTUAL_PIXELS_Y 240

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

static __attribute__((section(".scratch_y.buffer"))) uint32_t bg; 

static __attribute__((section(".scratch_x.buffer"))) uint8_t __aligned(4) tmsScanlineBuffer[TMS9918_PIXELS_X + 8];

const uint tmsWriteSm = 0;
const uint tmsReadSm = 1;
static int frameCount = 0;
static int logoOffset = 100;
static bool doneInt = false;  // interrupt raised this frame?

static const uint32_t dma32 = 2; // memset 32bit

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
      switch (tms9918->registers [0x0F] & 0x0F)
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
  irq_set_enabled(TMS_IRQ, true);
}

/*
 * disable gpio interrupts inline
 */
static inline void disableTmsPioInterrupts()
{
  irq_set_enabled(TMS_IRQ, false);
}

#ifdef GPIO_RESET

/*
 * handle reset pin going active (low)
 */
void __not_in_flash_func(gpioIrqHandler)()
{
  disableTmsPioInterrupts();
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
  logoOffset = 100;
  gpio_put(GPIO_INT, !currentInt);
  enableTmsPioInterrupts();

  gpio_acknowledge_irq(GPIO_RESET, GPIO_IRQ_EDGE_FALL);
}

#endif



static void eofInterrupt()
{
  doneInt = true;
  tms9918->status [0x01] |=  0x02;
  if (tms9918->registers[0x32] & 0x20)
  {
    gpuTrigger();
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

/* for diagnostics / statistics */
uint32_t renderTimeBcd = 0;
uint32_t frameTimeBcd = 0;
uint32_t renderTimePerScanlineBcd = 0;
uint32_t totalTimePerScanlineBcd = 0;

uint32_t accumulatedRenderTime = 0;
uint32_t accumulatedFrameTime = 0;
uint32_t accumulatedScanlines = 0;

/* convert an integer to bcd (two digits per byte)*/
static __attribute__ ((noinline)) uint32_t toBcd(uint32_t number)
{
  uint32_t result = 0;
  for (int i = 0; i < 8; ++i)
  {
    divmod_result_t dmResult = divmod_u32u32(number, 10);
    result |= to_remainder_u32(dmResult) << (i * 4);
    number = to_quotient_u32(dmResult);
  }
  return result;
}


/* F18A palette entries are big-endian 0x0RGB which looks like
   0xGB0R to our RP2040. our vga code is expecting 0x0BGR
   so we've got B and R correct by pure chance. just need to shift G
   over. this function does that. note: msbs are ignore so... */

inline uint32_t bigRgb2LittleBgr(uint32_t val)
{
  return val | ((val >> 12) << 4);
}

static void tmsPorch()
{
  tms9918->blanking = 1; // V
  tms9918->scanline = 255; // F18A value for vsync
  tms9918->status [0x03] = 255;
}

static void tmsEndOfFrame(uint32_t frameNumber)
{
  ++frameCount;
  //vgaCurrentParams()->scanlines = tms9918->registers[0x32] & 0x04;

#if TIMING_DIAG
  const uint32_t framesPerUpdate = 1 << 2;
  if ((frameCount & (framesPerUpdate - 1)) == 0)
  {
    renderTimeBcd = toBcd(accumulatedRenderTime / framesPerUpdate);
    frameTimeBcd = toBcd(accumulatedFrameTime / framesPerUpdate);
    renderTimePerScanlineBcd = toBcd(accumulatedRenderTime / accumulatedScanlines);
    totalTimePerScanlineBcd = toBcd(accumulatedFrameTime / accumulatedScanlines);
    accumulatedRenderTime = accumulatedFrameTime = accumulatedScanlines = 0;
  }
#endif

  // here, we catch the case where the last row(s) were
  // missed and we never raised an interrupt. do it now  
  if (!doneInt)
  {
    eofInterrupt();
    updateInterrupts(STATUS_INT);
  }
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
      if (frameCount < 200 && logoOffset > (22 - splashHeight)) --logoOffset;
      else if (frameCount > 500) ++logoOffset;
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

#if PICO9918_DIAG

uint32_t temperature = 0;

static void analogReadTempSetup() {
  adc_init();
  adc_set_temp_sensor_enabled(true);

#if PICO_RP2040
  adc_select_input(4); // Temperature sensor
#else
  adc_select_input(8); // RP2350 QFN80 package only... 
#endif
}

static float analogReadTemp(float vref) {
  int v = adc_read();
  float t = 27.0f - ((v * vref / 4096.0f) - 0.706f) / 0.001721f; // From the datasheet
  return t;
}

/* render a bcd value scanline */
void __attribute__ ((noinline))  renderBcd(uint16_t scanline, uint32_t bcd, uint16_t x, uint16_t y, uint16_t color, uint16_t* pixels)
{
  int fontY = scanline - y;
  if (fontY < 0 || fontY >= 7) return;

  uint32_t mask = 0xc0;
  if (fontY == 0) { mask = 0; } else { --fontY;}

  uint8_t *imgOffset = fontsubset + 16 * (6 + fontY);
  bool haveNonZero = false;
  for (int i = 0; i < 8; ++i)
  {
    int digit = (bcd >> (28 - (4 * i))) & 0xf;
    if (haveNonZero || digit)
    {
      haveNonZero = true;
      uint8_t digitBits = imgOffset[digit];
      for (int j = 0; j < 4; ++j)
      {
        pixels[x++] = (digitBits & mask) ? color : 0x000;
        digitBits <<= 2;
      }
    }
  }
  pixels[x++] = 0x000;
}

void renderDiagnostics(uint16_t y, uint16_t* pixels)
{
#if TIMING_DIAG   
  // Output average render time in microseconds (just vrEmuTms9918ScanLine())
  // We only have ~16K microseconds to do everything for an entire frame.
  // blanking takes around 10% of that, leaving ~14K microseconds
  // ROW30 mode will inflate this figure since... more scanlines 
  dma_channel_wait_for_finish_blocking(dma32);

  int xPos = 2;
  int yPos = 2;
  renderBcd(y, frameTimeBcd, xPos, yPos, 0x0f40, pixels);
  renderBcd(y, totalTimePerScanlineBcd, xPos + 28, yPos, 0x004f, pixels); yPos += 8;

  renderBcd(y, renderTimeBcd, xPos, yPos, 0x0f40, pixels);
  renderBcd(y, renderTimePerScanlineBcd, xPos + 28, yPos, 0x004f, pixels); yPos += 8;

  renderBcd(y, temperature, xPos, yPos, 0x004f, pixels);
#endif

#if REG_DIAG

  /* Register diagnostics. with this enabled, we overlay a binary 
   * representation of the VDP registers to the right-side of the screen */

  dma_channel_wait_for_finish_blocking(dma32);

  const int diagBitWidth = 4;
  const int diagBitSpacing = 2;
  const int diagRightMargin = 6;

  const int16_t diagBitOn = 0x2e2;
  const int16_t diagBitOff = 0x222;

  uint8_t reg = y / 3;
  if (reg < 64)
  {
    if (y % 3 != 0)
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

#endif

/*
 * generate a single VGA scanline (called by vgaLoop(), runs on proc1)
 */
static void __time_critical_func(tmsScanline)(uint16_t y, VgaParams* params, uint16_t* pixels)
{
  int vPixels = (tms9918->registers[0x31] & 0x40) ? 30 * 8 : 24 * 8;

  const uint32_t vBorder = (VIRTUAL_PIXELS_Y - vPixels) / 2;
  const uint32_t hBorder = (VIRTUAL_PIXELS_X - TMS9918_PIXELS_X * 2) / 2;

  uint32_t* dPixels = (uint32_t*)pixels;
  bg = bigRgb2LittleBgr(tms9918->pram[vrEmuTms9918RegValue(TMS_REG_FG_BG_COLOR) & 0x0f]);
  bg = bg | (bg << 16);

  if (y == 0)
  {
#if PICO9918_DIAG
    uint32_t t = (int)(analogReadTemp(3.3f) * 10.0f);
    uint32_t i = (t / 100);
    t -= (i * 100);
    temperature = (i << 12) | ((t / 10) << 8) | (0x0A << 4) | (t % 10);
#endif
    doneInt = false;
  }

  /*** top and bottom borders ***/
  if (y < vBorder || y >= (vBorder + vPixels))
  {
    dma_channel_set_write_addr(dma32, dPixels, false);
    dma_channel_set_trans_count(dma32, VIRTUAL_PIXELS_X / 2, true);
    tms9918->blanking = 1; // V
    if ((y >= vBorder + vPixels))
    {
      tms9918->scanline = y - vBorder;
      tms9918->status [0x03] = tms9918->scanline;
    }
    dma_channel_wait_for_finish_blocking(dma32);

#if PICO9918_DIAG
    renderDiagnostics(y, pixels);
#endif

    if (frameCount < 600)
    {
      outputSplash(y, vBorder, vPixels, pixels);
    }
    if (tms9918->registers[0x32] & 0x40)
    {
      gpuTrigger();
    }
    return;
  }

  uint32_t frameStart = time_us_32();

  y -= vBorder;
  tms9918->blanking = 0;
  tms9918->scanline = y;
  tms9918->status [0x03] = y;

  /*** left border ***/
  dma_channel_set_write_addr(dma32, dPixels, false);
  dma_channel_set_trans_count(dma32, hBorder / 2, true);

  /*** main display region ***/

  /* generate the scanline */
  uint32_t renderStart  = time_us_32();
  uint8_t tempStatus = vrEmuTms9918ScanLine(y, tmsScanlineBuffer);
  accumulatedRenderTime += time_us_32() - renderStart;
  ++accumulatedScanlines;

  /*** interrupt signal? ***/
  const int interruptThreshold = 1;
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
      uint32_t c = tms9918->pram[i] & 0xFF0F;
      for (int j = 0; j < 16; ++j, ++x)
      {
        data = ((tms9918->pram[j] & 0xFF0F) << 16) | c; data |= ((data >> 8) & 0x00f000f0); pram[x] = data;
      }
    }
    tms9918->blanking = 1; // H

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
      data = tms9918->pram [i + 0] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 0] = data | (data << 16);
      data = tms9918->pram [i + 1] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 1] = data | (data << 16);
      data = tms9918->pram [i + 2] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 2] = data | (data << 16);
      data = tms9918->pram [i + 3] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 3] = data | (data << 16);
      data = tms9918->pram [i + 4] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 4] = data | (data << 16);
      data = tms9918->pram [i + 5] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 5] = data | (data << 16);
      data = tms9918->pram [i + 6] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 6] = data | (data << 16);
      data = tms9918->pram [i + 7] & 0xFF0F; data = data | ((data >> 12) << 4); pram [i + 7] = data | (data << 16);
    }
    tms9918->blanking = 1; // H

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

  accumulatedFrameTime += time_us_32() - frameStart;

#if PICO9918_DIAG
    renderDiagnostics(y + vBorder, pixels);
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
  gpio_init_mask(GPIO_CD_MASK | GPIO_CSR_MASK | GPIO_CSW_MASK | GPIO_MODE_MASK | GPIO_INT_MASK | GPIO_RESET_MASK);
  gpio_put_all(GPIO_INT_MASK);
  gpio_set_dir_all_bits(GPIO_INT_MASK); // int is an output

  tmsPioInit();

  // set up the GROMCLK and CPUCLK signals
  initClock(GPIO_GROMCL, TMS_CRYSTAL_FREQ_HZ / 24.0f);
  initClock(GPIO_CPUCL, TMS_CRYSTAL_FREQ_HZ / 3.0f);

#ifdef GPIO_RESET
  // set up reset gpio interrupt handler
  irq_set_exclusive_handler(IO_IRQ_BANK0, gpioIrqHandler);
  gpio_set_irq_enabled(GPIO_RESET, GPIO_IRQ_EDGE_FALL, true);
  irq_set_enabled(IO_IRQ_BANK0, true);
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
   * that comes close to being divisible by 25.175MHz. 302.0 is close... enough :)
   * I do have code which sets the best clock baased on the chosen VGA mode,
   * but this'll do for now. */
  //vreg_set_voltage(VREG_VOLTAGE_1_15);
  vreg_set_voltage(VREG_VOLTAGE_1_30);
  set_sys_clock_pll(PICO_CLOCK_PLL, PICO_CLOCK_PLL_DIV1, PICO_CLOCK_PLL_DIV2);

  /* we need one of these. it's the main guy */
  vrEmuTms9918Init();

  /* launch core 1 which handles TMS9918<->CPU and rendering scanlines */
  multicore_launch_core1(proc1Entry);

  dma_channel_config cfg = dma_channel_get_default_config(dma32);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, true);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
  dma_channel_set_config(dma32, &cfg, false);

  dma_channel_set_read_addr(dma32, &bg, false);


  /* then set up VGA output */
  VgaInitParams params = { 0 };

#if PICO9918_SCART_RGBS
  #if PICO9918_SCART_PAL
    params.params = vgaGetParams(RGBS_PAL_720_576i_50HZ);
  #else
    params.params = vgaGetParams(RGBS_NTSC_720_480i_60HZ);
  #endif
#else // VGA
  params.params = vgaGetParams(VGA_640_480_60HZ);

  /* virtual size will be 640 x 240 */
  setVgaParamsScaleY(&params.params, 2);
#endif

  /* set vga scanline callback to generate tms9918 scanlines */
  params.scanlines = PICO9918_SCANLINES;
  params.scanlineFn = tmsScanline;
  params.endOfFrameFn = tmsEndOfFrame;
  params.porchFn = tmsPorch;

  const char *version = PICO9918_VERSION;

  vgaInit(params);

#if PICO9918_DIAG
  analogReadTempSetup();
#endif

  /* signal proc1 that we're ready to start the display */
  multicore_fifo_push_blocking(0);

  /* initialize the GPU */
  gpuInit();

  /* pass control of core0 over to the TMS9900 GPU */
  gpuLoop();

  return 0;
}
