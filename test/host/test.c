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

 /*
  * External pins
  *
  * Pin  | GPIO | Name   | TMS9918A Pin
  * -----+------+--------+-------------
  *  19  |  14  |  CD0   |  24
  *  20  |  15  |  CD1   |  23
  *  21  |  16  |  CD2   |  22
  *  22  |  17  |  CD3   |  21
  *  24  |  18  |  CD4   |  20
  *  25  |  19  |  CD5   |  19
  *  26  |  20  |  CD6   |  18
  *  27  |  21  |  CD7   |  17
  *  29  |  22  |  /INT  |  16
  *  30  |  RUN |  RST   |  34
  *  31  |  26  |  /CSR  |  15
  *  32  |  27  |  /CSW  |  14
  *  34  |  28  |  MODE  |  13
  */

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "clocks.pio.h"

#include "breakout.h"



  //#include <stddef>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>


extern const uint8_t tmsFont[];
extern size_t tmsFontBytes;

#define GPIO_GROMCL 0
#define GPIO_CPUCL 1

#define GPIO_CD0 14
#define GPIO_CSR 26
#define GPIO_CSW 27
#define GPIO_MODE 28
#define GPIO_INT 22

#define GPIO_CD_MASK (0xff << GPIO_CD0)
#define GPIO_CSR_MASK (0x01 << GPIO_CSR)
#define GPIO_CSW_MASK (0x01 << GPIO_CSW)
#define GPIO_MODE_MASK (0x01 << GPIO_MODE)
#define GPIO_INT_MASK (0x01 << GPIO_INT)

#define TMS_CRYSTAL_FREQ_HZ 10738635.0f


/* todo should I make this uint32_t and shift the bits too?*/
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

uint32_t buildGpioState(bool read, bool write, bool mode, uint8_t value)
{
  return (read ? 0 : GPIO_CSR_MASK) |
    (write ? 0 : GPIO_CSW_MASK) |
    (mode ? GPIO_MODE_MASK : 0) |
    (REVERSE(value) << GPIO_CD0);
}

int del = 0;
int del2 = 0;
void doFn(uint8_t value)
{
  --del;
  del2 += value;
  del2 *= value;
  del2 -= value & 0xff;
  del2 ^= value;
  del2 += value;
  del2 *= value;
  del2 -= value & 0xff;
  del2 ^= value;
}

void writeTo9918(bool mode, uint8_t value)
{
  gpio_set_dir_out_masked(GPIO_CD_MASK);
  gpio_put_all(buildGpioState(false, true, mode, value));
  sleep_us(0);
  //  del = 12;
  //  while (del) doFn(value);
    //for (del = 0; del < 50; ++del);
  gpio_put_all(buildGpioState(false, false, mode, value));

  //del = 8;
  //while (del) doFn(value);

  sleep_us(0);
  //gpio_set_dir_in_masked(GPIO_CD_MASK);
}

uint8_t readFrom9918(bool mode)
{
  gpio_set_dir_in_masked(GPIO_CD_MASK);
  gpio_put_all(buildGpioState(true, false, mode, 0));
  sleep_us(0);
  uint8_t value = REVERSE((gpio_get_all() >> GPIO_CD0) & 0xff);
  gpio_put_all(buildGpioState(false, false, mode, value));
  sleep_us(0);
  return value;
}



typedef enum
{
  TMS_MODE_GRAPHICS_I,
  TMS_MODE_GRAPHICS_II,
  TMS_MODE_TEXT,
  TMS_MODE_MULTICOLOR,
} vrEmuTms9918Mode;

typedef enum
{
  TMS_TRANSPARENT = 0,
  TMS_BLACK,
  TMS_MED_GREEN,
  TMS_LT_GREEN,
  TMS_DK_BLUE,
  TMS_LT_BLUE,
  TMS_DK_RED,
  TMS_CYAN,
  TMS_MED_RED,
  TMS_LT_RED,
  TMS_DK_YELLOW,
  TMS_LT_YELLOW,
  TMS_DK_GREEN,
  TMS_MAGENTA,
  TMS_GREY,
  TMS_WHITE,
} vrEmuTms9918Color;

typedef enum
{
  TMS_REG_0 = 0,
  TMS_REG_1,
  TMS_REG_2,
  TMS_REG_3,
  TMS_REG_4,
  TMS_REG_5,
  TMS_REG_6,
  TMS_REG_7,
  TMS_NUM_REGISTERS,
  TMS_REG_NAME_TABLE = TMS_REG_2,
  TMS_REG_COLOR_TABLE = TMS_REG_3,
  TMS_REG_PATTERN_TABLE = TMS_REG_4,
  TMS_REG_SPRITE_ATTR_TABLE = TMS_REG_5,
  TMS_REG_SPRITE_PATT_TABLE = TMS_REG_6,
  TMS_REG_FG_BG_COLOR = TMS_REG_7,
} vrEmuTms9918Register;

#define TMS9918_PIXELS_X 256
#define TMS9918_PIXELS_Y 192


#define TMS_R0_MODE_GRAPHICS_I    0x00
#define TMS_R0_MODE_GRAPHICS_II   0x02
#define TMS_R0_MODE_MULTICOLOR    0x00
#define TMS_R0_MODE_TEXT          0x00
#define TMS_R0_EXT_VDP_ENABLE     0x01
#define TMS_R0_EXT_VDP_DISABLE    0x00

#define TMS_R1_RAM_16K            0x80
#define TMS_R1_RAM_4K             0x00
#define TMS_R1_DISP_BLANK         0x00
#define TMS_R1_DISP_ACTIVE        0x40
#define TMS_R1_INT_ENABLE         0x20
#define TMS_R1_INT_DISABLE        0x00
#define TMS_R1_MODE_GRAPHICS_I    0x00
#define TMS_R1_MODE_GRAPHICS_II   0x00
#define TMS_R1_MODE_MULTICOLOR    0x08
#define TMS_R1_MODE_TEXT          0x10
#define TMS_R1_SPRITE_8           0x00
#define TMS_R1_SPRITE_16          0x02
#define TMS_R1_SPRITE_MAG1        0x00
#define TMS_R1_SPRITE_MAG2        0x01

#define TMS_DEFAULT_VRAM_NAME_ADDRESS          0x3800
#define TMS_DEFAULT_VRAM_COLOR_ADDRESS         0x0000
#define TMS_DEFAULT_VRAM_PATT_ADDRESS          0x2000
#define TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS   0x3B00
#define TMS_DEFAULT_VRAM_SPRITE_PATT_ADDRESS   0x1800


struct vrEmuTMS9918_s;
typedef struct vrEmuTMS9918_s VrEmuTms9918;


/* PUBLIC INTERFACE
 * ---------------------------------------- */

 /* Function:  vrEmuTms9918New
  * --------------------
  * create a new TMS9918
  */
VrEmuTms9918* vrEmuTms9918New()
{
  gpio_init_mask(GPIO_CD_MASK | GPIO_CSR_MASK | GPIO_CSW_MASK | GPIO_MODE_MASK | GPIO_INT_MASK);

  gpio_set_pulls(GPIO_CSW, true, false);
  gpio_set_pulls(GPIO_CSR, true, false);

  gpio_put_all(GPIO_CSR_MASK | GPIO_CSW_MASK | GPIO_MODE_MASK); // drive r, w, mode high
  gpio_set_dir_all_bits(GPIO_CSR_MASK | GPIO_CSW_MASK | GPIO_MODE_MASK); // set r, w, mode to outputs

  return NULL;
}

/* Function:  vrEmuTms9918Reset
  * --------------------
  * reset the new TMS9918
  */
void vrEmuTms9918Reset(VrEmuTms9918* tms9918)
{
}

/* Function:  vrEmuTms9918Destroy
 * --------------------
 * destroy a TMS9918
 *
 * tms9918: tms9918 object to destroy / clean up
 */
void vrEmuTms9918Destroy(VrEmuTms9918* tms9918)
{

}

/* Function:  vrEmuTms9918WriteAddr
 * --------------------
 * write an address (mode = 1) to the tms9918
 *
 * uint8_t: the data (DB0 -> DB7) to send
 */
void vrEmuTms9918WriteAddr(VrEmuTms9918* tms9918, uint8_t data)
{
  writeTo9918(true, data);
}

/* Function:  vrEmuTms9918WriteData
 * --------------------
 * write data (mode = 0) to the tms9918
 *
 * uint8_t: the data (DB0 -> DB7) to send
 */
void vrEmuTms9918WriteData(VrEmuTms9918* tms9918, uint8_t data)
{
  writeTo9918(false, data);
}

/* Function:  vrEmuTms9918ReadStatus
 * --------------------
 * read from the status register
 */
uint8_t vrEmuTms9918ReadStatus(VrEmuTms9918* tms9918)
{
  return readFrom9918(true);
}

/* Function:  vrEmuTms9918ReadData
 * --------------------
 * read data (mode = 0) from the tms9918
 */
uint8_t vrEmuTms9918ReadData(VrEmuTms9918* tms9918)
{
  return readFrom9918(false);
}


/*
 * Write a register value
 */
inline static void vrEmuTms9918WriteRegisterValue(VrEmuTms9918* tms9918, vrEmuTms9918Register reg, uint8_t value)
{
  vrEmuTms9918WriteAddr(tms9918, value);
  vrEmuTms9918WriteAddr(tms9918, 0x80 | (uint8_t)reg);
}


/*
 * Write a series of bytes to the VRAM
 */
inline static void vrEmuTms9918WriteBytes(VrEmuTms9918* tms9918, const uint8_t* bytes, size_t numBytes)
{
  for (size_t i = 0; i < numBytes; ++i)
  {
    vrEmuTms9918WriteData(tms9918, bytes[i]);
  }
}


/*
 * Set current VRAM address for reading
 */
inline static void vrEmuTms9918SetAddressRead(VrEmuTms9918* tms9918, uint16_t addr)
{
  vrEmuTms9918WriteAddr(tms9918, addr & 0x00ff);
  vrEmuTms9918WriteAddr(tms9918, ((addr & 0xff00) >> 8));
}


/*
 * Set current VRAM address for writing
 */
inline static void vrEmuTms9918SetAddressWrite(VrEmuTms9918* tms9918, uint16_t addr)
{
  vrEmuTms9918SetAddressRead(tms9918, addr | 0x4000);
}



/*
 * Return a colur byte consisting of foreground and background colors
 */
inline static uint8_t vrEmuTms9918FgBgColor(vrEmuTms9918Color fg, vrEmuTms9918Color bg)
{
  return (uint8_t)((uint8_t)fg << 4) | (uint8_t)bg;
}

/*
 * Set name table address
 */
inline static void vrEmuTms9918SetNameTableAddr(VrEmuTms9918* tms9918, uint16_t addr)
{
  vrEmuTms9918WriteRegisterValue(tms9918, TMS_REG_NAME_TABLE, addr >> 10);
}

/*
 * Set color table address
 */
inline static void vrEmuTms9918SetColorTableAddr(VrEmuTms9918* tms9918, uint16_t addr)
{
  vrEmuTms9918WriteRegisterValue(tms9918, TMS_REG_COLOR_TABLE, (uint8_t)(addr >> 6));
}

/*
 * Set pattern table address
 */
inline static void vrEmuTms9918SetPatternTableAddr(VrEmuTms9918* tms9918, uint16_t addr)
{
  vrEmuTms9918WriteRegisterValue(tms9918, TMS_REG_PATTERN_TABLE, addr >> 11);
}

/*
 * Set sprite attribute table address
 */
inline static void vrEmuTms9918SetSpriteAttrTableAddr(VrEmuTms9918* tms9918, uint16_t addr)
{
  vrEmuTms9918WriteRegisterValue(tms9918, TMS_REG_SPRITE_ATTR_TABLE, (uint8_t)(addr >> 7));
}

/*
 * Set sprite pattern table address
 */
inline static void vrEmuTms9918SetSpritePattTableAddr(VrEmuTms9918* tms9918, uint16_t addr)
{
  vrEmuTms9918WriteRegisterValue(tms9918, TMS_REG_SPRITE_PATT_TABLE, addr >> 11);
}

/*
 * Set foreground (text mode) and background colors
 */
inline static void vrEmuTms9918SetFgBgColor(VrEmuTms9918* tms9918, vrEmuTms9918Color fg, vrEmuTms9918Color bg)
{
  vrEmuTms9918WriteRegisterValue(tms9918, TMS_REG_FG_BG_COLOR, vrEmuTms9918FgBgColor(fg, bg));
}



void vrEmuTms9918InitialiseGfxII(VrEmuTms9918* tms9918)
{
  vrEmuTms9918WriteRegisterValue(tms9918, TMS_REG_0, TMS_R0_EXT_VDP_DISABLE | TMS_R0_MODE_GRAPHICS_II);
  vrEmuTms9918WriteRegisterValue(tms9918, TMS_REG_1, TMS_R1_RAM_16K | TMS_R1_MODE_GRAPHICS_II | TMS_R1_RAM_16K | TMS_R1_DISP_ACTIVE | TMS_R1_INT_ENABLE | TMS_R1_SPRITE_MAG2);
  vrEmuTms9918SetNameTableAddr(tms9918, TMS_DEFAULT_VRAM_NAME_ADDRESS);

  /* in Graphics II, Registers 3 and 4 work differently
   *
   * reg3 - Color table
   *   0x7f = 0x0000
   *   0xff = 0x2000
   *
   * reg4 - Pattern table
   *  0x03 = 0x0000
   *  0x07 = 0x2000
   */

  vrEmuTms9918WriteRegisterValue(tms9918, TMS_REG_COLOR_TABLE, 0x7f);
  vrEmuTms9918WriteRegisterValue(tms9918, TMS_REG_PATTERN_TABLE, 0x07);

  vrEmuTms9918SetSpriteAttrTableAddr(tms9918, TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS);
  vrEmuTms9918SetSpritePattTableAddr(tms9918, TMS_DEFAULT_VRAM_SPRITE_PATT_ADDRESS);
  vrEmuTms9918SetFgBgColor(tms9918, TMS_BLACK, TMS_CYAN);

  vrEmuTms9918SetAddressWrite(tms9918, TMS_DEFAULT_VRAM_NAME_ADDRESS);
  for (int i = 0; i < 768; ++i)
  {
    vrEmuTms9918WriteData(tms9918, i & 0xff);
  }

}



VrEmuTms9918* tms = NULL;


void animateSprites(uint64_t frameNumber)
{
  for (int i = 0; i < 16; ++i)
  {
    float x = sin(frameNumber / 20.0f + i / 3.0f);

    vrEmuTms9918SetAddressWrite(tms, TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS + (8 * i) + 4);
    uint8_t yPos = (frameNumber / 2 + i * 10 + 24);
    if (yPos == 0xd0) ++yPos;
    vrEmuTms9918WriteData(tms, yPos);
    vrEmuTms9918WriteData(tms, 128 - 8 + (x * 80.0f));

    vrEmuTms9918SetAddressWrite(tms, TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS + (8 * i));
    if (yPos - 2 == 0xd0) ++yPos;
    vrEmuTms9918WriteData(tms, yPos - 2);
    vrEmuTms9918WriteData(tms, 128 - 8 + (x * 80.0f) - 2);


    vrEmuTms9918SetAddressRead(tms, TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS + (8 * i) + 2);
    char c = vrEmuTms9918ReadData(tms);
    vrEmuTms9918SetAddressWrite(tms, TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS + (8 * i) + 2);
    vrEmuTms9918WriteData(tms, c);

  }
}

int i = 0;

void onTms9918Interrupt()
{
  vrEmuTms9918SetFgBgColor(tms, TMS_WHITE, TMS_CYAN);
  animateSprites(++i);
  vrEmuTms9918SetFgBgColor(tms, TMS_WHITE, TMS_BLACK);

  vrEmuTms9918ReadStatus(tms);   // clear the interrupt
}


int main(void)
{
  set_sys_clock_khz(252000, false);

  uint clocksPioOffset = pio_add_program(pio0, &clock_program);

  uint gromClkSm = pio_claim_unused_sm(pio0, true);
  uint cpuClkSm = pio_claim_unused_sm(pio0, true);

  clock_program_init(pio0, gromClkSm, clocksPioOffset, GPIO_GROMCL);
  clock_program_init(pio0, cpuClkSm, clocksPioOffset, GPIO_CPUCL);

  float clockDiv = (float)clock_get_hz(clk_sys) / (TMS_CRYSTAL_FREQ_HZ * 10.0f);

  pio_sm_set_clkdiv(pio0, gromClkSm, clockDiv);
  pio_sm_set_clkdiv(pio0, cpuClkSm, clockDiv);

  pio_sm_set_enabled(pio0, gromClkSm, true);
  pio_sm_set_enabled(pio0, cpuClkSm, true);

  const float gromClkFreq = TMS_CRYSTAL_FREQ_HZ / 24.0f;
  const float cpuClkFreq = TMS_CRYSTAL_FREQ_HZ / 3.0f;

  pio_sm_put(pio0, gromClkSm, (uint)(clock_get_hz(clk_sys) / clockDiv / (2.0f * gromClkFreq)) - 3.0f);
  pio_sm_put(pio0, cpuClkSm, (uint)(clock_get_hz(clk_sys) / clockDiv / (2.0f * cpuClkFreq)) - 3.0f);

  tms = vrEmuTms9918New();

  sleep_ms(50);

  vrEmuTms9918ReadStatus(tms);

  vrEmuTms9918InitialiseGfxII(tms);

  //while ((vrEmuTms9918ReadStatus(tms) & 0x80) == 0)
//    sleep_ms(10);

  vrEmuTms9918SetFgBgColor(tms, TMS_WHITE, TMS_BLACK);

  vrEmuTms9918SetAddressWrite(tms, TMS_DEFAULT_VRAM_SPRITE_PATT_ADDRESS + 32 * 8);
  vrEmuTms9918WriteBytes(tms, tmsFont, tmsFontBytes);

  vrEmuTms9918SetAddressWrite(tms, TMS_DEFAULT_VRAM_COLOR_ADDRESS);
  vrEmuTms9918WriteBytes(tms, BREAKOUT_TIAC, 6144);
  vrEmuTms9918SetAddressWrite(tms, TMS_DEFAULT_VRAM_PATT_ADDRESS);
  vrEmuTms9918WriteBytes(tms, BREAKOUT_TIAP, 6144);

  vrEmuTms9918SetAddressWrite(tms, TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS);
  const char* str = "Hello, World!";
  const int strLen = strlen(str);

  for (int i = 0; i < strLen; ++i)
  {
    vrEmuTms9918WriteData(tms, i * 10 + 24 - 2);
    vrEmuTms9918WriteData(tms, i * 10 - 2);
    vrEmuTms9918WriteData(tms, str[strLen - (i + 1)]);
    vrEmuTms9918WriteData(tms, i + 2);

    vrEmuTms9918WriteData(tms, i * 10 + 24);
    vrEmuTms9918WriteData(tms, i * 10);
    vrEmuTms9918WriteData(tms, str[strLen - (i + 1)]);
    vrEmuTms9918WriteData(tms, 1);
  }

  for (int i = strLen; i < 16; ++i)
  {
    vrEmuTms9918WriteData(tms, 0xd0);
    vrEmuTms9918WriteData(tms, 0x0);
    vrEmuTms9918WriteData(tms, 0x0);
    vrEmuTms9918WriteData(tms, 0x0);

    vrEmuTms9918WriteData(tms, 0xd2);
    vrEmuTms9918WriteData(tms, 0x0);
    vrEmuTms9918WriteData(tms, 0x0);
    vrEmuTms9918WriteData(tms, 0x0);
  }
  //  }

  gpio_set_irq_enabled_with_callback(GPIO_INT, GPIO_IRQ_EDGE_FALL, true, onTms9918Interrupt);

  while (1)
  {
    tight_loop_contents();
  }

  vrEmuTms9918Destroy(tms);

  return 0;
}
