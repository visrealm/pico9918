/**
 * \file
 * \brief pico9918-core - host hooks that make a desktop capture the same artifact as a board's
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * The device's harness takes a row through PICO9918_LINE_CAPTURE, which the platform
 * header leaves as a no-op unless a host defines it - and says so: those hooks are
 * "test/live"'s, defaulted after this header is included so a host gets first say.
 *
 * Defining it here is the whole reason a desktop capture is the SAME artifact as a
 * device one. The row arrives from the identical call site inside
 * pico9918_frame_scanline, from the identical pointer, rather than from a second
 * render loop that would have to be kept in step with it by hand.
 *
 * PICO9918_LINE_NOTE_TIME is deliberately left as the no-op. Microseconds off a PC
 * mean nothing here, and a hook that reported them would invite a comparison
 * against the device's numbers.
 */

#pragma once

#include <stdint.h>

void liveDesktopCaptureRow(uint16_t y, uint16_t height, uint16_t width, const uint8_t* indices);

#define PICO9918_LINE_CAPTURE(y, height, width, indices) \
  liveDesktopCaptureRow((y), (height), (width), (indices))

/* the interlock in platform.h: a real ops header announces itself, so a
   decoy of the same name beside the platform header cannot silently win */
#define PICO9918_HOST_OPS_SUPPLIED 1
