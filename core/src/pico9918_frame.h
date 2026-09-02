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
 * The frame module owns per-frame and per-scanline sequencing.
 */

#ifndef _PICO9918_FRAME_H
#define _PICO9918_FRAME_H

#include "pico9918.h"

/* PICO9918_PIXEL_T, for the scanline entry point's output buffer. Same dependency
   overlay/splash.h already takes, and for the same reason - the pixel type
   is host policy and lives on the platform surface. */
#include "impl/platform.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * \brief merge newly raised status flags into the SR0 latch, publish it, and
   * bring the /INT pin into agreement
   *
   * tempStatus: flags raised by the scanline just rendered (STATUS_INT / _5S / _COL
   *             plus, when 5S is newly set, the sprite number in the low 5 bits)
   *
   * Runs on every active scanline and at the end-of-frame trigger line. The body
   * runs inside PICO9918_HOST_ENTER/EXIT_CRITICAL because the host's bus-interface
   * handlers mutate the same latch - see the threading contract in
   * impl/platform.h.
   */
  PICO9918_DLLEXPORT
  void pico9918_frame_update_interrupts(PICO9918_INST_ARG uint8_t tempStatus);

  /**
   * \brief vertical porch: blank the display and park the scanline counter at
   * the F18A vsync value
   *
   * Called once per frame, at the start of the vertical porch. Touches only VDP
   * state, so it needs nothing from the host.
   */
  PICO9918_DLLEXPORT
  void pico9918_frame_porch(PICO9918_INST_ONLY_ARG);

  /**
   * \brief raise this frame's end-of-frame interrupt: latch doneInt, set the SR1
   * vsync bit, trigger the GPU if R50 bit 5 asks for it, consume a pending config
   * change, then merge STATUS_INT into the SR0 latch
   *
   * Unconditional - the caller decides whether the frame still owes an interrupt by
   * testing pico9918_frame_done_int_impl(). Called at the trigger line (from
   * pico9918_frame_end_of_scanline) and again as a fallback at true end of frame, for a
   * frame whose trigger line was never reached.
   *
   * Exported as part of the frame contract: a host driving frames itself needs the
   * fallback, though every caller in this tree is inside the module.
   */
  PICO9918_DLLEXPORT
  void pico9918_frame_raise_end_of_frame_int(PICO9918_INST_ONLY_ARG);

  /**
   * \brief end-of-frame trigger line: if this frame has not raised its interrupt
   * yet, account for the dropped frame and raise the interrupt
   *
   * Called once per frame, from the trigger scanline the host was told to fire on
   * (the first line past the display region). Does nothing on a frame whose
   * interrupt was already raised.
   *
   * The VGA callback that reaches this passes the display line; it is not a
   * parameter here because the body never used it.
   */
  PICO9918_DLLEXPORT
  void pico9918_frame_end_of_scanline(PICO9918_INST_ONLY_ARG);

  /**
   * \brief the host's mutable vertical display parameters, as the end-of-frame
   * geometry sees them
   *
   * FIELD WIDTHS ARE DELIBERATE and must not be widened for tidiness. They mirror
   * the host's own declarations because the arithmetic below NARROWS through them:
   * SCART NTSC in row-30 mode computes a negative border, which the uint32_t in
   * pico9918_frame_geometry_t then converts to 4294967286. That conversion is the
   * shipping behaviour and is pinned by the golden frame surface
   * (geom-scart-ntsc-row30); a model using int throughout would compute -10, a
   * value no device ever produces.
   *
   * Ownership split, which is the whole reason this is in/out rather than in:
   *   displayPixels   host-owned input - the mode's vertical active line count
   *   interlaced      host-owned input - selects the scale the geometry runs at
   *   vPixelScale     host-owned under interlace; REWRITTEN by the library when the
   *   vVirtualPixels  build is progressive (yScale > 1). See pico9918_frame_geometry.
   */
  typedef struct
  {
    int displayPixels;       /**< vertical active lines of the host's mode (in) */
    bool interlaced;         /**< (in) */
    uint8_t vPixelScale;     /**< (in, and out when yScale > 1) */
    uint16_t vVirtualPixels; /**< (in, and out when yScale > 1) */
  } pico9918_frame_display_t;

  /**
   * \brief the vertical geometry the end of frame derives
   *
   * vBorder is UNSIGNED on purpose - see the narrowing note above.
   * triggerScanline inherits the same wrap.
   */
  typedef struct
  {
    int vPixels;              /**< active VDP display lines */
    uint32_t vBorder;         /**< top border offset, in virtual lines */
    uint32_t triggerScanline; /**< vBorder + vPixels */
  } pico9918_frame_geometry_t;

  /**
   * \brief recompute the vertical display geometry from R0's double-rows bit and
   * R49's row-30 bit, and publish it as this module's vPixels / vBorder
   *
   * display: the host's mutable vertical parameters, read and - on a progressive
   *          build only - written. Under interlace (yScale 1) vPixelScale and
   *          vVirtualPixels are NOT touched: the host set them up and owns them.
   *
   * Returns the derived geometry, including the scanline the host must arm its
   * end-of-frame trigger on. The host applies both: any rewritten display fields
   * and the trigger line are host plumbing (a VGA parameter block and a trigger
   * register here), so the library computes and returns rather than reaching out.
   *
   * Called once per frame from pico9918_frame_end, and exported so the golden frame
   * surface can drive it directly - it is the surface's candidate path.
   */
  PICO9918_DLLEXPORT
  pico9918_frame_geometry_t pico9918_frame_geometry(PICO9918_INST_ARG pico9918_frame_display_t* display);

  /**
   * \brief true end of frame: advance the frame counter, fold in this frame's
   * temperature reading, latch the first display enable, refresh the diagnostics
   * panel, raise a still-owed end-of-frame interrupt, and recompute the geometry
   *
   * tempC:   this frame's core temperature in degrees C. The host owns the sensor;
   *          the averaging cadence and the SR13 publish are the library's.
   * frameRateHz: the host's display timing, needed by the diagnostics panel. Read
   *          only when the panel is enabled, but passed unconditionally - it is a
   *          register-resident float on the host side, so a conditional read would
   *          cost the host a branch to save nothing.
   * display: as pico9918_frame_geometry, which this calls last.
   *
   * Returns that geometry, so the host applies the display fields and arms the
   * trigger line exactly once per frame.
   *
   * The late config reload reaches the host through the tier-2 hook below, NOT
   * through a flag tested on return: it must land at its original point in this
   * sequence, ahead of the diagnostics refresh and the interrupt fallback, both of
   * which read config bytes the reload rewrites.
   */
  PICO9918_DLLEXPORT
  pico9918_frame_geometry_t pico9918_frame_end(PICO9918_INST_ARG float tempC, float frameRateHz,
                                         pico9918_frame_display_t* display);

/**
 * \brief frames of startup grace before an un-enabled display is taken to mean
 * "nothing is driving this VDP", at which point the splash gives way to the
 * diagnostics screen
 *
 * Both halves of that behaviour are inside this module: the end of frame tests it
 * to decide whether the config needs reloading once the display finally comes up,
 * and the scanline's border path tests it to force the PICO9918_CONF_DIAG* bytes
 * on. They must use the SAME number - a reload that fires at a different frame
 * than the forcing did would either restore settings that were never overridden
 * or leave overridden ones in place.
 *
 * Exported because it is the documented length of the startup grace period, and a
 * host driving frames itself has no other way to know when the diagnostics screen
 * takes over.
 */
#define PICO9918_FRAME_STARTUP_FRAMES 900

  /**
   * \brief the host's per-call display parameters, as the scanline sees them
   *
   * Per-call rather than stored, because that is the shape the host already had: the
   * VGA layer hands its parameter block to every scanline callback, so the values are
   * in registers at the call. A setter would add a store per mode change and a load
   * per scanline to buy nothing.
   *
   * WIDTHS MIRROR THE HOST'S OWN DECLARATIONS - see the same note on
   * pico9918_frame_display_t. hVirtualPixels feeds the border-fill count and the
   * half-border offset; vVirtualPixels reaches the splash geometry; the interlace pair
   * selects the field mapping.
   */
  typedef struct
  {
    uint16_t hVirtualPixels; /**< full scanline width, guard pixels excluded */
    uint16_t vVirtualPixels; /**< virtual lines per field */
    bool interlaced;              /**< the host's mode is interlaced */
    uint8_t interlacedFieldOrder; /**< 0 or 1: XOR'd with the field number */
  } pico9918_scanline_params_t;

  /**
   * \brief generate one display scanline: border fill or active render, the F18A
   * scanline and blanking registers, the R19 line interrupt, the GPU trigger, the
   * splash and the palette LUT maintenance
   *
   * y:      the display line. For an interlaced mode bit 12 carries the field number
   *         and bits 11-0 the line within the field, which is the encoding the host's
   *         VGA layer already uses.
   * params: the host's display parameters for this line (above).
   * pixels: the host's scanline buffer, at least hVirtualPixels wide.
   *
   * Returns TRUE when the BORDER path was taken. The host needs exactly this and
   * nothing more: it draws its own overlays after this call, and one of them (the
   * host's pending-display banner) must appear on border lines only. Returning
   * the flag is what keeps the border test in one place - a host re-deriving it from
   * the geometry would be a second copy of the rule, and in row-30 progressive mode
   * (vBorder == 0) the top rows the banner sits on are ACTIVE, so a host that simply
   * assumed "low y is border" would paint over the display.
   *
   * __time_critical_func: this is the per-scanline path, and it must stay in RAM.
   * The attribute is on the DEFINITION (a Pico build compiles this module with
   * copy_to_ram anyway, but the attribute is what pins it if that ever changes).
   */
  PICO9918_DLLEXPORT
  bool pico9918_frame_scanline(PICO9918_INST_ARG uint16_t y, const pico9918_scanline_params_t* params,
                             PICO9918_PIXEL_T* pixels);

  /**
   * \brief generate one OUTPUT line - the entry for a host that scans out a fixed frame
   *
   * `outputLine` runs 0 to the host's output height (480 for VGA) in every mode, so a host
   * never sees vPixelScale, double rows, the CRT-scanlines setting or the overlay.
   *
   * Returns whether `pixels` changed; false means a host's converted copy still stands.
   * `params->vVirtualPixels` is an output here. Progressive hosts only - an interlaced one
   * drives pico9918_frame_scanline per field.
   */
  PICO9918_DLLEXPORT
  bool pico9918_frame_output_line(PICO9918_INST_ARG uint32_t outputLine,
                                pico9918_scanline_params_t* params, PICO9918_PIXEL_T* pixels);

  /**
   * \brief one pixel from the buffer above as 0x00RRGGBB
   *
   * A rendered pixel is the board's: BGR12, four bits a channel, red's nibble
   * lowest. A host blitting to a 32-bit surface converts through here rather than
   * unpacking the nibbles itself - OR in whatever alpha its format wants.
   */
  static inline uint32_t pico9918_pixel_rgb888(PICO9918_PIXEL_T pixel)
  {
    const uint32_t p = (uint32_t)pixel;
    return ((p & 0x00f) * 0x11u) << 16 | (((p >> 4) & 0x00f) * 0x11u) << 8 | ((p >> 8) & 0x00f) * 0x11u;
  }

  /**
   * \brief register the host's late-config-reload hook
   *
   * Fires from pico9918_frame_end, once, on the frame that first sees the display
   * enabled - and only if that happened later than PICO9918_FRAME_STARTUP_FRAMES
   * frames into the run, i.e. the user sat on the diagnostics screen long enough
   * for it to have been forced on. Reloading the stored config is what puts the
   * diagnostics bytes (and everything else) back to what the user actually saved.
   *
   * A function pointer rather than a tier-1 op because the host's implementation
   * is a FLASH read: it cannot be a macro expanded into a library TU, and it is
   * per-run rather than per-frame - it can fire at most once between console
   * resets. NULL (the default) means the host has no stored config to reload and
   * nothing is called.
   *
   * A host that reads flash here should know it is called from the per-frame
   * path, so the read lands inside a frame and collides with XIP.
   */
  PICO9918_DLLEXPORT
  void pico9918_frame_set_config_reload_callback(void (*cb)(void));

#ifdef __cplusplus
}
#endif

#endif // _PICO9918_FRAME_H
