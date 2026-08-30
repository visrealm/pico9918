/**
 * \file
 * \brief pico9918-core - GPU Interface
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Purpose: Library-public interface to the TMS9900 GPU (F18A compatibility layer)
 *
 */

#pragma once

/*
 * Public surface only: out-of-line API needing nothing but the instance type and
 * the PICO9918_INST_* argument macros, both of which pico9918.h declares. Nothing
 * under impl/ may be included from here - pico9918_gpu_trigger needs the private
 * layout, so it lives on the privileged surface in impl/pico9918_gpu_priv.h.
 */
#include "pico9918.h"
#include "pico9918_build_config.h"

/* Nothing below is built into a TMS9918A library, so including this there would fail at
   link time with nothing to say why. */
#if PICO9918_BUILD_MODE == 0
#error "the GPU is an F18A feature - this library was built PICO9918_MODE=0"
#endif

/**
 * Initialize the TMS9900 GPU.
 * Must be called after pico9918_init() / pico9918_reset().
 */
PICO9918_DLLEXPORT
void pico9918_gpu_init(PICO9918_INST_ONLY_ARG);

#ifdef PICO_BUILD
/**
 * The palette guard, as much of it as the host has to see. A GPU palette write
 * has no other way of announcing itself, so an MPU region faults on it, marks
 * the palette dirty and takes itself out of the way until the renderer has taken
 * the flag. Putting it back is the host's to schedule, because the MPU belongs
 * to the core running the GPU, and the only place that core is reliably idle is
 * its scanline interrupt: read the flag from there, and call the re-arm.
 */
extern volatile uint8_t pico9918_gpu_palette_guard_off;
/** \brief put the palette guard back, from the core that owns the MPU */
PICO9918_DLLEXPORT
void pico9918_gpu_rearm_palette_guard(PICO9918_INST_ONLY_ARG);
#endif

/**
 * GPU main loop - call from a dedicated core/thread.
 * Runs indefinitely; processes GPU programs, flash requests, and config saves.
 */
PICO9918_DLLEXPORT
void pico9918_gpu_loop(PICO9918_INST_ONLY_ARG);

/**
 * One pass of that loop: run a pending trigger to completion, then dispatch any
 * flash and config-action requests. Returns.
 *
 * It is the loop's body rather than a second copy of it, so a program run this
 * way is run by the same code the device runs it with, and it is timed into the
 * same accumulator pico9918_gpu_time reads.
 *
 * How long it takes is the program's business: run9900 returns on IDLE or when
 * the program clears its own run flag (TMS register 0x38 bit 0), and a program
 * that does neither does not return.
 *
 * Which makes this the wrong entry for a host with one thread, however much it
 * looks like the right one. A program may WAIT on the display - the scanline
 * being scanned out is readable at >7000 - and the caller that would advance the
 * raster is the one blocked in here. Use pico9918_gpu_step_n for that, or give
 * the GPU a thread and render on the one you have.
 */
PICO9918_DLLEXPORT
void pico9918_gpu_step(PICO9918_INST_ONLY_ARG);

/**
 * The same pass, capped at `instructions`, returning true while the program still
 * has work left. Zero means no cap, which is pico9918_gpu_step().
 *
 * This is the entry for a host with one thread. pico9918_gpu_step() cannot come back
 * until the program stops itself, so a program that waits on the scanline at >7000 -
 * to page a bitmap in the vertical blank, say - would wait forever: the caller that
 * would advance the raster is the one blocked inside it. Capped, the caller gets
 * control back with the PC kept, renders, and calls again:
 *
 *     while (pico9918_gpu_step_n(PICO9918_INST 20000))
 *       renderOneScanline();
 *
 * A host with a thread to spare wants pico9918_gpu_loop() on it instead, which is
 * what the firmware does. Both shapes are real; this one asks nothing of the host
 * but a loop.
 *
 * Only the portable C core counts instructions. On a board built with the
 * hand-written Thumb core the cap is ignored and this runs to completion - which
 * costs that build nothing, because it has a core to give the GPU.
 */
PICO9918_DLLEXPORT
bool pico9918_gpu_step_n(PICO9918_INST_ARG uint32_t instructions);

/**
 * Return the GPU's CPU time in microseconds.
 * If the GPU is still running (hasn't reported back), returns totalTime.
 *
 * CROSS-CORE: the accumulator and its reported-back flag are written by
 * pico9918_gpu_loop - core 0 on Pico - while these two calls are made from the
 * frame/overlay side on core 1. Both are volatile and neither call is guarded:
 * the worst case is one sample window's update being lost, which is acceptable
 * for a statistics readout and cheaper than a critical section per frame.
 */
PICO9918_DLLEXPORT
uint32_t pico9918_gpu_time(uint32_t totalTime);

/**
 * Reset the internal GPU time accumulator to 0.
 * Cross-core, unguarded - see pico9918_gpu_time.
 */
PICO9918_DLLEXPORT
void pico9918_gpu_reset_time(void);

/**
 * Register a callback that will be invoked when the GPU wants to flash a sector.
 * Pass NULL to disable.
 */
PICO9918_DLLEXPORT
void pico9918_gpu_set_flash_callback(void (*cb)(void));

/**
 * Register a callback that will be invoked when the GPU loop detects a config
 * action request. The callback receives the config array pointer and the
 * config key that fired (save / forced save / pending confirm / pending
 * cancel - semantics are owned by the host). The key is cleared before the
 * callback is invoked.
 * Pass NULL to disable.
 */
PICO9918_DLLEXPORT
void pico9918_gpu_set_config_save_callback(void (*cb)(uint8_t* config, uint8_t key));
