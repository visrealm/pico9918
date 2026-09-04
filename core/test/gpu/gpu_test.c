/**
 * \file
 * \brief pico9918-core - the library-paced GPU
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * pico9918_gpu_set_clock's contract, which nothing else covers: a rate makes the library
 * run an armed program from inside the arming write, and zero leaves the GPU to the host.
 *
 * The first half is what software detecting an F18A depends on. The probe arms a tiny
 * self-modifying program and reads its result back within a handful of host cycles, so a
 * host servicing the GPU once a scanline sees the chip only when a scanline boundary
 * happens to fall in between - a real flaky-detection bug in two emulators.
 */

#include "impl/pico9918_priv.h"
#include "gpu/gpu.h"

#include <stdio.h>

#define PROGRAM_AT 0x2000u
#define RESULT_AT  0x2100u
#define MARKER     0xbeefu

static int failures;

static void fail(const char* what, unsigned wanted, unsigned got)
{
  ++failures;
  printf("  FAIL %s: want %04x got %04x\n", what, wanted, got);
}

static void regWrite(uint8_t reg, uint8_t value)
{
  pico9918_write_reg_value_impl(PICO9918_INST 0x80 | reg, value);
}

/* Two writes of 0x1c to R57, which is what an F18A answers to. */
static void unlock(void)
{
  regWrite(0x39, 0x1c);
  regWrite(0x39, 0x1c);
}

/*
 *   LI   R0, >BEEF     0200 BEEF
 *   MOV  R0, @>2100    C800 2100
 *   IDLE               0340
 *
 * Written straight into VRAM rather than through the host bus, which masks to 16K.
 */
static void loadProgram(void)
{
  static const uint8_t program[] = {0x02, 0x00, 0xbe, 0xef, 0xc8, 0x00, 0x21, 0x00, 0x03, 0x40};

  for (unsigned i = 0; i < sizeof(program); ++i) tms9918->vram.bytes[PROGRAM_AT + i] = program[i];

  tms9918->vram.bytes[RESULT_AT]     = 0;
  tms9918->vram.bytes[RESULT_AT + 1] = 0;
}

static uint16_t result(void)
{
  return (uint16_t)((tms9918->vram.bytes[RESULT_AT] << 8) | tms9918->vram.bytes[RESULT_AT + 1]);
}

/* Arm at PROGRAM_AT. Writing R55 is what arms it, so R54 goes first. */
static void arm(void)
{
  regWrite(0x36, (uint8_t)(PROGRAM_AT >> 8));
  regWrite(0x37, (uint8_t)(PROGRAM_AT & 0xff));
}

int main(void)
{
  pico9918_init();
  pico9918_gpu_init(PICO9918_INST_ONLY);

  /* 1. no rate is the default, and leaves the GPU to whoever else drives it. The
        program is armed, so a host's own step_n would still run it - but the arming
        write must not. */
  unlock();
  loadProgram();
  arm();
  if (result() != 0) fail("no-rate-ran", 0, result());

  /* 2. and it is only armed, not lost: the host-driven path still works. */
  pico9918_gpu_step_n(PICO9918_INST 1000);
  if (result() != MARKER) fail("host-driven", MARKER, result());

  /* 3. a rate runs it from inside the arming write, which is the whole point */
  pico9918_gpu_set_clock(PICO9918_INST PICO9918_GPU_IPS_PRO);
  loadProgram();
  arm();
  if (result() != MARKER) fail("armed-not-run", MARKER, result());

  /* 4. the other arming route: R56 bit 0. It resumes from gpuAddress rather than
        restarting, so R55 sets the address with no rate on - arming without running -
        and the R56 write is what starts it. */
  pico9918_gpu_set_clock(PICO9918_INST 0);
  loadProgram();
  arm();
  if (result() != 0) fail("r56-early-run", 0, result());

  pico9918_gpu_set_clock(PICO9918_INST PICO9918_GPU_IPS_PRO);
  regWrite(0x38, 1);
  if (result() != MARKER) fail("r56-not-run", MARKER, result());

  /* 5. a locked device has no GPU to arm - a reset re-locks it. The registers that
        would arm one are above the eight it admits, so the write is ignored outright,
        and the rate is still set so this is the lock doing it. */
  pico9918_reset(PICO9918_INST_ONLY);
  pico9918_gpu_init(PICO9918_INST_ONLY);
  pico9918_gpu_set_clock(PICO9918_INST PICO9918_GPU_IPS_PRO);
  loadProgram();
  arm();
  if (result() != 0) fail("locked-ran", 0, result());

  /* 6. and unlocking again brings it back, so nothing above latched */
  unlock();
  loadProgram();
  arm();
  if (result() != MARKER) fail("relocked", MARKER, result());

  /* 7. back to zero, back to the host */
  pico9918_gpu_set_clock(PICO9918_INST 0);
  loadProgram();
  arm();
  if (result() != 0) fail("cleared-rate-ran", 0, result());

  printf("%s: library-paced GPU, %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures != 0;
}
