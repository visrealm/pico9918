/**
 * \file
 * \brief pico9918-core - GPU Implementation
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Purpose: TMS9900 GPU glue code (adapted from pico9918/src/gpu/gpu.c)
 *
 * Credits: JasonACT (AtariAge)
 *
 */

#include "gpu.h"
/* the private instance layout: this TU reaches TMS_REGISTER/TMS_STATUS and the
   struct directly, and the public GPU header does not supply them */
#include "impl/pico9918_priv.h"
#include "pico9918_config.h" /* PICO9918_CONF_* action keys */

#include <string.h> /* memcpy */

/* -------------------------------------------------------------------------
 * Platform-specific includes
 * ---------------------------------------------------------------------- */
#ifdef PICO_BUILD
#include "pico/stdlib.h"
#include "hardware/structs/mpu.h"
#include "hardware/sync.h"
#include <hardware/flash.h>
#include "pico.h" /* PICO_RP2040 */
#endif

#include "tms9900.h"
#include "impl/platform.h" /* PICO9918_HOST_TIME_US */

#if !PICO9918_GPU_BUDGETED
/* run9900() implemented in platform/thumb9900_{m0,m33}.S */
extern uint16_t run9900(uint8_t* memory, uint16_t pc, uint16_t wp, uint8_t* regx38);
#else
#if defined(TMS9900_WATCH_WRITES)
static void gpuDmaWatch(uint8_t* vram, uint32_t addr);
#endif

static uint16_t run9900Budget(uint8_t* mem, uint16_t pc, uint16_t wp, uint8_t* r38,
                              uint32_t budget, uint16_t* st, bool* outOfBudget)
{
  Tms9900Cpu cpu;
  tms9900_init(&cpu, mem, r38, pc, wp);
#if defined(TMS9900_WATCH_WRITES)
  cpu.onWrite = gpuDmaWatch;
#endif
  cpu.st = *st;
  const uint16_t next = run9900_budget_c(&cpu, budget, outOfBudget);
  *st                 = cpu.st;
  return next;
}
#endif

/* -------------------------------------------------------------------------
 * Config keys used to request config actions (save / forced save / pending
 * confirm / pending cancel). Semantics are owned by the host - the GPU loop
 * just detects a set key, clears it and notifies the host via the config
 * callback.
 * ---------------------------------------------------------------------- */
static const uint8_t configActionKeys[] = {PICO9918_CONF_SAVE_TO_FLASH, PICO9918_CONF_SAVE_FORCED, PICO9918_CONF_PENDING_CONFIRM,
                                           PICO9918_CONF_PENDING_CANCEL};

/* -------------------------------------------------------------------------
 * Callbacks (registered by the host application)
 * ---------------------------------------------------------------------- */
static void (*gpu_flash_cb)(void)                   = NULL;
static void (*gpu_config_save_cb)(uint8_t*, uint8_t) = NULL;

void pico9918_gpu_set_flash_callback(void (*cb)(void))
{
  gpu_flash_cb = cb;
}

void pico9918_gpu_set_config_save_callback(void (*cb)(uint8_t* config, uint8_t key))
{
  gpu_config_save_cb = cb;
}

/* -------------------------------------------------------------------------
 * Hard-fault handler (triggered by MPU for GPU DMA and palette writes)
 * ---------------------------------------------------------------------- */
#ifdef PICO_BUILD
static int didFault = 0;

void isr_hardfault(void)
{
  didFault                    = 1;
  TMS_REGISTER(tms9918, PICO9918_REG_GPU_CONTROL) = 0; /* Stop the GPU */
  mpu_hw->ctrl                = 0; /* Turn off memory protection - all models */
}
#endif /* PICO_BUILD */

/* -------------------------------------------------------------------------
 * Run a GPU DMA job
 * ---------------------------------------------------------------------- */

/* A transfer that runs off an end of the map. The engine's address register is 16 bits so
   it comes back at the other end, which a pointer walk cannot do and no correct program
   asks for - so this stays out of line rather than unrolling into the caller. */
static PICO9918_NOINLINE void dmaWrapped(uint8_t* vram, uint32_t src, uint32_t dst,
                                         uint32_t width, uint32_t height, int32_t advance,
                                         int32_t srcInc, int32_t dstInc)
{
  uint16_t s = (uint16_t)src;
  uint16_t d = (uint16_t)dst;
  for (uint32_t y = 0; y < height; ++y)
  {
    for (uint32_t x = 0; x < width; ++x, s += srcInc, d += dstInc) vram[d] = vram[s];
    s += advance * srcInc;
    d += advance * dstInc;
  }
}

static void triggerGpuDma(uint8_t* vram)
{
  uint32_t srcVramAddr = __builtin_bswap16(*(uint16_t*)(vram + 0x8000));
  uint32_t dstVramAddr = __builtin_bswap16(*(uint16_t*)(vram + 0x8002));
  const uint32_t widthByte = vram[0x8004];
  uint32_t width           = widthByte ? widthByte : 256;
  uint32_t height          = vram[0x8005] ? vram[0x8005] : 256;
  uint32_t stride          = vram[0x8006];
  uint32_t params          = vram[0x8007];

  int32_t dstInc = (params & 0x02) ? -1 : 1;
  int32_t srcInc = (params & 0x01) ? 0 : dstInc;

  uint8_t* srcPtr = vram + srcVramAddr;
  uint8_t* dstPtr = vram + dstVramAddr;

  const bool contiguous = stride == widthByte;
  const uint32_t rowStep = contiguous ? width : stride;

  const uint32_t run  = contiguous ? width * height : width;
  const uint32_t rows = contiguous ? 1 : height;

  const uint32_t reach    = (height - 1) * rowStep + width - 1;
  const uint32_t srcReach = srcInc ? reach : 0;
  const bool     inRange  = (dstInc < 0)
                              ? (dstVramAddr >= reach && srcVramAddr >= srcReach)
                              : (dstVramAddr + reach <= 0xFFFF && srcVramAddr + srcReach <= 0xFFFF);

  if (!inRange)
    dmaWrapped(vram, srcVramAddr, dstVramAddr, width, height,
               (int32_t)rowStep - (int32_t)width, srcInc, dstInc);
  else if (srcInc == 0 && dstInc == 1)
  {
    const uint8_t value = *srcPtr;
    for (uint32_t y = 0; y < rows; ++y, dstPtr += rowStep) memset(dstPtr, value, run);
  }
  else if (srcInc == 1 && (dstPtr >= srcPtr + run || srcPtr >= dstPtr + run))
  {
    for (uint32_t y = 0; y < rows; ++y, srcPtr += rowStep, dstPtr += rowStep)
      memcpy(dstPtr, srcPtr, run);
  }
  else
  {
    /* Signed, because a stride under the width steps back into the row just written. */
    const int32_t advance = (int32_t)rowStep - (int32_t)width;
    for (uint32_t y = 0; y < height; ++y)
    {
      for (uint32_t x = 0; x < width; ++x, srcPtr += srcInc, dstPtr += dstInc) *dstPtr = *srcPtr;
      srcPtr += advance * srcInc;
      dstPtr += advance * dstInc;
    }
  }

  *(uint16_t*)(vram + 0x8008) = 0;
}

#if defined(TMS9900_WATCH_WRITES)
/*
 * The MPU's job, done in software. Region 0 guards 32 bytes at 0x8000 and its
 * handler reads the trigger, so this is the same test at the same moment - which
 * is what lets a program start a transfer and carry straight on.
 */
static void gpuDmaWatch(uint8_t* vram, uint32_t addr)
{
  if ((addr & ~(uint32_t)0x1F) == 0x8000 && vram[0x8008]) triggerGpuDma(vram);
}
#endif

/* -------------------------------------------------------------------------
 * MPU guards (Pico only). Region 0 covers the GPU DMA port, region 1 the
 * palette - a GPU palette write has no other way of announcing itself.
 * ---------------------------------------------------------------------- */
#ifdef PICO_BUILD
volatile uint8_t pico9918_gpu_palette_guard_off = 0;

/* Fault on writes to a range; reads still pass. The range must not cross a 256-byte
   boundary, which the instance's own 256-byte alignment is what guarantees. */
static void PICO9918_IN_FLASH_FUNC(guard)(uint32_t region, void* a, uint32_t bytes)
{
  uintptr_t addr = (uintptr_t)a;
#if PICO_RP2040
  uint32_t base  = addr & (uint)~0xff;
  uint32_t first = (addr - base) >> 5;
  uint32_t last  = (addr + bytes - 1 - base) >> 5;
  uint32_t srd   = ~(((1u << (last - first + 1)) - 1u) << first) & 0xffu;

  mpu_hw->rbar = base | M0PLUS_MPU_RBAR_VALID_BITS | region;
  mpu_hw->rasr = 1 | (0x07 << 1) | (srd << 8) | 0x15000000; /* 256 bytes, privileged RO, XN */
#else
  mpu_hw->rnr  = region;
  mpu_hw->rbar = (addr & (uint)~31u) | (2u << M33_MPU_RBAR_AP_LSB) | M33_MPU_RBAR_XN_BITS;
  mpu_hw->rlar = ((addr + bytes - 1) & (uint)~31u) | M33_MPU_RLAR_EN_BITS;
#endif
}

static void __not_in_flash_func(guardEnable)(uint32_t region, bool on)
{
  mpu_hw->rnr = region;
#if PICO_RP2040
  if (on)
    mpu_hw->rasr |= M0PLUS_MPU_RASR_ENABLE_BITS;
  else
    mpu_hw->rasr &= ~M0PLUS_MPU_RASR_ENABLE_BITS;
#else
  if (on)
    mpu_hw->rlar |= M33_MPU_RLAR_EN_BITS;
  else
    mpu_hw->rlar &= ~M33_MPU_RLAR_EN_BITS;
#endif
}

/* the flag goes up before the region drops; the other order lets an interrupt
   re-arm and clear it, leaving the guard down with nothing to notice */
static void __not_in_flash_func(gpuPaletteFault)(PICO9918_INST_ONLY_ARG)
{
  tms9918->palDirty = 1;

  uint32_t save           = save_and_disable_interrupts();
  pico9918_gpu_palette_guard_off = 1;
  guardEnable(1, false);
  restore_interrupts(save);
}

void __not_in_flash_func(pico9918_gpu_rearm_palette_guard)(PICO9918_INST_ONLY_ARG)
{
  if (tms9918->palDirty) return;

  pico9918_gpu_palette_guard_off = 0;
  guardEnable(1, true);
  tms9918->palDirty = 1;
}
#endif /* PICO_BUILD */

/* -------------------------------------------------------------------------
 * Core GPU execution (non-inlined for stack safety)
 * ---------------------------------------------------------------------- */
static PICO9918_NOINLINE bool volatileHack(PICO9918_INST_ARG uint32_t budget)
{
  bool running     = false;
  bool outOfBudget = false;
  tms9918->restart = 0;
  if ((tms9918->gpuAddress & 1) == 0) /* Odd addresses crash the RP2040 */
  {
    uint16_t lastAddress = tms9918->gpuAddress;

#ifdef PICO_BUILD
  restart:
#endif
    TMS_REGISTER(tms9918, PICO9918_REG_GPU_CONTROL) = 1;
    TMS_STATUS(tms9918, PICO9918_SR_GPU) |= 0x80; /* Running */

#ifdef PICO_BUILD
#if PICO_RP2040
    mpu_hw->ctrl = M0PLUS_MPU_CTRL_PRIVDEFENA_BITS | M0PLUS_MPU_CTRL_ENABLE_BITS;
#else
    mpu_hw->ctrl = M33_MPU_CTRL_PRIVDEFENA_BITS | M33_MPU_CTRL_ENABLE_BITS;
#endif
#endif /* PICO_BUILD */

#if PICO9918_GPU_BUDGETED
    lastAddress = run9900Budget(tms9918->vram.bytes, lastAddress, 0xFFFE,
                                &TMS_REGISTER(tms9918, PICO9918_REG_GPU_CONTROL), budget, &tms9918->gpuStatus,
                                &outOfBudget);
#else
    (void)budget;
    lastAddress =
      run9900(tms9918->vram.bytes, lastAddress, 0xFFFE, &TMS_REGISTER(tms9918, PICO9918_REG_GPU_CONTROL));
#endif

#ifdef PICO_BUILD
    mpu_hw->ctrl = 0; /* Turn off memory protection - all models */
#endif

    if (TMS_REGISTER(tms9918, PICO9918_REG_GPU_CONTROL) & 1)
    {
      tms9918->gpuAddress = lastAddress;
      tms9918->restart    = 0;
      running             = outOfBudget;
    }
#ifdef PICO_BUILD
    if (didFault)
    {
      didFault = 0;
      if (tms9918->vram.bytes[0x8008])
        triggerGpuDma(tms9918->vram.bytes);
      else if (!pico9918_gpu_palette_guard_off)
        gpuPaletteFault(PICO9918_INST_ONLY);
      goto restart;
    }
#endif
  }
  if (running) return true;

  TMS_STATUS(tms9918, PICO9918_SR_GPU) &= ~0x80; /* Stopped */
  TMS_REGISTER(tms9918, PICO9918_REG_GPU_CONTROL) = 0;
  return false;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/*
 * Initialize the TMS9900 GPU
 */
void pico9918_gpu_init(PICO9918_INST_ONLY_ARG)
{
#ifdef PICO_BUILD
#if !PICO_RP2040
  mpu_hw->mair[0] = 0x44; /* normal non-cacheable, so a guarded read stays an ordinary load */
#endif
  guard(0, &(tms9918->vram.bytes[0x8000]), 32);
  guard(1, tms9918->vram.map.pram, 64 * sizeof(*tms9918->vram.map.pram));
#endif
}

/*
 * Cross-core, unguarded by design. Written by pico9918_gpu_loop (core 0 on Pico)
 * and read + reset from the frame/overlay side (core 1) via pico9918_gpu_time /
 * pico9918_gpu_reset_time. volatile so the compiler cannot cache or reorder the
 * accesses across the core boundary; the remaining race is a lost update of one
 * sample window, which is tolerable for a statistics readout.
 */
static volatile bool reportedBack  = true;
static volatile uint32_t gpuTimeUs = 0;

/*
 * Return GPU CPU time in microseconds.
 */
uint32_t pico9918_gpu_time(uint32_t totalTime)
{
  if (!reportedBack) return totalTime;
  return gpuTimeUs;
}

/*
 * Reset internal GPU time accumulator.
 */
void pico9918_gpu_reset_time(void)
{
  gpuTimeUs = 0;
}

/*
 * One service pass. Split out of pico9918_gpu_loop so a host without a core to
 * spare can run a GPU program and get control back.
 */
void pico9918_gpu_step(PICO9918_INST_ONLY_ARG)
{
  if (tms9918->restart)
  {
    reportedBack      = false;
    uint32_t gpuStart = PICO9918_HOST_TIME_US();
    volatileHack(PICO9918_INST 0);
    gpuTimeUs += PICO9918_HOST_TIME_US() - gpuStart;
  }
  reportedBack = true;

  if (tms9918->flash)
  {
    if (gpu_flash_cb) gpu_flash_cb();
  }

  for (int i = 0; i < (int)(sizeof(configActionKeys) / sizeof(configActionKeys[0])); ++i)
  {
    const uint8_t key = configActionKeys[i];
    if (tms9918->config[key])
    {
      tms9918->config[key] = 0;
      if (gpu_config_save_cb) gpu_config_save_cb(tms9918->config, key);
    }
  }
}

/*
 * The same pass, but capped, for a host that has only the one thread.
 *
 * The cap is what lets a caller interleave: a program that waits on the scanline at
 * >7000 cannot finish until something advances it, and nothing can while the core is
 * inside run9900. Returning with the PC kept is what makes the next call carry on.
 */
bool pico9918_gpu_step_n(PICO9918_INST_ARG uint32_t instructions)
{
  bool running = false;

  if (tms9918->restart)
  {
    reportedBack      = false;
    uint32_t gpuStart = PICO9918_HOST_TIME_US();
    running           = volatileHack(PICO9918_INST instructions);
    gpuTimeUs += PICO9918_HOST_TIME_US() - gpuStart;
    /* volatileHack clears it on the way in, so put it back for the next slice */
    if (running) tms9918->restart = 1;
  }
  reportedBack = !running;

  if (tms9918->flash)
  {
    if (gpu_flash_cb) gpu_flash_cb();
  }

  for (int i = 0; i < (int)(sizeof(configActionKeys) / sizeof(configActionKeys[0])); ++i)
  {
    const uint8_t key = configActionKeys[i];
    if (tms9918->config[key])
    {
      tms9918->config[key] = 0;
      if (gpu_config_save_cb) gpu_config_save_cb(tms9918->config, key);
    }
  }

  return running;
}

/*
 * GPU main loop - runs indefinitely, call from a dedicated core/thread.
 */
void pico9918_gpu_loop(PICO9918_INST_ONLY_ARG)
{
  while (1)
  {
    pico9918_gpu_step(PICO9918_INST_ONLY);
  }
}

/*
 * Instructions a scanline, from a rate. Never zero while a rate is set: a slice of
 * nothing would arm the GPU and never advance it, which is worse than running it too
 * fast. Sixty fields of 240 lines until the first pico9918_frame_end says otherwise -
 * the shape every mode is within a factor of two of.
 */
#define GPU_SLICE_FROM_IPS(ips, lines, hz) ((uint32_t)((ips) / ((lines) * (hz))) + 1u)

void pico9918_gpu_set_clock(PICO9918_INST_ARG uint32_t instructionsPerSecond)
{
#if PICO9918_GPU_BUDGETED
  tms9918->gpuIps   = instructionsPerSecond;
  tms9918->gpuSlice = instructionsPerSecond ? GPU_SLICE_FROM_IPS(instructionsPerSecond, 240u, 60u) : 0u;
#else
  /* refused, not honoured: a hand-written Thumb core runs to completion */
  (void)tms9918;
  (void)instructionsPerSecond;
#endif
}

#if PICO9918_GPU_BUDGETED

/* Re-derived per frame, because a mode change moves both the line count and, on a
   50Hz machine, the rate. Called from pico9918_frame_end. */
void pico9918_gpu_note_frame(PICO9918_INST_ARG uint32_t lines, float frameRateHz)
{
  if (!tms9918->gpuIps || !lines || frameRateHz < 1.0f) return;

  tms9918->gpuSlice = GPU_SLICE_FROM_IPS(tms9918->gpuIps, lines, (uint32_t)frameRateHz);
}

/*
 * One slice, for the library's own two service points: the register write that arms a
 * program, and each scanline while one is still running.
 *
 * Gated on the unlock rather than the personality: a program can only be armed on an
 * unlocked device, and stepping one down clears that flag - so this is also what stops
 * a program armed as an F18A from running on as a TMS9918A, which pico9918_set_chip
 * deliberately leaves to whoever runs the GPU.
 */
void pico9918_gpu_run_slice(PICO9918_INST_ONLY_ARG)
{
  if (PICO9918_UNLOCKED(tms9918)) pico9918_gpu_step_n(PICO9918_INST tms9918->gpuSlice);
}

#endif
