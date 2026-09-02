/**
 * \file
 * \brief pico9918-core - GPU privileged surface
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * The GPU entries that need the private instance layout, kept out of gpu/gpu.h so
 * that header stays public. The include rule this serves: an emulator includes
 * pico9918.h plus the platform/host header and nothing under impl/. Putting the
 * one-line pico9918_gpu_trigger below on the public header would pull the whole
 * private struct onto that surface for a single store.
 *
 * Privileged, like the rest of impl/. Do not include from an emulator.
 */

#pragma once

#include "pico9918_priv.h"

/*
 * Trigger the GPU to (re)start execution at the current gpuAddress.
 * Safe to call from the main/IRQ context.
 *
 * Every caller is inside the library: pico9918_frame.c is the only user, and it
 * already includes impl/.
 */
PICO9918_INLINE void pico9918_gpu_trigger(PICO9918_INST_ONLY_ARG)
{
  tms9918->restart = 1;
}

/*
 * Everything below is gpu.c's, and gpu.c is not in a TMS9918A library - so a MODE=0
 * build must not see even a declaration it could call and fail to link.
 */
#if PICO9918_MODE == PICO9918_MODE_F18A && PICO9918_GPU_BUDGETED

/* A slice for an armed program. In gpu.c because it steps the core; the gpuSlice test
   is here so a host driving the GPU itself pays a load and a branch, not a call. */
void pico9918_gpu_run_slice(PICO9918_INST_ONLY_ARG);

/* Re-derive the slice from the rate for this frame's line count and refresh. */
void pico9918_gpu_note_frame(PICO9918_INST_ARG uint32_t lines, float frameRateHz);

PICO9918_INLINE void pico9918_gpu_service(PICO9918_INST_ONLY_ARG)
{
  if (tms9918->gpuSlice) pico9918_gpu_run_slice(PICO9918_INST_ONLY);
}

#else

/* A TMS9918A has no GPU to arm, and an unbudgeted core is a board's, which runs one on
   a core of its own - so the call sites stay unguarded and these fold away, rather than
   leaving the scanline path a test that can never be true. */
PICO9918_INLINE void pico9918_gpu_service(PICO9918_INST_ONLY_ARG)
{
  (void)tms9918;
}

PICO9918_INLINE void pico9918_gpu_note_frame(PICO9918_INST_ARG uint32_t lines, float frameRateHz)
{
  (void)tms9918;
  (void)lines;
  (void)frameRateHz;
}

#endif
