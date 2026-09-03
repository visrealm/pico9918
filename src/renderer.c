/**
 * \file
 * \brief renderer - the VGA callbacks, and the host's own overlay tail
 *
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 */

#include "renderer.h"

#include "config.h"
#include "display.h"
#include "palette.h"
#include "temperature.h"
#include "vga.h"
#include "vga-modes.h"

#include "impl/pico9918_priv.h"
#include "pico9918_frame.h"
#include "overlay/diag.h"
#include "overlay/splash.h"

#include "pico/stdlib.h"

/** \brief blank VRAM and park the scanline register through the vertical porch */
static void tmsPorch(void)
{
  pico9918_frame_porch();
}

/** \brief raise the frame interrupt at the trigger scanline */
static void tmsEndOfScanline(uint32_t displayLine)
{
  pico9918_frame_end_of_scanline();
}

/* Frame housekeeping is the library's. What is left here is the three host seams it
   cannot own: the temperature sensor, the VGA display timing, and applying the
   geometry back into the host's own parameter block and trigger register.

   The geometry comes back by value rather than through a registered callback: it is
   strictly cheaper, and there is exactly one place to publish it from and one place
   to apply it. */
static void tmsEndOfFrame(uint32_t frameNumber)
{
  VgaParams* params = &vgaCurrentParams()->params;

  pico9918_frame_display_t display = {params->vSyncParams.displayPixels, params->interlaced, params->vPixelScale,
                                  params->vVirtualPixels};

  pico9918_frame_geometry_t geom = pico9918_frame_end(coreTemperatureC(), params->frameRateHz, &display);

  /* Written back unconditionally, which is not a widening of what the library may
     touch: under interlace pico9918_frame_end leaves both fields at the values it was
     handed, so these two stores put back exactly what was read. Having the host
     re-test the interlace condition would duplicate the library's ownership rule. */
  params->vPixelScale    = display.vPixelScale;
  params->vVirtualPixels = display.vVirtualPixels;

  vgaSetTriggerScanline(geom.triggerScanline);
}

/* Render one display line. The line itself is the library's; what remains here is the
 * VGA layer's callback signature and the host's OWN overlay tail - the pending-display
 * banner, and the diagnostics panels on the lines the library did not already draw
 * them on.
 *
 * The border flag comes back from the library rather than being re-derived, and that
 * is load-bearing: in row-30 progressive mode vBorder is 0, so the rows the banner
 * occupies are ACTIVE rows. A host gating on "y is small" would paint over the display.
 *
 * The diagnostics split is deliberate. An active line draws them inside the library,
 * before it closes its render-time sample - the overlay is part of what the line costs
 * and the recorded timeline was measured that way. A border line draws them here,
 * because the banner has to come first and the banner is host state.
 */
static void __time_critical_func(tmsScanline)(uint16_t y, VgaParams* params, uint16_t* pixels)
{
  const pico9918_scanline_params_t scanlineParams = {params->hVirtualPixels, params->vVirtualPixels,
                                                 params->interlaced, params->interlacedFieldOrder};

  if (!pico9918_frame_scanline(y, &scanlineParams, pixels)) return;

  /* Drop the interlace field bit; the overlays want the display line. Masked here
     explicitly rather than left to a narrowing conversion two levels deeper. */
  y &= 0x0fff;

  /* The pending-display banner persists in the top border until the user power cycles
     (PENDING) or confirms in the configurator (ARMED). Host code by design: the text
     is host-owned flash-block state, the trigger is a host state byte, and the
     centring uses the host's own buffer width.
     Centring is against RGB_PIXELS_X (the buffer width, guard pixels included), NOT
     hVirtualPixels; keep it that way or the banner shifts. */
#define RENDER_CENTERED(scanline, text, ypos, fg, pixelData)                                      \
  pico9918_diag_render_text((scanline), (text),                                                      \
                         (RGB_PIXELS_X - (sizeof(text) - 1) * PICO9918_DIAG_CHAR_WIDTH) / 2, (ypos), \
                         (fg), (pixelData))
  /* fg white, through the pixel policy like the diag panels. Masked to 12 bits: the
     policy replicates green into bits 15-12, which is dead at the pins but live on the
     RP2040 CRT-dim path. */
#define BANNER_FG ((uint16_t)(PICO9918_PIXEL_FROM_RGB12(0xff0f) & 0x0fff))
  uint8_t banner = pendingDisplayBanner();
  if (banner == PENDING_BANNER_AWAIT_PC)
  {
    PICO9918_FILL32_WAIT(PICO9918_FILL_BORDER);
    RENDER_CENTERED(y, "POWER CYCLE TO TEST NEW CONFIGURATION", 8, BANNER_FG, pixels);
  }
  else if (banner == PENDING_BANNER_AWAIT_OK)
  {
    PICO9918_FILL32_WAIT(PICO9918_FILL_BORDER);
    RENDER_CENTERED(y, "OPEN CONFIGURATOR TO CONFIRM NEW SETTINGS", 8, BANNER_FG, pixels);
  }

  if (tms9918->config[PICO9918_CONF_DIAG])
  {
    PICO9918_FILL32_WAIT(PICO9918_FILL_BORDER);
    pico9918_diag_render(y, params->vVirtualPixels, pixels);
  }
}

/** \brief install the renderer's scanline, frame and porch callbacks into \p params */
void rendererConfigureVga(VgaInitParams* params)
{
  params->scanlineFn      = tmsScanline;
  params->endOfFrameFn    = tmsEndOfFrame;
  params->endOfScanlineFn = tmsEndOfScanline;
  params->porchFn         = tmsPorch;
  params->triggerScanline = UINT32_MAX;
}
