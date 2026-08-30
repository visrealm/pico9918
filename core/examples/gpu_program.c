/*
 * pico9918-core - run a program on the F18A's GPU, beside a running raster
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
 * Except that "let it run" is where a host has a real decision to make, and it is
 * what this example is about.
 *
 * A GPU program is not a subroutine. It runs beside the display, and it can WAIT on
 * the display: the scanline being scanned out is readable at >7000, and a program
 * that pages a bitmap wants to do it in the vertical blank, so it polls that address
 * until the raster is somewhere safe. If nothing advances the raster while the
 * program runs, that poll never ends.
 *
 * There are two honest shapes for a host, and the library supports both:
 *
 *   A THREAD FOR THE GPU. What this file does, and what the firmware does - core 0
 *   runs pico9918_gpu_loop() while core 1 renders. The program executes beside the
 *   raster rather than instead of it, so the drawing appears a frame at a time. The
 *   thing to get right is the throttle: the frame loop has to be paced to the display
 *   rate, or the program's waits are answered faster than a display would answer them.
 *
 *   ONE THREAD, INTERLEAVED. pico9918_gpu_step_n() runs a bounded number of
 *   instructions and returns with the PC kept, so a host with no core to spare
 *   alternates slices of program with lines of raster. gpu_program.py is written that
 *   way, against the same two programs, and is worth reading beside this one.
 *
 * pico9918_gpu_step() - unbounded, on the calling thread - is the third option and is
 * the one that cannot service a wait: the caller that would advance the raster is the
 * one blocked inside it.
 *
 * The default program is **Tursi's** F18A GPU Mandelbrot, from
 * test/suite/data/gpu-programs/ where it is credited in full. 548 bytes: it sets
 * VR0-VR7 itself, builds its own name table, and draws 49,152 pixels in Graphics II
 * over x -2.0..+0.5, y +1.25..-1.25 with 14 iterations of Q13 fixed point before
 * halting on IDLE. Twenty-three million TMS9900 instructions. It is somebody else's
 * program, which is the point - nothing in it was written with this library in mind.
 * It never looks at the raster, so it runs the same under any of the three shapes.
 *
 * The other thing a host has to get right: the GPU is an F18A feature, so the library
 * must be built PICO9918_MODE=1 and the chip must be unlocked. A locked TMS9918A has
 * no GPU to run anything on.
 *
 * Build it against an installed package:
 *
 *     cmake -S examples -B build-examples -DPICO9918_MODE=1
 *     cmake --build build-examples
 *     ./build-examples/gpu_program mandel.ppm
 *
 * A program and the address it was assembled for can be given instead. cube.bin,
 * beside it, is a solid cube turning on the F18A's bitmap layer, and it is the one
 * that waits on the raster:
 *
 *     ./build-examples/gpu_program cube.ppm .../gpu-programs/cube.bin 0x3200
 *
 * Its colours come out wrong here, and that is the interesting part: the cube shades
 * itself by rewriting palette RAM, and this colours the frame from the boot palette
 * instead. test/suite/view.py --gpu reads the live one and shows it turning.
 */

#include "pico9918.h"
#include "pico9918_frame.h"
#include "pico9918_util.h"

#include "gpu/gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

/* Where Tursi's program starts. Even, because the GPU refuses an odd address. */
#define ENTRY 0x1B02

/* The GPU may occupy base VRAM below the F18A's GRAM window. Blanked before the
   program is loaded, so nothing already in VRAM can end up as part of the picture. */
#define PROGRAM_SPACE 0x4000

#define ROWS 192
#define COLS TMS9918_PIXELS_X

/* The host's own display, which is what the raster below is a raster OF. 640x480 at
   60Hz is the shipping VGA mode; any host's numbers go here instead. */
#define H_VIRTUAL_PIXELS 640
#define V_DISPLAY_LINES  480
#define FRAME_RATE_HZ    60.0f
#define CORE_TEMP_C      30.0f
#define FRAME_US         (1000000 / 60)
/* The share of a frame the raster spends outside the picture. A 480-line mode over
   525 total lines puts it near a sixth, and it is what a program waiting for the
   vertical blank has to catch. */
#define BLANK_US         (FRAME_US / 6)

/* Long enough that a program which is not waiting on anything we understand is
   reported rather than hung on. */
#define GIVE_UP_FRAMES 3600

/* The F18A registers this reaches. The public header names only R0-R7, the ones a
   TMS9918A has, so the enhanced ones are written by number. */
#define VR_UNLOCK   0x39 /* 0x1c twice unlocks the F18A */
#define VR_GPU_HI   0x36 /* GPU start address, high byte */
#define VR_GPU_LO   0x37 /* low byte - writing it arms the GPU at that address */

#if PICO9918_SINGLE_INSTANCE
#define INSTANCE
#else
static pico9918_t* tms9918;
#define INSTANCE tms9918
#endif

static void writeReg(PICO9918_INST_ARG uint8_t reg, uint8_t value)
{
  pico9918_write_register_value(PICO9918_INST(pico9918_register_t) reg, value);
}

static void writeVram(PICO9918_INST_ARG uint16_t addr, const uint8_t* data, size_t len)
{
  pico9918_set_address_write(PICO9918_INST addr);
  pico9918_write_bytes(PICO9918_INST data, len);
}

/* -------------------------------------------------------------------------
 * The GPU, on a thread of its own.
 *
 * Nothing is locked, and that is not an oversight - it is the shape of the machine.
 * On a board the two cores share VRAM with no lock either. Every access is a byte, so
 * a reader sees the old value or the new one, and a half-drawn frame is the truth
 * about a half-drawn picture.
 * ---------------------------------------------------------------------- */
static volatile int gpuBusy = 0;

#ifdef _WIN32
static DWORD WINAPI gpuBody(LPVOID unused)
{
  (void)unused;
  pico9918_gpu_step(INSTANCE);
  gpuBusy = 0;
  return 0;
}

typedef HANDLE thread_t;
static int threadStart(thread_t* t)
{
  *t = CreateThread(NULL, 0, gpuBody, NULL, 0, NULL);
  return *t != NULL;
}
static void threadJoin(thread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
static void sleepUs(unsigned us) { Sleep(us / 1000); }
static uint64_t nowUs(void)
{
  LARGE_INTEGER f, t;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&t);
  return (uint64_t)t.QuadPart * 1000000u / (uint64_t)f.QuadPart;
}
#else
static void* gpuBody(void* unused)
{
  (void)unused;
  pico9918_gpu_step(INSTANCE);
  gpuBusy = 0;
  return NULL;
}

typedef pthread_t thread_t;
static int threadStart(thread_t* t) { return pthread_create(t, NULL, gpuBody, NULL) == 0; }
static void threadJoin(thread_t t) { pthread_join(t, NULL); }
static void sleepUs(unsigned us)
{
  struct timespec ts = {(time_t)(us / 1000000), (long)(us % 1000000) * 1000};
  nanosleep(&ts, NULL);
}
static uint64_t nowUs(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000u + (uint64_t)(t.tv_nsec / 1000);
}
#endif

/* A deadline, not a duration. Sleeping a fixed amount at the bottom of a loop drifts
   by however long the work took, and keeping the display's time is the entire job of
   the throttle. The last stretch is spun rather than slept because no host's sleep is
   accurate to a scanline - ask for less than you need, then wait out the rest. */
static void waitUntil(uint64_t deadlineUs)
{
  for (;;)
  {
    const uint64_t now = nowUs();
    if (now >= deadlineUs) return;
    const uint64_t left = deadlineUs - now;
    if (left > 2000) sleepUs((unsigned)(left - 2000));
  }
}

static uint8_t vPixelScale      = 1;
static uint16_t vVirtualPixels  = V_DISPLAY_LINES;

/* One frame, in the order a host's video layer calls it: every visible line, the
   porch, then the end of frame. This is what moves the scanline register the program
   reads at >7000 - pico9918_scan_line() renders a line but does not publish one, so a
   host that only calls that has no raster as far as a GPU program is concerned.

   The frame's time is spent in two places, and that split is the point. Rendering 480
   lines takes a fraction of a frame, so a loop that renders then sleeps leaves the
   raster parked whereever it stopped for the rest of the frame - and it stops in the
   porch, at >FF, which is exactly the value a program waiting for the vertical blank
   is looking for. Answered instantly, every time, the throttle does nothing. Waiting
   out most of the frame BEFORE the porch leaves the raster in the picture where it
   belongs, and the remainder after it is a blank wide enough for a program to catch.

   The geometry a frame runs under is the PREVIOUS frame's: pico9918_frame_end is the
   only thing that recomputes it, on a device too. */
static void renderFrame(PICO9918_INST_ARG uint64_t startUs)
{
  static PICO9918_PIXEL_T pixels[H_VIRTUAL_PIXELS + 16];

  pico9918_scanline_params_t params = {H_VIRTUAL_PIXELS, vVirtualPixels, false, 0};
  for (uint16_t y = 0; y < vVirtualPixels; ++y)
    pico9918_frame_scanline(PICO9918_INST y, &params, pixels);

  waitUntil(startUs + FRAME_US - BLANK_US);
  pico9918_frame_porch(PICO9918_INST_ONLY);

  pico9918_frame_display_t display = {V_DISPLAY_LINES, false, vPixelScale, vVirtualPixels};
  pico9918_frame_end(PICO9918_INST CORE_TEMP_C, FRAME_RATE_HZ, &display);
  vPixelScale    = display.vPixelScale;
  vVirtualPixels = display.vVirtualPixels;
  waitUntil(startUs + FRAME_US);
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
  const char* out      = (argc > 1) ? argv[1] : "gpu.ppm";
  const char* bin      = (argc > 2) ? argv[2] : MANDEL_BIN;
  const uint16_t entry = (argc > 3) ? (uint16_t)strtoul(argv[3], NULL, 0) : ENTRY;

  if ((entry & 1) || entry >= PROGRAM_SPACE)
  {
    fprintf(stderr, "%#06x cannot be a start address: the GPU refuses an odd one, and "
                    "a program lives below %#06x\n", entry, PROGRAM_SPACE);
    return 1;
  }

  static uint8_t program[PROGRAM_SPACE];
  FILE* f = fopen(bin, "rb");
  if (!f)
  {
    fprintf(stderr, "cannot open %s\n", bin);
    return 1;
  }
  const size_t len = fread(program + entry, 1, sizeof program - entry, f);
  fclose(f);
  if (!len)
  {
    fprintf(stderr, "%s is empty\n", bin);
    return 1;
  }

#if PICO9918_SINGLE_INSTANCE
  pico9918_init();
#else
  tms9918 = pico9918_new();
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

  /* One frame before the program starts, so the raster it reads has a value in it
     rather than whatever a reset left. */
  renderFrame(PICO9918_INST nowUs());

  /* Arm and run. Writing the low byte is what latches the address and starts it, so
     the high byte goes first. */
  writeReg(PICO9918_INST VR_GPU_HI, (uint8_t)(entry >> 8));
  writeReg(PICO9918_INST VR_GPU_LO, (uint8_t)(entry & 0xff));

  pico9918_gpu_reset_time();
  gpuBusy = 1;

  thread_t gpu;
  if (!threadStart(&gpu))
  {
    fprintf(stderr, "no thread to run the GPU on\n");
    return 1;
  }

  /* The frame loop, paced to the display. Pacing is the price of the thread: without
     it a program that waits a frame is answered in microseconds, and anything it
     times against the raster runs at whatever speed this host happens to have. */
  unsigned frames  = 0;
  uint64_t nextUs  = nowUs();
  while (gpuBusy && frames < GIVE_UP_FRAMES)
  {
    renderFrame(PICO9918_INST nextUs);
    nextUs += FRAME_US;
    ++frames;
  }
  if (gpuBusy)
  {
    fprintf(stderr, "%s did not finish in %u frames\n", bin, frames);
    return 1;
  }
  threadJoin(gpu);
  const uint32_t us = pico9918_gpu_time(0);

  /* Wall clock on whatever is emulating it, not the TMS9900's own time - the same
     accumulator the firmware reports the GPU's share of a frame from. */
  printf("%s: %u bytes at 0x%04X, %u us of host time over %u frames\n", bin,
         (unsigned)len, entry, (unsigned)us, frames);

  /* What it drew. The GPU wrote VRAM and the registers; rendering is unchanged from
     any other frame. Taken a line at a time rather than through the frame loop above
     because what a PPM wants is palette INDICES, upstream of any host's pixel format. */
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
