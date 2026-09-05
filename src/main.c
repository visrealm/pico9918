/**
 * \file
 * \brief firmware entry point and the two cores' startup order
 *
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

#include "vga.h"
#include "vga-modes.h"

#include "impl/pico9918_priv.h"
#include "pico9918_frame.h"
#include "gpu/gpu.h"
#include "overlay/diag.h"

#include "clocks.h"
#include "config.h"
#include "display.h"
#include "flash.h"
#include "gpio.h"
#include "palette.h"
#include "renderer.h"
#include "temperature.h"
#include "xip.h"
#include "tms_bus.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

/** \brief GPU config-action callback: the GPU loop clears the requesting key and passes it here */
static void configActionCallback(pico9918_t* tms9918, uint8_t* config, uint8_t key, void* userdata)
{
  (void)tms9918;
  (void)userdata;

  switch (key)
  {
    case PICO9918_CONF_SAVE_TO_FLASH: // normal save: changed display fields go to pending
      saveConfigSplitPending(config);
      break;

    case PICO9918_CONF_SAVE_FORCED:     // factory reset: write everything to main, skip the pending split
    case PICO9918_CONF_PENDING_CONFIRM: // user accepted pending change: promote to main
      writeConfig(config);
      erasePendingDisplay();
      pico9918_config_refresh_pending_mirror(config, PENDING_STATE_CONFIRMED);
      break;

    case PICO9918_CONF_PENDING_CANCEL: // user cancelled: keep this run going, revert on next boot
      erasePendingDisplay();
      config[PICO9918_CONF_PENDING_STATE] = PENDING_STATE_CONFIRMED;
      break;
  }
}

/** \brief core 1 entry: start the TMS bus interface, then wait for core 0 and run the VGA loop */
static void __in_flash_func(proc1Entry)(void)
{
  tmsBusInit();

  // Release /INT now that the TMS bus interface is ready.
#ifdef PICO9918_INT_ACTIVE_HIGH
  gpio_put_all(0);
#else
  gpio_put_all(GPIO_INT_MASK);
#endif

  paletteInitCore1();

  multicore_fifo_pop_blocking();
  vgaLoop();
}

/** \brief core 0 entry: bring up GPIO, clocks, config, renderer and video, then run the GPU loop */
int __in_flash_func(main)(void)
{
  // Keep /INT asserted while the firmware initializes.
#ifdef PICO9918_INT_ACTIVE_HIGH
  gpio_put_all(GPIO_INT_MASK);
#else
  gpio_put_all(0);
#endif
  gpio_set_dir_all_bits(GPIO_INT_MASK);
  gpio_set_function_masked(GPIO_CD_MASK | GPIO_CSR_MASK | GPIO_CSW_MASK | GPIO_MODE_MASK | GPIO_MODE1_MASK |
                             GPIO_INT_MASK | GPIO_RESET_MASK,
                           GPIO_FUNC_SIO);

  // Detect on core 0 before core 1 is launched (proc1 also reads it).
  (void)currentHwVersion();
  detectScartDongle();
  systemClockInit();

  pico9918_init();
  multicore_launch_core1(proc1Entry);

  readConfig(tms9918->config);
  applyPendingDisplay(tms9918->config);
  updateDispDriver();

  systemClockApplyConfig();
  vdpClocksInit();
  paletteInit();

  VgaInitParams params = {0};
#if PICO9918_ENABLE_SCART
  // PICO9918_CONF_DISP_DRIVER: 0=VGA, 1=NTSC, 2=PAL (resolved by updateDispDriver).
  if (tms9918->config[PICO9918_CONF_DISP_DRIVER] == 0)
  {
    params.params = vgaGetParams(VGA_640_480_60HZ);
  }
  else
  {
    params.params = vgaGetParams(tms9918->config[PICO9918_CONF_SCART_MODE] ? RGBS_NTSC_720_480i_60HZ : RGBS_PAL_720_576i_50HZ);
  }
#else
  params.params = vgaGetParams(DISPLAY_MODE);
#endif
  rendererConfigureVga(&params);

  vgaInit(params);

  /* after vgaInit: the hook writes vgaCurrentParams(), and it must be in place
     before core 1 starts consuming configDirty */
  pico9918_config_set_applied_callback(applyConfigHostEffects, NULL);

  /* the late config reload the frame module asks for when the display comes up
     after the startup diagnostics screen. Flash I/O, so it stays host-side. */
  pico9918_frame_set_config_reload_callback(reloadStoredConfig, NULL);

  initTemperature();

  pico9918_diag_init();

  /* Version identity and the OUTPUT-row label are host policy - only the host knows
     its board revisions, its firmware version and which timings its PICO9918_CONF_DISP_DRIVER
     encoding names - so the already-chosen strings are pushed. */
#if PICO_RP2350
  pico9918_diag_set_version_info("V2.0+", PICO9918_VERSION);
#else
  pico9918_diag_set_version_info(currentHwVersion() == HWVer_0_3 ? "V0.3" : "V1.0+", PICO9918_VERSION);
#endif

  {
    static const char* outputValues[] = {"480P ", "480I ", "576I "};
    static const char* outputUnits[]  = {"@60", "@60", "@50"};
    uint8_t driver                    = tms9918->config[PICO9918_CONF_DISP_DRIVER];
    if (driver > 2) driver = 0;
    pico9918_diag_set_output_name(outputValues[driver], outputUnits[driver]);
  }

  multicore_fifo_push_blocking(0);

  pico9918_gpu_init();
  pico9918_gpu_set_flash_callback(flashSector, NULL);
  pico9918_gpu_set_config_save_callback(configActionCallback, NULL);

  pico9918_gpu_loop();

  return 0;
}
