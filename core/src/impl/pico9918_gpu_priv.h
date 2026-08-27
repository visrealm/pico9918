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
