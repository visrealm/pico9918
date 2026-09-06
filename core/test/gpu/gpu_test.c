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
 *   STST R3            02C3        so the status accessor has something to agree with
 *   IDLE               0340
 *
 * Written straight into VRAM rather than through the host bus, which masks to 16K.
 */
static void loadProgram(void)
{
  static const uint8_t program[] = {0x02, 0x00, 0xbe, 0xef, 0xc8, 0x00,
                                    0x21, 0x00, 0x02, 0xc3, 0x03, 0x40};

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

  /* the accessor publishes what STST stored, bit for bit - a non-zero check cannot see
     the two disagreeing by the eight places the low-byte storage sits at */
  if (pico9918_gpu_status(PICO9918_INST_ONLY) != pico9918_gpu_reg_value(PICO9918_INST 3))
    fail("gpu-status-stst", pico9918_gpu_reg_value(PICO9918_INST 3),
         pico9918_gpu_status(PICO9918_INST_ONLY));

  /* and it is what >BEEF earns from a cleared status: logical greater than alone, since
     arming zeroed it, MOV keeps only C/OV/P across, and >BEEF is negative and non-zero */
  if (pico9918_gpu_status(PICO9918_INST_ONLY) != PICO9918_GPU_ST_LGT)
    fail("gpu-status-flags", PICO9918_GPU_ST_LGT, pico9918_gpu_status(PICO9918_INST_ONLY));

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

  /* either side of where it overflows, which for a width of 8 is a stride of 135 */
  dma(0x1100, 0x1900, 8, 2, 134, 0x00);
  expect("dma-edge-fwd-row1", 0x1986, 0xc6);
  expect("dma-edge-fwd-gap", 0x1985, 0x00);

  dma(0x1100, 0x1900, 8, 2, 135, 0x00);
  expect("dma-edge-back-row1", 0x1887, 0xc7);
  expect("dma-edge-back-not-forward", 0x1987, 0x00);

  /* a width of zero is 256, and with stride zero the difference wraps to a pitch of 256 */
  dma(DMA_SRC, DMA_DST, 0, 1, 0, 0x00);
  expect("dma-width256-first", DMA_DST, 0x40);
  expect("dma-width256-last", DMA_DST + 255, 0x3f);
  expect("dma-width256-past", DMA_DST + 256, 0x00);

  /* and a height of zero is 256 rows of it */
  dma(DMA_SRC, DMA_DST, 1, 0, 1, 0x00);
  expect("dma-height256-first", DMA_DST, 0x40);
  expect("dma-height256-last", DMA_DST + 255, 0x3f);
  expect("dma-height256-past", DMA_DST + 256, 0x00);

  /* the top of each register: 255 wide by one, then one wide by 255 */
  dma(DMA_SRC, DMA_DST, 255, 1, 255, 0x00);
  expect("dma-width255-first", DMA_DST, 0x40);
  expect("dma-width255-last", DMA_DST + 254, 0x3e);
  expect("dma-width255-past", DMA_DST + 255, 0x00);

  dma(DMA_SRC, DMA_DST, 1, 255, 1, 0x00);
  expect("dma-height255-first", DMA_DST, 0x40);
  expect("dma-height255-last", DMA_DST + 254, 0x3e);
  expect("dma-height255-past", DMA_DST + 255, 0x00);

  /* a stride under the width steps back into the row just written */
  dma(DMA_SRC, DMA_DST, 8, 2, 4, 0x00);
  expect("dma-narrow-kept", DMA_DST + 3, 0x43);
  expect("dma-narrow-rewritten", DMA_DST + 4, 0x44);
  expect("dma-narrow-last", DMA_DST + 11, 0x4b);
  expect("dma-narrow-past", DMA_DST + 12, 0x00);

  /* only two bits of the parameter byte are decoded, so the other six say nothing */
  dma(DMA_SRC, DMA_DST, 4, 3, 4, 0xfc);
  expect("dma-params-spare-first", DMA_DST, 0x40);
  expect("dma-params-spare-last", DMA_DST + 11, 0x4b);
  expect("dma-params-spare-past", DMA_DST + 12, 0x00);

  /* both parameter bits at once: a fill that decrements */
  dma(0x1100, 0x1900, 4, 2, 4, 0x03);
  expect("dma-fill-dec-first", 0x1900, 0x40);
  expect("dma-fill-dec-row0", 0x18fd, 0x40);
  expect("dma-fill-dec-row1", 0x18f9, 0x40);
  expect("dma-fill-dec-past", 0x18f8, 0x00);

  /* LOAD-BEARING: overlapping forwards, so each byte read has already been written. A
     block copy would answer 40..47 here, which is what makes this the guard on the fast
     path being taken only where source and destination are disjoint. */
  dma(DMA_SRC, DMA_SRC + 2, 8, 1, 8, 0x00);
  expect("dma-overlap-0", DMA_SRC + 2, 0x40);
  expect("dma-overlap-2", DMA_SRC + 4, 0x40);
  expect("dma-overlap-7", DMA_SRC + 9, 0x41);

  /* sixteen bits of address, either end */
  dma(DMA_SRC, 0xfffe, 4, 1, 4, 0x00);
  expect("dma-dst-wrap-before", 0xffff, 0x41);
  expect("dma-dst-wrap-after", 0x0000, 0x42);
  expect("dma-dst-wrap-last", 0x0001, 0x43);

  tms9918->vram.bytes[0xfffe] = 0x11;
  tms9918->vram.bytes[0xffff] = 0x22;
  tms9918->vram.bytes[0x0000] = 0x33;
  tms9918->vram.bytes[0x0001] = 0x44;
  dma(0xfffe, DMA_DST, 4, 1, 4, 0x00);
  expect("dma-src-wrap-before", DMA_DST + 1, 0x22);
  expect("dma-src-wrap-after", DMA_DST + 2, 0x33);
  expect("dma-src-wrap-last", DMA_DST + 3, 0x44);

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

  /* decrementing with a stride of zero cancels the row's own backwards run exactly */
  dma(0x1100, 0x1900, 4, 3, 0, 0x02);
  expect("dma-dec-stride0-first", 0x1900, 0x40);
  expect("dma-dec-stride0-last", 0x18fd, 0x3d);
  expect("dma-dec-stride0-end", 0x18fc, 0x00);
  expect("dma-dec-stride0-past", 0x1901, 0x00);

  /* TRAP: the difference is (width - 1) - stride here, so an ordinary stride always makes
     the pitch negative and only one that underflows walks the rows FORWARDS while each row
     is still written backwards. 200 with a width of 8 gives (7 - 200) & 0xff = 63, so +56 -
     the mirror of dma-back-row1 above, which is the same numbers incrementing. */
  dma(0x1100, 0x1900, 8, 2, 200, 0x02);
  expect("dma-dec-fwd-row0-first", 0x1900, 0x40);
  expect("dma-dec-fwd-row0-last", 0x18f9, 0x39);
  expect("dma-dec-fwd-row1-first", 0x1938, 0x78);
  expect("dma-dec-fwd-row1-last", 0x1931, 0x71);
  expect("dma-dec-fwd-gap", 0x1901, 0x00);
  expect("dma-dec-fwd-not-back", 0x18c8, 0x00);

  /* and its own edge, which is NOT where incrementing turns over: two's complement holds
     one more negative than positive, so dec flips at 136 where inc flipped at 135 */
  dma(0x1100, 0x1900, 8, 2, 135, 0x02);
  expect("dma-dec-edge-back-row1", 0x1879, 0xb9);
  expect("dma-dec-edge-back-not-forward", 0x1979, 0x00);

  dma(0x1100, 0x1900, 8, 2, 136, 0x02);
  expect("dma-dec-edge-fwd-row1", 0x1978, 0xb8);
  expect("dma-dec-edge-fwd-not-back", 0x1878, 0x00);

  printf("%s: library-paced GPU, %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures != 0;
}
