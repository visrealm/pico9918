/**
 * \file
 * \brief pico9918-core - the golden harness's deterministic clock
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Force-included (-include) into every TU of the golden build - the library AND
 * the harness - through the documented host-override mechanism (see
 * impl/platform.h).
 *
 * WHY THIS EXISTS
 *
 * The library's one wall-clock read is PICO9918_HOST_TIME_US(), which defaults to
 * time_us_32() - QueryPerformanceCounter / clock_gettime off-target, so neither of
 * the two library surfaces that read it is repeatable without this:
 *
 *   - the diagnostics panel's GPU% row, and the flt2Str / uint2Str plumbing behind
 *     it, which cannot be covered at all against a wall clock, and
 *   - the F18A reset/snap timer registers, which are device behaviour a host can
 *     read back rather than diagnostics.
 *
 * The counter below replaces the clock for both, which is the point of putting
 * the op on the whole library rather than in the overlay: a clock injected for
 * the panel alone would leave the timer registers on the wall clock and the
 * resulting flakiness would read as a harness bug.
 *
 * WHY A FIXED STEP AND NOT A CONSTANT
 *
 * A constant clock would make totalTime (currentTime - lastUpdateTime) ZERO on
 * the second and every later update, and the GPU% row divides by it - a
 * divide-by-zero producing inf/nan and, worse, a row whose glyphs would no
 * longer respond to the arithmetic being tested. A fixed nonzero STEP keeps the
 * elapsed interval both deterministic and representative.
 *
 * The step is a per-call increment, not a per-frame one, deliberately: the
 * library must not be able to tell how many times it read the clock, so no
 * call count is baked into a golden. Any read order still yields the same
 * sequence because the sequence is the only state.
 *
 * goldenClockReset() lets a scenario start the sequence from a known point, so
 * cases stay independent of each other and of scene order - the same property
 * overlayPrimeDiag exists to give the value strings.
 */

#pragma once

#include <stdint.h>

/*
 * 1000us per read. Chosen so the derived numbers are stable, nonzero and land
 * with digits in the positions the panel actually renders:
 *   - the GPU% row's divisor totalTime is a whole number of milliseconds, and
 *   - gpuTimeUs is 0 in the harness (pico9918_gpu_loop is never run here, so the
 *     accumulator is never fed), which makes the GPU% row a stable 0.000.
 * Not a power of two: a shift-vs-divide mutation must not be absorbed.
 */
#define GOLDEN_CLOCK_STEP_US  1000u

extern uint32_t goldenClockNow;

static inline uint32_t goldenClockTick(void)
{
  const uint32_t now = goldenClockNow;
  goldenClockNow = now + GOLDEN_CLOCK_STEP_US;
  return now;
}

static inline void goldenClockReset(void)
{
  goldenClockNow = 0;
}

#define PICO9918_HOST_TIME_US()  goldenClockTick()
