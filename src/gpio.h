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

#pragma once

 /*
  * Pin mapping
  *
  * Pico Pin  | GPIO (v0.3) | GPIO (v0.4+) | Name      | TMS9918A Pin
  * ----------+-------------+--------------+-----------+-------------
  *     19    |     14      |     14       |  CD7      |  24
  *     20    |     15      |     15       |  CD6      |  23
  *     21    |     16      |     16       |  CD5      |  22
  *     22    |     17      |     17       |  CD4      |  21
  *     24    |     18      |     18       |  CD3      |  20
  *     25    |     19      |     19       |  CD2      |  19
  *     26    |     20      |     20       |  CD1      |  18
  *     27    |     21      |     21       |  CD0      |  17
  *     29    |     22      |     22       |  /INT     |  16
  *     30    |     RUN     |     23       |  RST      |  34
  *     31    |     26      |     26       |  /CSR     |  15
  *     32    |     27      |     27       |  /CSW     |  14
  *     34    |     28      |     28       |  MODE     |  13
  *     --    |     --      |     29       |  MODE 1   |  --
  *     35    |     29      |     25       |  GROMCLK  |  37
  *     37    |     23      |     24       |  CPUCLK   |  38
  *
  *
  * Note: Due to GROMCLK and CPUCLK using GPIO23 and GPIO29
  *       a genuine Raspberry Pi Pico can't be used.
  *       v0.3 of the PCB is designed for the DWEII?
  *       RP2040 USB-C module which exposes these additional
  *       GPIOs. A future pico9918 revision (v0.4+) will do without
  *       an external RP2040 board and use the RP2040 directly.
  * 
  * Note: Hardware v0.3 has different GPIO mappings for GROMCL and CPUCL
  *       Hardware v0.3 doesn't have a soft reset GPIO either
  * 
  * Purchase links for v0.3 Pi Pico module:
  *       https://www.amazon.com/RP2040-Board-Type-C-Raspberry-Micropython/dp/B0CG9BY48X
  *       https://www.aliexpress.com/item/1005007066733934.html
  */

 #pragma once

 #include "tms9918.pio.h"

#define GPIO_CD7 14
#define GPIO_CSR tmsRead_CSR_PIN  // defined in tms9918.pio.h
#define GPIO_CSW tmsWrite_CSW_PIN // defined in tms9918.pio.h
#define GPIO_MODE 28
#define GPIO_MODE1 29
#define GPIO_INT 22
#define GPIO_RESET 23

// default mappings (v0.4+)
#define GPIO_GROMCL 25
#define GPIO_CPUCL 24

// v0.3-specific pins mappings
#define GPIO_GROMCL_V03 29
#define GPIO_CPUCL_V03 23

// gpio masks
#define GPIO_CD_MASK (0xff << GPIO_CD7)
#define GPIO_CSR_MASK (0x01 << GPIO_CSR)
#define GPIO_CSW_MASK (0x01 << GPIO_CSW)
#define GPIO_MODE_MASK (0x01 << GPIO_MODE)
#define GPIO_MODE1_MASK (0x01 << GPIO_MODE1)
#define GPIO_INT_MASK (0x01 << GPIO_INT)
#define GPIO_RESET_MASK (0x01 << GPIO_RESET)

