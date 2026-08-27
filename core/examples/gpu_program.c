/*
 * pico9918-core - run a program on the F18A's GPU
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * \example gpu_program.c
 *
 * The F18A has a TMS9900 on it. A program sitting in VRAM runs on that core, reaches
 * the VDP register file through the GPU's >6000 window, and draws by writing VRAM -
 * so the host's whole job is to load it, point the GPU at it, and let it run.
 *
 * The program here is **Tursi's** F18A GPU Mandelbrot, from
 * test/suite/data/gpu-programs/ where it is credited in full. 548 bytes: it sets
 * VR0-VR7 itself, builds its own name table, and draws 49,152 pixels in Graphics II
 * over x -2.0..+0.5, y +1.25..-1.25 with 14 iterations of Q13 fixed point before
 * halting on IDLE. Twenty-three million TMS9900 instructions. It is somebody else's
 * program, which is the point - nothing in it was written with this library in mind.
 *
 * Two things a host has to get right, and they are the reason this example exists:
 *
 *   The GPU is an F18A feature, so the library must be built PICO9918_MODE=1 and the
 *   chip must be unlocked. A locked TMS9918A has no GPU to run anything on.
 *
 *   pico9918_gpu_step() is the single-threaded entry: it runs the pending program to
 *   completion and returns, which is what an emulator wants. A host with a core to
 *   spare calls pico9918_gpu_loop() on that core instead - what the firmware does,
 *   so that the program executes beside the renderer rather than instead of it.
 *
 * Build it against an installed package:
 *
 *     cmake -S examples -B build-examples -DPICO9918_MODE=1
 *     cmake --build build-examples
 *     ./build-examples/gpu_program mandel.ppm
 */

#include "pico9918.h"
#include "pico9918_util.h"

#include "gpu/gpu.h"

#include <stdio.h>
#include <string.h>

/* Where the program starts. Even, because the GPU refuses an odd address. */
#define ENTRY 0x1B02

/* The GPU may occupy base VRAM below the F18A's GRAM window. Blanked before the
   program is loaded, so nothing already in VRAM can end up as part of the picture. */
#define PROGRAM_SPACE 0x4000

#define ROWS 192
#define COLS TMS9918_PIXELS_X

/* The F18A registers this reaches. The public header names only R0-R7, the ones a
   TMS9918A has, so the enhanced ones are written by number. */
#define VR_UNLOCK   0x39 /* 0x1c twice unlocks the F18A */
#define VR_GPU_HI   0x36 /* GPU start address, high byte */
#define VR_GPU_LO   0x37 /* low byte - writing it arms the GPU at that address */

static void writeReg(PICO9918_INST_ARG uint8_t reg, uint8_t value)
{
  pico9918_write_register_value(PICO9918_INST(pico9918_register_t) reg, value);
}

static void writeVram(PICO9918_INST_ARG uint16_t addr, const uint8_t* data, size_t len)
{
  pico9918_set_address_write(PICO9918_INST addr);
  pico9918_write_bytes(PICO9918_INST data, len);
}

static int writePpm(const char* path, const uint8_t* rgb)
{
  FILE* f = fopen(path, "wb");
  if (!f) return 0;
  fprintf(f, "P6\n%d %d\n255\n", COLS, ROWS);
  fwrite(rgb, 1, (size_t)COLS * ROWS * 3, f);
  fclose(f);
  return 1;
}

int main(int argc, char** argv)
{
  const char* out = (argc > 1) ? argv[1] : "gpu.ppm";
  const char* bin = (argc > 2) ? argv[2] : MANDEL_BIN;

  static uint8_t program[PROGRAM_SPACE];
  FILE* f = fopen(bin, "rb");
  if (!f)
  {
    fprintf(stderr, "cannot open %s\n", bin);
    return 1;
  }
  const size_t len = fread(program + ENTRY, 1, sizeof program - ENTRY, f);
  fclose(f);
  if (!len)
  {
    fprintf(stderr, "%s is empty\n", bin);
    return 1;
  }

#if PICO9918_SINGLE_INSTANCE
  pico9918_init();
#else
  pico9918_t* tms9918 = pico9918_new();
  if (!tms9918) return 1;
#endif
  pico9918_reset(PICO9918_INST_ONLY);
  pico9918_gpu_init(PICO9918_INST_ONLY);

  /* Unlocked: two writes of 0x1c to VR57, which is the F18A's own sequence and the
     only register write that is honoured while locked. */
  writeReg(PICO9918_INST VR_UNLOCK, 0x1c);
  writeReg(PICO9918_INST VR_UNLOCK, 0x1c);

  /* The register file is left as pico9918_reset() made it. Do not blank it from here:
     VR48 is the address auto-increment the data port uses, and zeroing it would make
     every byte of the load below land on the same address. */
  writeVram(PICO9918_INST 0, program, sizeof program);

  /* Arm and run. Writing the low byte is what latches the address and starts it, so
     the high byte goes first. */
  writeReg(PICO9918_INST VR_GPU_HI, (uint8_t)(ENTRY >> 8));
  writeReg(PICO9918_INST VR_GPU_LO, (uint8_t)(ENTRY & 0xff));

  pico9918_gpu_reset_time();
  pico9918_gpu_step(PICO9918_INST_ONLY);
  const uint32_t us = pico9918_gpu_time(0);

  printf("%s: %u bytes at 0x%04X, ran for %u us\n", bin, (unsigned)len, ENTRY,
         (unsigned)us);

  /* What it drew. The GPU wrote VRAM and the registers; rendering is unchanged from
     any other frame. */
  static uint8_t rgb[(size_t)COLS * ROWS * 3];
  for (uint16_t y = 0; y < ROWS; ++y)
  {
    pico9918_scan_line(PICO9918_INST y);

    const uint8_t* line = pico9918_line_source(PICO9918_INST_ONLY);
    for (uint16_t x = 0; x < COLS; ++x)
    {
      const uint32_t argb = pico9918_default_palette(line[x] & 0x0f);
      uint8_t* px         = rgb + ((size_t)y * COLS + x) * 3;
      px[0]               = (uint8_t)(((argb >> 8) & 0xf) * 17);
      px[1]               = (uint8_t)(((argb >> 4) & 0xf) * 17);
      px[2]               = (uint8_t)((argb & 0xf) * 17);
    }
  }

  if (!writePpm(out, rgb))
  {
    fprintf(stderr, "cannot write %s\n", out);
    return 1;
  }
  printf("wrote %s (%dx%d)\n", out, COLS, ROWS);

#if !PICO9918_SINGLE_INSTANCE
  pico9918_destroy(tms9918);
#endif
  return 0;
}
