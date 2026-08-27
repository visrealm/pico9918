/**
 * \file
 * \brief pico9918-core - Platform Abstraction
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Dispatch header. Selects the platform implementation and declares the
 * platform-independent parts of the contract.
 *
 * A host may pre-define any PICO9918_* macro before including the library to
 * override a desktop default (a real lock, a different pixel type, ...).
 * The Pico implementation is not overridable - it must emit exact codegen.
 */

#pragma once

#include <stdint.h>

#ifdef PICO_BUILD
#include "platform/pico/platform_pico.h"
#else
#include "platform/desktop/platform_std.h"
#endif


/*
 * PICO9918_INLINE / PICO9918_INLINE_HOT - the library's header-inline linkage.
 *
 * BOTH ARE `static inline`, and the static is load-bearing. A non-static C99
 * `inline` in a header is an inline DEFINITION with no external definition anywhere
 * in the library: at -O2 every call inlines and nothing is emitted, but at -O0 GCC
 * emits calls to symbols that do not exist and the link fails. That failure is
 * invisible on Pico, where the SDK's __force_inline forces every call to inline
 * regardless. Static is what makes an unoptimized desktop build link, and it is
 * correct on both platforms.
 *
 * TWO forms, because collapsing them into one changes shipping codegen:
 *
 *   PICO9918_INLINE      static inline. Left to the compiler.
 *   PICO9918_INLINE_HOT  static inline + always_inline.
 *
 * THE SPLIT IS NOT A JUDGEMENT ABOUT HOTNESS. Read HOT as "always_inline is
 * required here", not as "this one is hot" - and do not add it anywhere new, which
 * would be a codegen change rather than a cleanup.
 *
 * WHERE IT IS LOAD-BEARING, measured rather than assumed:
 *
 *   - the file-local helpers in pico9918.c are the F18A sprite and tile renderers.
 *     Dropping the attribute out-lines renderSprites and renderEcmTile, adds `bl`
 *     calls inside the per-scanline mode-ops stagers, and dissolves
 *     pico9918_output_sprites into its callers. NONE of that is visible to the asm
 *     gate in tools/capture-baselines.sh - no affected symbol is in its FUNCS list.
 *   - on the *Impl surface it matters to the status-read chain: without it
 *     pico9918_status_read_core and pico9918_status_read_reconcile_impl both grow
 *     the read IRQ handler, while pico9918_read_ahead_data_impl on the same
 *     handler's other arm makes no difference at all. It is NOT predictable from
 *     the call site: measure the one function before dropping an attribute.
 *
 * MSVC has no always_inline attribute (__forceinline is a different spelling in a
 * different position) and the MSVC path is desktop-only, so HOT degrades to that
 * spelling there - the same treatment the desktop header gives every __attribute__.
 */
#define PICO9918_INLINE static inline

#if defined(_MSC_VER) && !defined(__clang__)
#define PICO9918_INLINE_HOT static __forceinline
#else
#define PICO9918_INLINE_HOT static inline __attribute__((always_inline))
#endif

/*
 * The rest of the compiler directives the library needs, in both spellings.
 * Every one is a codegen hint with no bearing on semantics, so where MSVC has
 * no equivalent the macro is simply empty - MSVC does no type-based alias
 * analysis, and an alignment assumption it cannot be told is only a missed
 * optimisation.
 *
 * PICO9918_ASSUME_ALIGNED yields the pointer itself there, so every call site
 * casts the result rather than relying on the builtin's void*.
 */
#if defined(_MSC_VER) && !defined(__clang__)
#define PICO9918_NOINLINE __declspec(noinline)
#define PICO9918_MAY_ALIAS
#define PICO9918_ASSUME_ALIGNED(ptr, n) (ptr)
#else
#define PICO9918_NOINLINE               __attribute__((noinline))
#define PICO9918_MAY_ALIAS              __attribute__((may_alias))
#define PICO9918_ASSUME_ALIGNED(ptr, n) __builtin_assume_aligned((ptr), (n))
#endif

/*
 * Host ops header: how a host supplies its own tier-1 ops.
 *
 * A host that owns state a tier-1 op must reach - the PICO9918's PIO read-ahead
 * push needs `nextValue` and the read SM - names its ops header here, and the
 * macros expand inside the library TU rather than becoming a call. The firmware
 * passes -DPICO9918_HOST_OPS_HEADER=... from its root CMakeLists.
 *
 * Included AFTER the platform header so a host op can override a platform
 * default, and before the ops defaults below so those stay the fallback.
 *
 * The macros here may reference library state (`tms9918`, TMS_REGISTER,
 * TMS_STATUS) that is declared later, in pico9918_priv.h - that is fine and
 * intentional: a macro body binds its names where it EXPANDS, and every
 * expansion site is inside a TU that has already included Priv.h.
 */
#ifdef PICO9918_HOST_OPS_HEADER
#include PICO9918_HOST_OPS_HEADER
#endif

/*
 * Interlock: prove the host's ops header actually arrived.
 *
 * Two ways a build can silently lose its tier-1 ops, both demonstrated by review
 * rather than imagined, and both of which compile and link cleanly:
 *
 *  1. The quoted #include above searches the INCLUDING file's directory first, so
 *     a file of the same name sitting next to this header outranks the -I path
 *     that was meant to supply it. A planted decoy defining the ops as no-ops
 *     produced a firmware with no critical section and no read-ahead push, and
 *     every tracked asm baseline still read exactly as expected.
 *  2. If PICO9918_HOST_OPS_HEADER is simply not passed, "no ops header" is
 *     indistinguishable from "desktop", and the defaults below apply.
 *
 * Either way a Pico build loses the IRQ-disable window around the interrupt
 * latch merge - a rare, hard-to-reproduce corruption with no diagnostic. So a
 * real ops header must announce itself, and on a Pico build its absence is a
 * hard error rather than a silent downgrade to no-ops.
 */
#if defined(PICO_BUILD) && !defined(PICO9918_HOST_OPS_SUPPLIED)
#error \
  "Pico build without host tier-1 ops: PICO9918_HOST_OPS_HEADER was not supplied, or the header that was included is not the host's (a same-named file beside this header shadows it). The ops header must #define PICO9918_HOST_OPS_SUPPLIED 1."
#endif

/*
 * Tier-1 host op defaults: the critical section and the status-visible publish.
 * Both are no-ops unless a host supplies them.
 *
 * ENTER/EXIT_CRITICAL guard the frame interrupt/status update against
 * *same-core IRQ preemption* by the host's bus-interface handlers. The no-op
 * mapping is correct only for a host where bus-access calls (WriteData /
 * WriteAddr / ReadStatus / ...) and frame-advance calls are never concurrent. A
 * multithreaded emulator must serialize those externally or define a real lock
 * here.
 *
 * STATUS_VISIBLE marks the point at which a newly latched status becomes
 * readable by the host CPU. A polling host needs nothing; a host that pre-loads
 * a bus response (the PICO9918's PIO read-ahead) does its push here.
 */
#ifndef PICO9918_HOST_ENTER_CRITICAL
#define PICO9918_HOST_ENTER_CRITICAL() ((void)0)
#endif

#ifndef PICO9918_HOST_EXIT_CRITICAL
#define PICO9918_HOST_EXIT_CRITICAL() ((void)0)
#endif

#ifndef PICO9918_HOST_STATUS_VISIBLE
#define PICO9918_HOST_STATUS_VISIBLE() ((void)0)
#endif


/*
 * Optional per-scanline capture seams for a host test harness (the PICO9918's
 * test/live). Undefined by default and compiled out entirely; a host that wants
 * them defines them in its tier-1 ops header - which is why they are defaulted
 * HERE, after that header is included, and not in either platform header, where
 * the default would already have won by the time the host got a say. Never
 * enable them in a build you intend to take timing readings from: the capture
 * costs about a microsecond a line, which is what PICO9918_LINE_NOTE_TIME is there
 * to let the harness record.
 */
#ifndef PICO9918_LINE_CAPTURE_WAIT
#define PICO9918_LINE_CAPTURE_WAIT() ((void)0)
#endif

#ifndef PICO9918_LINE_CAPTURE
#define PICO9918_LINE_CAPTURE(y, height, width, indices) ((void)0)
#endif

#ifndef PICO9918_LINE_NOTE_TIME
#define PICO9918_LINE_NOTE_TIME(y, us) ((void)0)
#endif


/*
 * PICO9918_HOST_TIME_US - the library's only wall-clock read.
 *
 * Defaulted here rather than guarded inside platform/desktop for two reasons.
 * It is platform-NEUTRAL: the Pico and desktop platforms both supply a
 * time_us_32(), so one default covers both and the override point is the same
 * on either. And it is defaulted AFTER the platform headers, so a host that
 * substitutes a clock does not have to know which platform it is displacing.
 *
 * On a Pico build this expands to the SDK's time_us_32() verbatim - no wrapper,
 * no indirection, identical codegen to reading it directly.
 *
 * Two library surfaces read it, and BOTH must see the same clock:
 *
 *   - the diagnostics panel's GPU% row (overlay/diag.c), and
 *   - the F18A reset/snap TIMER REGISTERS (pico9918.c), which are device
 *     behaviour a host can read back, not diagnostics.
 *
 * That is why the op is whole-library and not overlay-local: a clock injected
 * for the overlay alone would leave the timer registers on the wall clock, and
 * a test surface covering them would be nondeterministic for a reason that
 * looks like a harness bug. See test/golden/goldenClock.h.
 */
#ifndef PICO9918_HOST_TIME_US
#define PICO9918_HOST_TIME_US() time_us_32()
#endif


/*
 * PICO9918_RGB12_FROM_RGB333 - widen a 3-bit-per-channel value to RGB444.
 *
 * Bit replication `(v << 1) | (v >> 2)` maps 0..7 onto 0..15 with the endpoints
 * exact (7 -> 15); a plain `v << 1` would cap full intensity at 14 and dim the
 * whole palette. Used by V9938 palette-port writes so that RGB444 (0x0RGB)
 * stays the library's single canonical colour format and the palette rebuild
 * path needs no per-base variant.
 */
#define PICO9918_RGB333_CH(v) (((v) << 1) | ((v) >> 2))

#define PICO9918_RGB12_FROM_RGB333(v) \
  ((uint16_t)((PICO9918_RGB333_CH(((v) >> 6) & 0x07) << 8) | (PICO9918_RGB333_CH(((v) >> 3) & 0x07) << 4) | \
              (PICO9918_RGB333_CH(((v)) & 0x07))))


/*
 * Palette LUT build class.
 *
 * The indexed scanline buffer is always a 256-byte stream consumed through a
 * 256-entry LUT; only the *meaning* of a byte changes per mode. That makes this
 * a three-way class resolved once per palette rebuild - never per pixel - and
 * not a doubled/undoubled boolean.
 *
 *   PICO9918_LUT_DOUBLED - every entry is the same pixel twice (all doubled modes)
 *   PICO9918_LUT_PAIRED  - each byte is two adjacent 4-bit indexes (TEXT80 today;
 *                        V9938 G5/G6 later)
 *   PICO9918_LUT_DIRECT  - the byte is the colour itself, via a fixed conversion
 *                        table rather than a palette (V9938 G7)
 */
typedef enum
{
  PICO9918_LUT_DOUBLED = 0,
  PICO9918_LUT_PAIRED,
  PICO9918_LUT_DIRECT
} pico9918_lut_class_t;
