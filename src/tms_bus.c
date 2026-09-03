/**
 * \file
 * \brief TMS9918A host bus interface - PIO state machines and their IRQ handlers
 *
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

#include "tms_bus.h"

#include "config.h"
#include "gpio.h"
#include "renderer.h"
#include "overlay/splash.h"
#include "tms9918.pio.h"
#include "impl/pico9918_priv.h"

#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

/* TMS_PIO, the two IRQs, the state-machine indices and the three tier-1 ops come
   from pico9918HostOps.h, which the library's platform dispatch header pulls in -
   so the ops expand inside the library TUs that need them as well as here. */

/** \brief the byte the read SM answers the next data read with; the ops header pushes it */
uint8_t nextValue = 0;

/** \brief refill the read SM with the reg/status/data/pindirs word it answers reads from */
static void updateTmsReadAhead(void)
{
  PICO9918_HOST_STATUS_VISIBLE();
}

/** \brief  a host read finished: advance the read-ahead on a data read, or apply the
 *          clear-on-read side effects and release /INT on a status read
 */
static void __not_in_flash_func(tmsReadIrqHandler)(void)
{
  uint32_t readVal = TMS_PIO->rxf[tmsReadSm];

  if ((readVal & 0x01) == 0)
  {
    nextValue = pico9918_read_ahead_data_impl();
  }
  else
  {
    readVal >>= (1 + 16);
    pico9918_status_read_reconcile_impl(readVal >> 8, readVal);
  }

  updateTmsReadAhead();
}

/** \brief  a host write finished: MODE comes from the pin state latched at the /CSW
 *          falling edge, the data byte from the state at the rising edge
 */
static void __not_in_flash_func(tmsWriteIrqHandler)(void)
{
  uint32_t writeVal = TMS_PIO->rxf[tmsWriteSm];
  uint8_t dataVal   = writeVal & 0xff;
  writeVal >>= (GPIO_MODE - GPIO_CD7) + 16;

  if (writeVal & 0x01)
  {
    pico9918_write_addr_impl(dataVal);
    pico9918_write_reconcile_int_impl();
  }
  else
  {
    pico9918_write_data_impl(dataVal);
  }

  nextValue = pico9918_read_data_no_inc_impl();
  updateTmsReadAhead();
}

/** \brief host reset: reset the VDP, reload the config and re-arm the bus from scratch */
static void __not_in_flash_func(gpioIrqHandler)(void)
{
  gpio_acknowledge_irq(GPIO_RESET, GPIO_IRQ_EDGE_FALL);
  PICO9918_HOST_ENTER_CRITICAL();

  pico9918_reset();
  readConfig(tms9918->config);

  irq_clear(TMS_WRITE_IRQ);
  irq_clear(TMS_READ_IRQ);
  pio_sm_clear_fifos(TMS_PIO, tmsReadSm);
  pio_sm_clear_fifos(TMS_PIO, tmsWriteSm);

  nextValue = 0;
  pico9918_frame_reset_int_impl(); // SR0 = 0x1f, INT clear, doneInt = TRUE
  rendererReset();
  updateTmsReadAhead();

  PICO9918_HOST_SET_INT(pico9918_frame_int_impl());
  PICO9918_HOST_EXIT_CRITICAL();
}

/** \brief start the read and write state machines and hook up their IRQ handlers */
void tmsBusInit(void)
{
  irq_set_exclusive_handler(TMS_WRITE_IRQ, tmsWriteIrqHandler);
  irq_set_enabled(TMS_WRITE_IRQ, true);

  irq_set_exclusive_handler(TMS_READ_IRQ, tmsReadIrqHandler);
  irq_set_enabled(TMS_READ_IRQ, true);

  uint tmsWriteProgram         = pio_add_program(TMS_PIO, &tmsWrite_program);
  pio_sm_config writePioConfig = tmsWrite_program_get_default_config(tmsWriteProgram);
  sm_config_set_in_pins(&writePioConfig, GPIO_CD7);
  sm_config_set_in_shift(&writePioConfig, false, true, 32);
  sm_config_set_jmp_pin(&writePioConfig, GPIO_CSW);
  sm_config_set_clkdiv(&writePioConfig, 1.0f);
  pio_sm_init(TMS_PIO, tmsWriteSm, tmsWriteProgram, &writePioConfig);
  pio_sm_set_enabled(TMS_PIO, tmsWriteSm, true);
  pio_set_irq0_source_enabled(TMS_PIO, pis_sm0_rx_fifo_not_empty, true);

  uint tmsReadProgram = pio_add_program(TMS_PIO, &tmsRead_program);
  for (uint i = 0; i < 8; ++i) pio_gpio_init(TMS_PIO, GPIO_CD7 + i);

  pio_sm_config readPioConfig = tmsRead_program_get_default_config(tmsReadProgram);
  sm_config_set_jmp_pin(&readPioConfig, GPIO_CSR);
  sm_config_set_in_pins(&readPioConfig, GPIO_MODE);
  sm_config_set_out_pins(&readPioConfig, GPIO_CD7, 8);
  sm_config_set_in_shift(&readPioConfig, false, false, 32);
  sm_config_set_out_shift(&readPioConfig, true, false, 32);
  sm_config_set_clkdiv(&readPioConfig, 1.0f);
  pio_sm_init(TMS_PIO, tmsReadSm, tmsReadProgram, &readPioConfig);
  pio_sm_set_enabled(TMS_PIO, tmsReadSm, true);
  pio_set_irq1_source_enabled(TMS_PIO, pis_sm1_rx_fifo_not_empty, true);
  pio_sm_put(TMS_PIO, tmsReadSm, 0x000000ff);

  Pico9918HardwareVersion hwVersion = currentHwVersion();
  if (hwVersion != HWVer_0_3)
  {
    irq_set_exclusive_handler(IO_IRQ_BANK0, gpioIrqHandler);
    gpio_set_irq_enabled(GPIO_RESET, GPIO_IRQ_EDGE_FALL, true);
    irq_set_enabled(IO_IRQ_BANK0, true);
  }
  tms9918->config[PICO9918_CONF_HW_VERSION] = hwVersion;
}
