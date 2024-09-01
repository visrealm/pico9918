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

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

 /*
  * Pin mapping (PCB v0.3)
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
  *       GPIOs. A future pico9918 revision (v0.4+) will do without
  *       an external RP2040 board and use the RP2040 directly.
  *
  * Purchase links:
  *       https://www.amazon.com/RP2040-Board-Type-C-Raspberry-Micropython/dp/B0CG9BY48X
  *       https://www.aliexpress.com/item/1005007066733934.html
  */

#define PCB_MAJOR_VERSION 0
#define PCB_MINOR_VERSION 4

#define GPIO_CD0 14
#define GPIO_CSR 26
#define GPIO_CSW 27
#define GPIO_MODE 28
#define GPIO_INT 22

#if PCB_MAJOR_VERSION != 0
#error "Time traveller?"
#endif

  // pin-mapping for gromclk and cpuclk changed in PCB v0.4
  // in order to have MODE and MODE1 sequential
#if PCB_MINOR_VERSION < 4
#error "Not for v0.3 yet"
#define GPIO_GROMCL 29
#define GPIO_CPUCL 23
#else
#define GPIO_GROMCL 25
#define GPIO_CPUCL 24
#define GPIO_RESET 23
#define GPIO_MODE1 29
#endif

#define GPIO_CD_MASK (0xff << GPIO_CD0)
#define GPIO_CSR_MASK (0x01 << GPIO_CSR)
#define GPIO_CSW_MASK (0x01 << GPIO_CSW)
#define GPIO_MODE_MASK (0x01 << GPIO_MODE)
#define GPIO_INT_MASK (0x01 << GPIO_INT)
#define GPIO_GROMCL_MASK (0x01 << GPIO_GROMCL)
#define GPIO_CPUCL_MASK (0x01 << GPIO_CPUCL)

// Quality control wiring:
// CPUCLK to CSR
// GROMCLK to CSW
// INT to MODE

int main(void)
{
  stdio_init_all();
  gpio_init_mask(GPIO_CD_MASK | GPIO_GROMCL_MASK | GPIO_CPUCL_MASK | GPIO_CSR_MASK | GPIO_CSW_MASK | GPIO_MODE_MASK | GPIO_INT_MASK);
  gpio_set_dir_all_bits(GPIO_CD_MASK | GPIO_GROMCL_MASK | GPIO_CPUCL_MASK | GPIO_INT_MASK); // set r, w, mode to outputs

start:
  gpio_put_all(0);

  printf("PICO9918 QC Tool\n");

  bool initialCheckOk;
  
  initialCheckOk = true;


  printf("Testing CPUCLK and CSR\n");

  printf("Set CPUCLK 1\n");
  gpio_put(GPIO_CPUCL, 1);
  sleep_ms(1);
  bool val = gpio_get(GPIO_CSR);
  if (!val) initialCheckOk = false;
  printf("Check CSR %d - %s\n", val, val ? "OK" : "FAILED!");
  sleep_ms(500);

  printf("Set CPUCLK 0\n");
  gpio_put(GPIO_CPUCL, 0);
  sleep_ms(1);
  val = gpio_get(GPIO_CSR);
  if (val) initialCheckOk = false;
  printf("Check CSR %d - %s\n", val, !val ? "OK" : "FAILED!");
  sleep_ms(500);


  printf("Testing GROMCLK and CSW\n");

  printf("Set GROMCLK 1\n");
  gpio_put(GPIO_GROMCL, 1);
  sleep_ms(1);
  val = gpio_get(GPIO_CSW);
  if (!val) initialCheckOk = false;
  printf("Check CSW %d - %s\n", val, val ? "OK" : "FAILED!");
  sleep_ms(500);

  printf("Set GROMCLK 0\n");
  gpio_put(GPIO_GROMCL, 0);
  sleep_ms(1);
  val = gpio_get(GPIO_CSW);
  if (val) initialCheckOk = false;
  printf("Check CSW %d - %s\n", val, !val ? "OK" : "FAILED!");
  sleep_ms(500);



  printf("Testing INT and MODE\n");

  printf("Set INT 1\n");
  gpio_put(GPIO_INT, 1);
  sleep_ms(1);
  val = gpio_get(GPIO_MODE);
  if (!val) initialCheckOk = false;
  printf("Check MODE %d - %s\n", val, val ? "OK" : "FAILED!");
  sleep_ms(500);

  printf("Set INT 0\n");
  gpio_put(GPIO_INT, 0);
  sleep_ms(1);
  val = gpio_get(GPIO_MODE);
  if (val) initialCheckOk = false;
  printf("Check MODE %d - %s\n", val, !val ? "OK" : "FAILED!");
  sleep_ms(500);

  if (!initialCheckOk)
  {
    printf("Initial check failed. Halting.");
    while (1)
    {
      tight_loop_contents();
    }
  }
  else
  {
    printf("Initial check passed. Continuing...");
  }

  uint8_t gpioOut = 1;

  printf("CD bit cycle start\n");

  for (int i = 0; i < 3 * 8; ++i)
  {
    uint32_t out = GPIO_GROMCL_MASK; // read low (active), write high (inactive)
    out |= (uint32_t)gpioOut << GPIO_CD0;

    gpio_put_all(out);

    printf("%02x\n", gpioOut);

    gpioOut <<= 1;
    if (gpioOut == 0) gpioOut= 1;

    sleep_ms(250);
  }

  printf("CD bit cycle end\n");
  printf("CD bit cycle OE disabled start\n");

  for (int i = 0; i < 3 * 8; ++i)
  {
    uint32_t out = GPIO_GROMCL_MASK | GPIO_CPUCL_MASK; // read low (active), write high (inactive)
    out |= (uint32_t)gpioOut << GPIO_CD0;

    gpio_put_all(out);

    printf("%02x\n", gpioOut);

    gpioOut <<= 1;
    if (gpioOut == 0) gpioOut= 1;

    sleep_ms(250);
  }
  printf("CD bit cycle OE disabled end\n");

  goto start;




  return 0;
}
