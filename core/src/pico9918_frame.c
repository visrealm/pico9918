/**
 * \file
 * \brief pico9918-core - frame module
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Per-frame / per-scanline sequencing: the interrupt/status latch merge, the
 * vertical porch, the end-of-frame interrupt and trigger line, the true end of
 * frame, and the vertical geometry it derives.
 *
 * It lands in a .c rather than on the Impl inline surface because a real function
 * here costs nothing extra: every call site already reaches it with a `bl`.
 * Inlining it into its three callers - one of them the per-scanline path - would
 * ADD per-scanline code, not remove a call. The entries the host reaches through a
 * VGA function pointer are calls for the same reason.
 */

#include "impl/pico9918_priv.h"
#include "pico9918_frame.h"
/* pico9918_gpu_trigger, the ONLY GPU entry this module uses - so gpu/gpu.h is not
   included here; the GPU time accessors it declares belong to the diagnostics
   overlay. */
#include "impl/pico9918_gpu_priv.h"
#include "overlay/diag.h"
#include "overlay/splash.h"

/* Frame counter and dropped-frame accounting - declared on the Impl surface, which
   carries the ownership and placement rationale. The window itself is a file static
   because nothing outside this module reads it; only the running total is exposed. */
int pico9918_frame_count                = 0;
int pico9918_dropped_frames_count        = 0;
static bool dropped_frames[16] = {0};

/* Vertical geometry and the display-enable latch. Also declared on the Impl
   surface - same rationale, same measurement. */
int pico9918_v_pixels      = 192;
uint32_t pico9918_v_border = 0;
bool pico9918_valid_writes = false;

/* What pico9918_frame_output_line maps an output line through. Seeded progressive. */
uint8_t pico9918_v_scale    = 2;
uint16_t pico9918_v_virtual = 240;

#if PICO9918_DIAG_GPU_FRAME_COUNTER
/* GPU frames observed at end of frame. Opt-in, and it sits with the accumulation
   that feeds it: the increment is per-frame and the reset is on console reset,
   both in this module. */
uint32_t pico9918_gpu_frame_count = 0;
#endif

/* Host late-config-reload hook - see the header for the contract, and for why this
   is a pointer rather than a tier-1 op. */
static void (*configReloadCallback)(void) = NULL;

/* The border colour the border-fill DMA reads. THE SCRATCH PLACEMENT IS
   LOAD-BEARING: `.scratch_y` is one of the RP2040's two single-cycle SRAM banks,
   and a silent fallthrough to striped SRAM is a real per-scanline loss that no
   functional test can see.

   Not static, because the fill instance that reads it is configured in
   pico9918.c's initLookups(), alongside the library's own fill - one init site
   for both, never lazily. Declared on the Impl surface with the module's other
   globals rather than reached for by an extern at the use site. */
PICO9918_SECTION_SCRATCH_Y(buffer) uint32_t pico9918_border_bg;

void pico9918_frame_set_config_reload_callback(void (*cb)(void))
{
  configReloadCallback = cb;
}

/**
 * \brief merge newly raised status flags into the SR0 latch, publish it, and bring the
 * /INT pin into agreement. See the header for the parameter contract.
 *
 * The internal sequence is load-bearing at two points and must not be reordered:
 *
 *  - the three-way merge below is the v1.2.0 semantics, not a simplification of
 *    it. Each branch differs in what it lets through and what it preserves;
 *  - the status publish (PICO9918_HOST_STATUS_VISIBLE) happens BEFORE the pin sync,
 *    so a host CPU that takes the interrupt cannot read a stale status.
 */
void pico9918_frame_update_interrupts(PICO9918_INST_ARG uint8_t tempStatus)
{
  PICO9918_HOST_ENTER_CRITICAL();
  uint8_t currentStatus = pico9918_frame_status_impl(PICO9918_INST_ONLY);
  if ((currentStatus & STATUS_INT) == 0)
  {
    if (currentStatus & STATUS_5S)
    {
      // 5S already latched - preserve existing ID, OR in any new flags (INT, 5S, COL)
      currentStatus |= (tempStatus & 0xe0);
    }
    else
    {
      currentStatus = (currentStatus & 0xe0) | tempStatus;
    }
  }
  else
  {
    // F is set - only allow COL through (per F18A/TMS9918A: COL is not gated by F)
    // 5S is blocked while F is set (per datasheet)
    currentStatus |= (tempStatus & STATUS_COL);
  }

  pico9918_set_status_impl(PICO9918_INST currentStatus);
  PICO9918_HOST_STATUS_VISIBLE();

  // Ensure interrupt pin state is correct
  // (in case R1 was modified to enable/disable interrupts)
  pico9918_frame_sync_int_impl(PICO9918_INST_ONLY);
  PICO9918_HOST_EXIT_CRITICAL();
}

/** \brief see the header. */
void pico9918_frame_porch(PICO9918_INST_ONLY_ARG)
{
  tms9918->vram.map.blanking = 1;   // V
  tms9918->vram.map.scanline = 255; // F18A value for vsync
  TMS_STATUS(tms9918, 0x03)  = 255;
}

/**
 * \brief see the header.
 *
 * The configDirty consumption is a CHECK-THEN-CLEAR on a non-volatile flag written
 * from the other core (the register path sets it on core 1, the host's config load
 * on core 0) and consumed here. Deliberately left alone: the worst case is a config
 * apply deferred by one frame.
 */
void pico9918_frame_raise_end_of_frame_int(PICO9918_INST_ONLY_ARG)
{
  pico9918_set_frame_done_int_impl(PICO9918_INST true);
  TMS_STATUS(tms9918, 0x01) |= STATUS1_BLANK;
  if (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) & PICO9918_R50_GPU_VSYNC)
  {
    pico9918_gpu_trigger(PICO9918_INST_ONLY);
  }

  if (tms9918->configDirty)
  {
    tms9918->configDirty = false;
    /* invokes the config-applied callback, which owns the VGA-side write */
    pico9918_config_apply(PICO9918_INST_ONLY);
    pico9918_diag_config_updated(PICO9918_INST_ONLY);
  }

  pico9918_frame_update_interrupts(PICO9918_INST STATUS_INT);
}

/** \brief see the header. It takes no display line: the body never reads one. */
void pico9918_frame_end_of_scanline(PICO9918_INST_ONLY_ARG)
{
  if (!pico9918_frame_done_int_impl(PICO9918_INST_ONLY))
  {
    bool droppedFrame = pico9918_frame_status_impl(PICO9918_INST_ONLY) & STATUS_INT;
    pico9918_dropped_frames_count += droppedFrame - dropped_frames[pico9918_frame_count & 0xf];
    dropped_frames[pico9918_frame_count & 0xf] = droppedFrame;

    pico9918_frame_raise_end_of_frame_int(PICO9918_INST_ONLY);
  }
}

/**
 * \brief see the header.
 *
 * Three things about this expression are deliberately NOT tidied:
 *
 *  - the yScale-conditional structure. vPixelScale and vVirtualPixels are
 *    rewritten only when yScale > 1, and the vPixels doubling is gated on the SAME
 *    condition. Under interlace the host owns the first two and vPixels must NOT
 *    double - the two fields already supply the second set of lines;
 *  - the shift-by-bool algebra (`yScale - (bool)doubleRows`, `<< (bool)doubleRows`).
 *    The golden frame surface's reference deliberately decomposes this into
 *    explicit cases so the two do not share the algebra; rewriting it here to look
 *    like the reference would destroy that independence;
 *  - the SIGNED intermediate for the border. SCART NTSC in row-30 mode makes it
 *    negative and the narrowing to uint32_t is the shipping behaviour, pinned at
 *    geom-scart-ntsc-row30. A known defect, ruled won't-fix: the host's unsigned
 *    border test then sends all 220 lines down the border path. Do not "fix" it
 *    here.
 *
 * yScale is derived from `interlaced` rather than from a build-time DISPLAY_YSCALE.
 * A non-SCART build's DISPLAY_YSCALE is always 2 with interlaced false, so the
 * interlace-derived form covers both cases exactly and the library needs no
 * build-time host macro.
 */
pico9918_frame_geometry_t pico9918_frame_geometry(PICO9918_INST_ARG pico9918_frame_display_t* display)
{
  const int yScale = display->interlaced ? 1 : 2;

  if (yScale > 1)
  {
    bool doubleRows         = TMS_REGISTER(tms9918, TMS_REG_0) & TMS_R0_DOUBLE_ROWS;
    display->vPixelScale    = yScale - (bool)doubleRows;
    display->vVirtualPixels = (display->displayPixels / yScale) << (bool)doubleRows;
  }

  /* Outside the conditional so both arms publish; under interlace these are the host's. */
  pico9918_v_scale   = display->vPixelScale;
  pico9918_v_virtual = display->vVirtualPixels;

  int baseRows = (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED1) & PICO9918_R49_ROW30) ? 30 : 24;
  pico9918_v_pixels = baseRows << 3;
  if (yScale > 1 && (TMS_REGISTER(tms9918, TMS_REG_0) & TMS_R0_DOUBLE_ROWS)) pico9918_v_pixels <<= 1;
  pico9918_v_border = (display->vVirtualPixels - pico9918_v_pixels) / 2;

  pico9918_frame_geometry_t g;
  g.vPixels         = pico9918_v_pixels;
  g.vBorder         = pico9918_v_border;
  g.triggerScanline = pico9918_v_border + pico9918_v_pixels;
  return g;
}

/**
 * \brief see the header. It takes no frame number: the cadence and the thresholds all run
 * off this module's own frame counter, which - unlike the VGA layer's frame number -
 * resets on console reset.
 *
 * The frame count is re-READ at each use rather than cached in a local. That is not
 * a style choice: the host's tier-1 critical section deliberately does not mask the
 * reset GPIO IRQ, which zeroes the counter, so a console reset landing mid-function
 * is observable and a cached copy would hide it.
 */
pico9918_frame_geometry_t pico9918_frame_end(PICO9918_INST_ARG float tempC, float frameRateHz,
                                       pico9918_frame_display_t* display)
{
  ++pico9918_frame_count;
#if PICO9918_DIAG_GPU_FRAME_COUNTER
  pico9918_gpu_frame_count += (TMS_STATUS(tms9918, 2) & 0x80) != 0;
#endif

  /* The slice is per scanline, so it moves with the line count and the refresh. */
  pico9918_gpu_note_frame(PICO9918_INST display->vVirtualPixels, frameRateHz);

  {
    static float tempAccum = 0.0f;
    tempAccum += tempC;
    if ((pico9918_frame_count & 0x3f) == 0) // every 64th frame
    {
      tempAccum /= 64.0f;
      pico9918_diag_set_temperature(tempAccum);
      uint8_t t4              = (uint8_t)(tempAccum * 4.0f + 0.5f);
      TMS_STATUS(tms9918, 13) = t4;
      tempAccum               = 0.0f;
    }
  }

  if (!pico9918_valid_writes)
  {
    // has the display been enabled?
    if ((pico9918_valid_writes = (TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_DISP_ACTIVE)) != 0)
    {
      pico9918_splash_allow_hide();
      if (pico9918_frame_count > PICO9918_FRAME_STARTUP_FRAMES)
      {
        // reset diagnostics and other settings back to defaults
        if (configReloadCallback) configReloadCallback();
      }
    }
  }

  if (tms9918->config[PICO9918_CONF_DIAG] && PICO9918_HAS(tms9918, PICO9918_FEAT_OVERLAY))
  {
    /* the overlay has no clock of its own, so the host's display timing is pushed
       right before it recomputes. The dropped-frame and GPU-frame counters need no
       push: this module owns both AND drives the refresh, so the overlay reads them
       directly. */
    pico9918_diag_set_frame_rate(frameRateHz);
    pico9918_diag_update(PICO9918_INST pico9918_frame_count);
  }

  // here, we catch the case where the last row(s) were
  // missed and we never raised an interrupt. do it now
  if (!pico9918_frame_done_int_impl(PICO9918_INST_ONLY))
  {
    pico9918_frame_raise_end_of_frame_int(PICO9918_INST_ONLY);
  }

  return pico9918_frame_geometry(PICO9918_INST display);
}

/**
 * \brief see the header. The per-scanline path - the function every gate in this project
 * exists to protect.
 *
 * The host's pending-display banner is deliberately not here. It is host code - host flash
 * state, a host trigger byte, centring against the host's own buffer width - and its only
 * ordering constraints are pixel ones: after the border fill and the splash, before the
 * diagnostics overlay. The host's overlay tail already sits there, and what runs between
 * touches no pixels, so the framebuffer is bit-identical either way.
 *
 * The return value is the border flag, and the host needs it to place that banner. "y is
 * small" is not the same test: in row-30 progressive mode vBorder is 0, so the rows the
 * banner occupies are active ones and a host that guessed would paint over the display.
 *
 * FIVE THINGS HERE MUST NOT BE TIDIED:
 *
 *  - the `bg` store BEFORE the border WAIT. The previous line's right-border fill may
 *    still be reading the source word when this line overwrites it. Benign - the
 *    value is the same on all but the frame a background register changes - and
 *    "fixing" it by waiting first adds a per-scanline stall the goldens cannot see;
 *  - the row-30 border test. The `// TODO` below is a recorded, user-ruled won't-fix;
 *  - the SCART-NTSC negative-border underflow. vBorder is unsigned and row-30 on that
 *    timing makes it 4294967286, so this test sends all 220 lines down the border
 *    path and renders none. Pinned deliberately by the golden frame surface at
 *    geom-scart-ntsc-row30. Do not fix it here;
 *  - the frame count re-READ at each use rather than cached, for the reason
 *    pico9918_frame_end above states: the reset GPIO IRQ is not masked and zeroes it;
 *  - the `y -= vBorder` in BOTH arms. It looks like it belongs after the branch, but
 *    the border arm's own body reads the unadjusted y (the bottom-border scanline
 *    register, the palette-regenerate trigger and the splash all do), so hoisting it
 *    would change all three.
 */
bool __time_critical_func(pico9918_frame_scanline)(PICO9918_INST_ARG uint16_t y,
                                                 const pico9918_scanline_params_t* params, PICO9918_PIXEL_T* pixels)
{
  const uint32_t halfHBorder = (params->hVirtualPixels - TMS9918_PIXELS_X * 2) / 4;

  // for interlaced modes, bit 12 of y carries the field number (0=Field1, 1=Field2)
  const uint8_t field = (y >> 12) & 1;
  y                   = y & 0x0fff; // virtual line within the field (0..N-1)

  uint32_t* dPixels = (uint32_t*)pixels;

  /* 512 bytes for 80 columns on a board with the 8bpp tier, 256 everywhere else */
  const uint32_t lineBytes = pico9918_line_bytes(PICO9918_INST_ONLY);
  /* the margin is the backdrop, so it takes tile layer 1's palette select as the
     picture does, except where 80 columns index the LUT by a pair of nibbles and have
     no room for one */
  const bool packedNibbles =
    pico9918_display_mode(PICO9918_INST_ONLY) == TMS_MODE_TEXT80 && lineBytes == TMS9918_PIXELS_X;
  pico9918_border_bg = pico9918_palette_lut[(pico9918_reg_value(PICO9918_INST TMS_REG_FG_BG_COLOR) & 0x0f) |
                                  (packedNibbles ? 0 : (TMS_REGISTER(tms9918, PICO9918_REG_PALETTE_SELECT) & PICO9918_R24_TILE1_PS) << 4)];

  if (y == 0)
  {
    pico9918_set_frame_done_int_impl(PICO9918_INST false);
  }

  PICO9918_FILL32_WAIT(PICO9918_FILL_BORDER);

  /*** top and bottom borders ***/
  // TODO: None of this runs in ROW30 mode
  if (y < pico9918_v_border_impl(PICO9918_INST_ONLY) ||
      y >= (pico9918_v_border_impl(PICO9918_INST_ONLY) + pico9918_v_pixels_impl(PICO9918_INST_ONLY)))
  {
    PICO9918_FILL32_SET_COUNT(PICO9918_FILL_BORDER, params->hVirtualPixels / 2);
    PICO9918_FILL32_TRIGGER(PICO9918_FILL_BORDER, dPixels);
    tms9918->vram.map.blanking = 1; // V
    if ((y >= pico9918_v_border_impl(PICO9918_INST_ONLY) + pico9918_v_pixels_impl(PICO9918_INST_ONLY)))
    {
      tms9918->vram.map.scanline = y - pico9918_v_border_impl(PICO9918_INST_ONLY);
      TMS_STATUS(tms9918, 0x03)  = tms9918->vram.map.scanline;
    }

    if (PICO9918_HAS(tms9918, PICO9918_FEAT_OVERLAY) &&
        (!pico9918_valid_writes_impl(PICO9918_INST_ONLY) ||
         (pico9918_frame_count_impl(PICO9918_INST_ONLY) < 600)))
    {
      PICO9918_FILL32_WAIT(PICO9918_FILL_BORDER);

      pico9918_splash_render(y, pico9918_frame_count_impl(PICO9918_INST_ONLY),
                           pico9918_v_border_impl(PICO9918_INST_ONLY),
                           pico9918_v_pixels_impl(PICO9918_INST_ONLY), params->vVirtualPixels, pixels);

      if (pico9918_frame_count_impl(PICO9918_INST_ONLY) > PICO9918_FRAME_STARTUP_FRAMES)
      {
        tms9918->config[PICO9918_CONF_DIAG]             = true;
        tms9918->config[PICO9918_CONF_DIAG_REGISTERS]   = true;
        tms9918->config[PICO9918_CONF_DIAG_PERFORMANCE] = true;
        tms9918->config[PICO9918_CONF_DIAG_PALETTE]     = true;
        tms9918->config[PICO9918_CONF_DIAG_ADDRESS]     = true;
      }
    }

    if (y == pico9918_v_border_impl(PICO9918_INST_ONLY) - 1)
    {
      pico9918_palette_regenerate(PICO9918_INST_ONLY);
    }

    if (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) & PICO9918_R50_GPU_HSYNC)
    {
      pico9918_gpu_trigger(PICO9918_INST_ONLY);
    }

    /* Border lines too: a program paging in the vertical blank waits on exactly these. */
    pico9918_gpu_service(PICO9918_INST_ONLY);

    return true;
  }

  uint32_t lineStart = PICO9918_HOST_TIME_US();

  y -= pico9918_v_border_impl(PICO9918_INST_ONLY);
  tms9918->vram.map.blanking = 0;
  tms9918->vram.map.scanline = y;
  TMS_STATUS(tms9918, 0x03)  = y;

  /*** left border ***/
  PICO9918_FILL32_SET_COUNT(PICO9918_FILL_BORDER, halfHBorder);
  PICO9918_FILL32_TRIGGER(PICO9918_FILL_BORDER, dPixels);

  /*** main display region ***/
  if (pico9918_palette_dirty(PICO9918_INST_ONLY)) pico9918_palette_regenerate(PICO9918_INST_ONLY);

  /* generate the scanline. The field mapping is an Impl inline so the golden frame
     surface can drive it directly, as its candidate path. Inlines away; see the
     accessor. */
  uint16_t tmsY =
    pico9918_frame_map_line_impl(PICO9918_INST y, field, params->interlaced, params->interlacedFieldOrder);

  /* the previous line's capture reads the buffer this render is about to overwrite */
  PICO9918_LINE_CAPTURE_WAIT();

  uint32_t renderTime = PICO9918_HOST_TIME_US();
  uint8_t tempStatus  = pico9918_scan_line(PICO9918_INST tmsY);
  renderTime          = PICO9918_HOST_TIME_US() - renderTime;

  const uint8_t* lineSource = pico9918_line_source(PICO9918_INST_ONLY);
  PICO9918_LINE_CAPTURE(y, pico9918_v_pixels_impl(PICO9918_INST_ONLY), lineBytes, lineSource);

  /*** F18A status register updates ***/
  TMS_STATUS(tms9918, 0x01) &= (uint8_t)~STATUS1_BLANK;

  /* The flag latches until the host reads SR1 - it is that read which acknowledges the
     interrupt. It does not touch SR0: the scanline interrupt is its own source, under
     R0's enable, and merging it into the frame flag would make it answer to R1's. */
  if (tms9918->vram.map.scanline && (TMS_REGISTER(tms9918, PICO9918_REG_HORZ_INT_LINE) == tms9918->vram.map.scanline))
  {
    TMS_STATUS(tms9918, 0x01) |= STATUS1_HF;
  }

  if (TMS_REGISTER(tms9918, PICO9918_REG_ENHANCED2) & PICO9918_R50_GPU_HSYNC)
  {
    pico9918_gpu_trigger(PICO9918_INST_ONLY);
  }

  pico9918_frame_update_interrupts(PICO9918_INST tempStatus);

  PICO9918_FILL32_WAIT(PICO9918_FILL_BORDER);

  tms9918->vram.map.blanking = 1; // H

  // convert all pixel data from color index to the host pixel format
#if PICO9918_TEXT80_8BPP
  if (lineBytes != TMS9918_PIXELS_X)
    PICO9918_EXPAND_INDEXED_WIDE(dPixels + halfHBorder, lineSource, lineBytes, pico9918_palette_lut);
  else
#endif
    PICO9918_EXPAND_INDEXED(dPixels + halfHBorder, lineSource, TMS9918_PIXELS_X, pico9918_palette_lut);

  // right border
  PICO9918_FILL32_TRIGGER(PICO9918_FILL_BORDER, dPixels + halfHBorder + TMS9918_PIXELS_X);

  /* the overlay is part of what the line costs, so it draws before the sample closes.
     Border lines draw it host-side instead - the banner has to come first there. */
  if (tms9918->config[PICO9918_CONF_DIAG] && PICO9918_HAS(tms9918, PICO9918_FEAT_OVERLAY))
  {
    PICO9918_FILL32_WAIT(PICO9918_FILL_BORDER);
    pico9918_diag_render(PICO9918_INST y + pico9918_v_border_impl(PICO9918_INST_ONLY),
                         params->vVirtualPixels, pixels);
  }

  PICO9918_LINE_NOTE_TIME(y, PICO9918_HOST_TIME_US() - lineStart);

  if (tms9918->config[PICO9918_CONF_DIAG_PERFORMANCE])
    pico9918_diag_update_render_time(renderTime, PICO9918_HOST_TIME_US() - lineStart);

  /* A long program's slice for this line; the arming write already ran the short ones. */
  pico9918_gpu_service(PICO9918_INST_ONLY);

  return false;
}

/* One stop over a whole line, borders included. Parity by output line, not repeat index -
   that is stuck at zero when vPixelScale is 1. */
static bool dimLine(PICO9918_INST_ARG PICO9918_PIXEL_T* pixels, uint32_t count,
                    uint32_t outputLine)
{
  if (!(outputLine & 1) || !tms9918->config[PICO9918_CONF_CRT_SCANLINES]) return false;

  uint32_t* pairs = (uint32_t*)pixels;
  for (uint32_t i = 0; i < count / 2; ++i) pairs[i] = PICO9918_PIXEL_PAIR_DIM(pairs[i]);
  return true;
}

/** \brief see the header. A repeat re-reads the host's buffer rather than re-rendering. */
PICO9918_DLLEXPORT
bool pico9918_frame_output_line(PICO9918_INST_ARG uint32_t outputLine,
                              pico9918_scanline_params_t* params, PICO9918_PIXEL_T* pixels)
{
  const uint32_t scale = pico9918_v_scale ? pico9918_v_scale : 1;
  bool           fresh = false;

  params->vVirtualPixels = pico9918_v_virtual;

  if (outputLine % scale == 0)
  {
    const uint16_t y = (uint16_t)(outputLine / scale);

    /* The border arm leaves the overlay to the caller so a host can put its own banner
       under it. With nothing to put there, draw it: the panels span the frame, and the
       border return is what says which lines were skipped. */
    if (pico9918_frame_scanline(PICO9918_INST y, params, pixels) &&
        tms9918->config[PICO9918_CONF_DIAG] && PICO9918_HAS(tms9918, PICO9918_FEAT_OVERLAY))
      pico9918_diag_render(PICO9918_INST y, params->vVirtualPixels, pixels);

    fresh = true;
  }

  bool changed = dimLine(PICO9918_INST pixels, params->hVirtualPixels, outputLine) || fresh;

#if PICO9918_BUILD_RUNTIME_CHIP
  /* An F18A shows its own power-on badge where a PICO9918 shows the splash. Here rather
     than in the scanline path so each row of it is one OUTPUT line, as the hardware's
     is, and last so nothing dims it - on an F18A the badge outranks the scanline dim as
     well as the picture. A repeated line still needs it drawn: the buffer it re-reads
     holds the row above. */
  if (tms9918->chip == PICO9918_CHIP_F18A)
    changed |= pico9918_f18a_badge_render((uint16_t)outputLine,
                                          pico9918_frame_count_impl(PICO9918_INST_ONLY), pixels);
#endif

  return changed;
}
