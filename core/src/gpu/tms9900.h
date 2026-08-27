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

  typedef struct Tms9900Cpu
  {
    uint8_t* mem;    /* Pointer to memory backing the CPU */
    uint8_t* regx38; /* Pointer to the GPU control byte (TMS register 0x38) */
    uint32_t pc;     /* Program counter (uint32_t to handle WP=0xFFFE overflow) */
    uint16_t wp;     /* Workspace pointer */
    uint16_t st;     /* Status register (flag layout matches assembly core) */
  } Tms9900Cpu;

  /* Initialize a CPU context */
  void tms9900_init(Tms9900Cpu* cpu, uint8_t* mem, uint8_t* regx38, uint16_t pc, uint16_t wp);

  /* Execute until regx38 bit0 is cleared or an IDLE occurs. Returns final PC. */
  uint16_t run9900_c(Tms9900Cpu* cpu);

#ifdef __cplusplus
}
#endif
