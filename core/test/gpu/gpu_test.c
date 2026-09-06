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
 *
 * The last section is the DMA engine, which is here because a program triggering one is
 * the only way to reach it: the ports it answers to are above the address space a host
 * can write.
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

/*
 *   LI   R1, >8008     0201 8008     the DMA trigger port
 *   LI   R2, >0100     0202 0100
 *   MOV  R2, *R1       C442          any write to >8008 starts the transfer
 *   IDLE               0340
 *
 * The registers below it are set by the case rather than by the program: what is under
 * test is the engine, not a program's ability to load eight bytes.
 */
static void loadDmaProgram(void)
{
  static const uint8_t program[] = {0x02, 0x01, 0x80, 0x08, 0x02, 0x02,
                                    0x01, 0x00, 0xc4, 0x42, 0x03, 0x40};

  for (unsigned i = 0; i < sizeof(program); ++i) tms9918->vram.bytes[PROGRAM_AT + i] = program[i];
}

#define DMA_SRC 0x1000u
#define DMA_DST 0x1800u

/* One transfer, from a source that is 0x40 counting up. Returns with the destination
   wherever the engine left it. */
static void dma(uint32_t src, uint32_t dst, uint8_t width, uint8_t height, uint8_t stride,
                uint8_t params)
{
  for (unsigned i = 0; i < 0x400; ++i)
  {
    tms9918->vram.bytes[DMA_SRC + i] = (uint8_t)(0x40 + i);
    tms9918->vram.bytes[DMA_DST + i] = 0;
  }

  tms9918->vram.bytes[0x8000] = (uint8_t)(src >> 8);
  tms9918->vram.bytes[0x8001] = (uint8_t)src;
  tms9918->vram.bytes[0x8002] = (uint8_t)(dst >> 8);
  tms9918->vram.bytes[0x8003] = (uint8_t)dst;
  tms9918->vram.bytes[0x8004] = width;
  tms9918->vram.bytes[0x8005] = height;
  tms9918->vram.bytes[0x8006] = stride;
  tms9918->vram.bytes[0x8007] = params;

  loadDmaProgram();
  arm();
}

static void expect(const char* what, uint32_t at, uint8_t wanted)
{
  if (tms9918->vram.bytes[at] != wanted) fail(what, wanted, tms9918->vram.bytes[at]);
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

  /* and armed is where pico9918_gpu_pc says it is, before a single instruction runs */
  if (pico9918_gpu_pc(PICO9918_INST_ONLY) != PROGRAM_AT)
    fail("armed-pc", PROGRAM_AT, pico9918_gpu_pc(PICO9918_INST_ONLY));

  /* and a disassembler can reach the program: the guest's own view stops at 0x3fff, so
     PROGRAM_AT is in range for both, but only the GPU's reaches past it. */
  if (pico9918_gpu_mem_value(PICO9918_INST PROGRAM_AT) != 0x02)
    fail("gpu-mem-program", 0x02, pico9918_gpu_mem_value(PICO9918_INST PROGRAM_AT));

  tms9918->vram.bytes[0x4100] = 0x5a;
  if (pico9918_gpu_mem_value(PICO9918_INST 0x4100) != 0x5a)
    fail("gpu-mem-gram", 0x5a, pico9918_gpu_mem_value(PICO9918_INST 0x4100));
  if (pico9918_vram_value(PICO9918_INST 0x4100) == 0x5a) fail("vram-value-reached-gram", 0, 0x5a);

  /* and it stops where the map does rather than walking into the instance */
  if (pico9918_gpu_mem_value(PICO9918_INST pico9918_gpu_mem_size()) != 0)
    fail("gpu-mem-past-end", 0, pico9918_gpu_mem_value(PICO9918_INST pico9918_gpu_mem_size()));

  /* 2. and it is only armed, not lost: the host-driven path still works. */
  pico9918_gpu_step_n(PICO9918_INST 1000);
  if (result() != MARKER) fail("host-driven", MARKER, result());

  /* the program left >BEEF in R0, which is the last word of the map, and R1 is above it
     in the workspace overflow - so reading either through the 16-bit map cannot work */
  if (pico9918_gpu_reg_value(PICO9918_INST 0) != MARKER)
    fail("gpu-r0", MARKER, pico9918_gpu_reg_value(PICO9918_INST 0));

  tms9918->vram.map.wrksp[0] = 0x12;
  tms9918->vram.map.wrksp[1] = 0x34;
  if (pico9918_gpu_reg_value(PICO9918_INST 1) != 0x1234)
    fail("gpu-r1", 0x1234, pico9918_gpu_reg_value(PICO9918_INST 1));

  /* IDLE leaves the status the compare before it set, so it is not simply zero */
  if (pico9918_gpu_status(PICO9918_INST_ONLY) == 0)
    fail("gpu-status-zero", 1, pico9918_gpu_status(PICO9918_INST_ONLY));

  /* and it moved: a run leaves the point it reached, not the address it was armed at */
  if (pico9918_gpu_pc(PICO9918_INST_ONLY) == PROGRAM_AT)
    fail("pc-did-not-move", 0, pico9918_gpu_pc(PICO9918_INST_ONLY));

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

  /* the reset parks an odd address, which is what the header calls "nothing armed" */
  if ((pico9918_gpu_pc(PICO9918_INST_ONLY) & 1) == 0)
    fail("reset-pc-even", 1, pico9918_gpu_pc(PICO9918_INST_ONLY));

  pico9918_gpu_init(PICO9918_INST_ONLY);
  pico9918_gpu_set_clock(PICO9918_INST PICO9918_GPU_IPS_PRO);
  loadProgram();
  arm();
  if (result() != 0) fail("locked-ran", 0, result());

  /* and the locked arm did not move it: the registers that would are above the eight */
  if ((pico9918_gpu_pc(PICO9918_INST_ONLY) & 1) == 0)
    fail("locked-armed-pc", 1, pico9918_gpu_pc(PICO9918_INST_ONLY));

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

  /* 8. the DMA engine's geometry. The source is 0x40 counting up, so where a byte landed
        says which one it was and therefore which row and column the engine thought it
        was on. Zero width and height mean 256; stride is a different animal entirely. */
  unlock();
  pico9918_gpu_set_clock(PICO9918_INST PICO9918_GPU_IPS_PRO);

  dma(DMA_SRC, DMA_DST, 4, 3, 4, 0x00);
  expect("dma-run-first", DMA_DST, 0x40);
  expect("dma-run-last", DMA_DST + 11, 0x4b);
  expect("dma-run-past", DMA_DST + 12, 0x00);

  dma(DMA_SRC, DMA_DST, 4, 3, 16, 0x00);
  expect("dma-stride-row0", DMA_DST, 0x40);
  expect("dma-stride-gap", DMA_DST + 4, 0x00);
  expect("dma-stride-row1", DMA_DST + 16, 0x50);
  expect("dma-stride-row2", DMA_DST + 32, 0x60);

  /* stride zero is a pitch of zero, not of 256: every row lands on the one before it */
  dma(DMA_SRC, DMA_DST, 4, 3, 0, 0x00);
  expect("dma-stride0-row0", DMA_DST, 0x40);
  expect("dma-stride0-end", DMA_DST + 4, 0x00);
  expect("dma-stride0-not256", DMA_DST + 256, 0x00);

  /* and a stride the eight-bit difference overflows walks backwards: 200 with a width of
     8 is a pitch of -56, not +200 */
  dma(0x1100, 0x1900, 8, 2, 200, 0x00);
  expect("dma-back-row0", 0x1900, 0x40);
  expect("dma-back-row1", 0x18c8, 0x08);
  expect("dma-back-not-forward", 0x19c8, 0x00);

  /* a width of zero is 256, and with stride zero the difference wraps to a pitch of 256 */
  dma(DMA_SRC, DMA_DST, 0, 1, 0, 0x00);
  expect("dma-width256-first", DMA_DST, 0x40);
  expect("dma-width256-last", DMA_DST + 255, 0x3f);
  expect("dma-width256-past", DMA_DST + 256, 0x00);

  /* a fill reads its byte once and strides like a copy */
  dma(DMA_SRC, DMA_DST, 4, 3, 16, 0x01);
  expect("dma-fill-row0", DMA_DST + 3, 0x40);
  expect("dma-fill-gap", DMA_DST + 4, 0x00);
  expect("dma-fill-row2", DMA_DST + 32, 0x40);

  /* decrementing runs both ends backwards, so a row ends below the address it started at */
  dma(0x1100, 0x1900, 4, 2, 4, 0x02);
  expect("dma-dec-row0-first", 0x1900, 0x40);
  expect("dma-dec-row0-last", 0x18fd, 0x3d);
  expect("dma-dec-row1-first", 0x18fc, 0x3c);
  expect("dma-dec-row1-last", 0x18f9, 0x39);
  expect("dma-dec-past", 0x1901, 0x00);

  printf("%s: library-paced GPU, %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures != 0;
}
