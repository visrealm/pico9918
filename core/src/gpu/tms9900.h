/**
 * \file
 * \brief pico9918-core - TMS9900 CPU interpreter (portable C)
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * This is a full reimplementation of JasonACT's RP2040 thumb assembly core
 * found in thumb9900_m0.S / thumb9900_m33.S. It is not cycle-accurate and
 * intentionally mirrors the status-flag encoding used in the assembly
 * (bits: LG=0x80, AG=0x40, EQ=0x20, C=0x10, OV=0x08, P=0x04) so existing
 * GPU glue can remain unchanged. run9900_c takes a Tms9900Cpu the caller has
 * initialised with tms9900_init; gpu.c adapts it to the assembly core's
 * four-argument run9900 signature. PICO9918_GPU_C_CORE selects between them.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Status flag bits (mirrors the assembly core's layout) */
#define TMS_ST_LGT 0x80 /* Logic greater-than */
#define TMS_ST_AGT 0x40 /* Arithmetic greater-than */
#define TMS_ST_EQ  0x20 /* Equal */
#define TMS_ST_C   0x10 /* Carry */
#define TMS_ST_OV  0x08 /* Overflow */
#define TMS_ST_P   0x04 /* Parity (odd) */

/*
 * Off-target, nothing watches memory for the library the way a Pico's MPU does, so
 * a write to the GPU's DMA port is not seen until the run returns - which is far
 * too late for a program that triggers a transfer and then keeps going. The
 * interpreter reports writes instead. On a Pico the hardware does it and none of
 * this is compiled.
 */
#if !defined(PICO_BUILD)
#define TMS9900_WATCH_WRITES 1
#endif

  typedef struct Tms9900Cpu
  {
    uint8_t* mem;    /* Pointer to memory backing the CPU */
    uint8_t* regx38; /* Pointer to the GPU control byte (TMS register 0x38) */
    uint32_t pc;     /* Program counter (uint32_t to handle WP=0xFFFE overflow) */
    uint16_t wp;     /* Workspace pointer */
    uint16_t st;     /* Status register (flag layout matches assembly core) */
#if defined(TMS9900_WATCH_WRITES)
    /*
     * Called after a write to an address the running program chose, or null.
     *
     * LIMITATION: workspace-relative writes are not reported - register stores and
     * the context saves BLWP and XOP make. Those land at WP+n, and WP is >FFFE, so
     * they can only reach a watched address if a program moves its workspace onto
     * one with LWPI. Nothing does. Widening this means routing set_reg and the
     * context saves through the same watch, which costs the CPU core's hot path a
     * call per register write.
     */
    void (*onWrite)(uint8_t* mem, uint32_t addr);
#endif
  } Tms9900Cpu;

  /* Initialize a CPU context */
  void tms9900_init(Tms9900Cpu* cpu, uint8_t* mem, uint8_t* regx38, uint16_t pc, uint16_t wp);

  /* Execute until regx38 bit0 is cleared or an IDLE occurs. Returns final PC. */
  uint16_t run9900_c(Tms9900Cpu* cpu);

  /* The same, giving up after at most `budget` instructions - zero means no limit.
     Returns the PC either way, which is what makes it resumable: a host with one
     thread interleaves this with its renderer, and a program that waits on the
     raster gets a raster that moves.

     A budget that runs out and a program that parks on an IDLE or a self-jump both
     leave the run flag set, so the flag cannot tell them apart. `outOfBudget`, if
     given, does: only that one has work still to do.

     The PC is not the whole of what a resume needs. A budget expires BETWEEN
     instructions, which includes between a compare and the jump that reads what it
     set, so `cpu->st` has to come back too: build the next call's Tms9900Cpu from the
     one this returned rather than from tms9900_init, which starts the status at zero
     and would have that jump decide on flags nothing set. */
  uint16_t run9900_budget_c(Tms9900Cpu* cpu, uint32_t budget, bool* outOfBudget);

#ifdef __cplusplus
}
#endif
