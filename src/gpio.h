/**
 * \file
 * \brief host bus GPIO assignments and pin masks
 *
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

/* MCU GPIO -> signal -> TMS9918A pin; the MCU side is the _L net on the schematic
 *   signal   v0.4+  v0.3   v0.3 module pin  TMS9918A pin
 *   CD7-CD0  14-21  14-21  19-22, 24-27     24-17 (CD0 is the MSB)
 *   /INT     22     22     29               16
 *   RST      23     RUN    30               34
 *   CPUCLK   24     23     37               38
 *   GROMCLK  25     29     35               37
 *   /CSR     26     26     31               15
 *   /CSW     27     27     32               14
 *   MODE     28     28     34               13
 *   MODE1    29     -      -                -
 *
 * GROMCLK and CPUCLK need GPIO23 and GPIO29, which a genuine Raspberry Pi Pico
 * does not bring out, so v0.3 takes the DWEII RP2040 USB-C module instead. It
 * also has no soft reset GPIO, taking host reset on RUN.
 *   https://www.amazon.com/RP2040-Board-Type-C-Raspberry-Micropython/dp/B0CG9BY48X
 *   https://www.aliexpress.com/item/1005007066733934.html
 */

#include "tms9918.pio.h"

// Base pins, overridable at build time with -DPICO9918_GPIO_* from pico9918_config.cmake
#ifndef PICO9918_GPIO_CD7
#define PICO9918_GPIO_CD7 14
#endif
#define GPIO_CD7 PICO9918_GPIO_CD7 ///< data bus base; CD7 to CD0 run upwards from here

// /CSR and /CSW are fixed at 26 and 27 by tms9918.pio, so they are not overridable
#define GPIO_CSR tmsRead_CSR_PIN
#define GPIO_CSW tmsWrite_CSW_PIN

#ifndef PICO9918_GPIO_MODE
#define PICO9918_GPIO_MODE 28
#endif
#define GPIO_MODE PICO9918_GPIO_MODE

#ifndef PICO9918_GPIO_MODE1
#define PICO9918_GPIO_MODE1 29
#endif
#define GPIO_MODE1 PICO9918_GPIO_MODE1 ///< V9938 second mode line, no TMS9918A pin

#ifndef PICO9918_GPIO_INT
#define PICO9918_GPIO_INT 22
#endif
#define GPIO_INT PICO9918_GPIO_INT

#ifndef PICO9918_GPIO_RESET
#define PICO9918_GPIO_RESET 23
#endif
#define GPIO_RESET PICO9918_GPIO_RESET ///< host reset in; not present on v0.3

// clock outputs, v0.4+ mapping
#ifndef PICO9918_GPIO_GROMCL
#define PICO9918_GPIO_GROMCL 25
#endif
#define GPIO_GROMCL PICO9918_GPIO_GROMCL

#ifndef PICO9918_GPIO_CPUCL
#define PICO9918_GPIO_CPUCL 24
#endif
#define GPIO_CPUCL PICO9918_GPIO_CPUCL

// clock outputs, v0.3 mapping
#ifndef PICO9918_GPIO_GROMCL_V03
#define PICO9918_GPIO_GROMCL_V03 29
#endif
#define GPIO_GROMCL_V03 PICO9918_GPIO_GROMCL_V03

#ifndef PICO9918_GPIO_CPUCL_V03
#define PICO9918_GPIO_CPUCL_V03 23
#endif
#define GPIO_CPUCL_V03 PICO9918_GPIO_CPUCL_V03

// pin masks for the gpio_*_masked / gpio_*_all_bits calls
#define GPIO_CD_MASK    (0xff << GPIO_CD7)
#define GPIO_CSR_MASK   (0x01 << GPIO_CSR)
#define GPIO_CSW_MASK   (0x01 << GPIO_CSW)
#define GPIO_MODE_MASK  (0x01 << GPIO_MODE)
#define GPIO_MODE1_MASK (0x01 << GPIO_MODE1)
#define GPIO_INT_MASK   (0x01 << GPIO_INT)
#define GPIO_RESET_MASK (0x01 << GPIO_RESET)
