/**
 * \file
 * \brief pico9918-core - the golden harness's view of the status publish
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Force-included (-include) into every TU of the golden build - the library AND
 * the harness - the same way goldenClock.h is, and for the same reason: the op
 * being overridden expands inside the library's own TUs, so a harness-only define
 * would change nothing.
 *
 * WHY THIS EXISTS
 *
 * PICO9918_HOST_STATUS_VISIBLE() defaults to a no-op, so on desktop nothing can
 * see whether a newly latched status was ever handed to the host. That is the one
 * consequence of pico9918_frame_update_interrupts the frame group could not digest,
 * and it is exactly the consequence a publish the frame path decides to SKIP would
 * break. Recording the op closes that hole.
 *
 * WHAT IS RECORDED, AND WHAT IS DELIBERATELY NOT
 *
 * Only the SR0 value visible at the moment of the publish. The word the PICO9918
 * firmware actually pushes carries a pin-direction byte, a read-ahead byte and a
 * status-register select as well, but those are HOST policy - the library's own
 * meaning for this op is "the newly latched status is now readable" - so digesting
 * them here would pin firmware decisions in a library test.
 *
 * The call COUNT is recorded but must never be digested, for the reason
 * goldenClock.h gives about its own step: a golden that pins how many times the
 * library published bakes in the current call pattern and fails the moment someone
 * legitimately changes it. The contract is that the host's view is never STALE, not
 * that it is refreshed a particular number of times - so what a row digests is
 * goldenPublishedStatus against the merged SR0 it expects.
 *
 * goldenPublishReset() states the precondition a row starts from: on the device a
 * latched status has always been published by whoever latched it, so a row that
 * installs SR0 directly has to say the host was told, or every row would read as a
 * stale publish before the function under test has even run.
 */

#pragma once

#include <stdint.h>

extern uint8_t  goldenPublishedStatus;
extern uint32_t goldenPublishCount;

static inline void goldenNotePublish(uint8_t status)
{
  goldenPublishedStatus = status;
  ++goldenPublishCount;
}

static inline void goldenPublishReset(uint8_t status)
{
  goldenPublishedStatus = status;
  goldenPublishCount    = 0;
}

#define PICO9918_HOST_STATUS_VISIBLE() goldenNotePublish(TMS_STATUS(tms9918, 0))
