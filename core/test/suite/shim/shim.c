/**
 * \file
 * \brief the live harness's desktop backend
 *
 * Project: pico9918
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 * `test/live/` drives a board over SWD: it writes the scene into the instance's
 * memory, waits, and reads back the rows the renderer produced. Every one of those
 * three is memory access plus a render, and none of them is device-specific - so
 * this speaks the same three operations over a pipe against an in-process library.
 *
 * What that buys is the correctness half of the suite without a board: 111 scenes
 * and their frozen references, on a desktop and in CI. What it cannot buy is the
 * other half. The device measures microseconds and which lines did not fit, and
 * neither has any meaning here. There is no timing hook in this build at all - see
 * liveDesktopOps.h - so the numbers cannot be produced by accident.
 *
 * THE ROWS ARE THE SAME ARTIFACT, and structurally rather than by agreement: the
 * capture arrives through PICO9918_LINE_CAPTURE, the hook the firmware's own harness
 * uses, at the same call site inside pico9918_frame_scanline, from the same pointer.
 * This file renders through the frame path the firmware renders through; it does
 * not reimplement it.
 *
 * Protocol: one command a line on stdin, a reply a line on stdout, and payloads as
 * raw bytes rather than hex - a full VRAM write is 16 KB and a capture 240 KB.
 *
 * \verbatim
 *   off                    field offsets, one "name value" a line, then "end"
 *   palette                the library's default palette, 64 little-endian uint16
 *   w <hexaddr> <len>      write <len> raw bytes that follow, instance-relative
 *   r <hexaddr> <len>      read: "data <len>" then <len> raw bytes
 *   frames <n>             render n frames, discarding them
 *   capture                render one frame: "capture <rows> <width>" then the rows
 *   gpu <hexaddr>          start a GPU program at <hexaddr> on its own thread, with
 *                          the raster running under it for as long as it does
 *   gpupoll                "gpu running", or "gpu done <microseconds>"
 *   gpustop                clear the GPU's run flag, which stops it where it is
 *   quit
 * \endverbatim
 *
 * Addresses are instance-relative, so the harness needs no notion of where the
 * instance lives - which is also what keeps the two backends' address arithmetic
 * identical.
 */

#include "impl/pico9918_priv.h"

#include "gpu/gpu.h"
#include "pico9918.h"
#include "pico9918_frame.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

/* The host's VGA geometry, and it has to be the host's: vBorder is derived from it,
   and vBorder is what decides which display row a capture row index means. 640x480
   at hPixelScale 1 is what the shipping VGA mode gives; the vertical pair is
   in/out and the library rewrites it on a progressive build, exactly as it does for
   the firmware in renderer.c. */
#define H_VIRTUAL_PIXELS 640
#define V_DISPLAY_LINES  480
#define FRAME_RATE_HZ    60.0f
#define CORE_TEMP_C      30.0f

/* Frames of grace the library gives an un-driven display before the splash gives
   way to the diagnostics screen. A device has been running for thousands of frames
   by the time a harness attaches, so a capture from frame 0 would be a capture of
   the splash. Started past it deliberately rather than by rendering 600 frames of
   artwork nobody reads. */
#define FRAMES_AT_START 1000

/* the widest line any tier renders, times the tallest frame any mode asks for */
#define CAPTURE_BYTES (512 * 512)

/* 640 pixels, plus room for the guard words the border fill writes past the picture.
   PICO9918_PIXEL_T is 32-bit on desktop and 16-bit on the device, so the fill's word
   count covers half the line here - which is why nothing in this file reads pixels.
   The captured rows are palette INDICES, upstream of every pixel-path difference
   between the two platforms, including the two known desktop pixel defects. */
static PICO9918_PIXEL_T pixels[1024];

static uint8_t capture[CAPTURE_BYTES];
static uint32_t rows   = 0;
static uint32_t width  = 0;
static uint32_t missed = 0;

static int vPixelScale        = 1;
static int vVirtualPixels     = V_DISPLAY_LINES;
static uint32_t triggerLine   = V_DISPLAY_LINES;
/* the vertical scale the frame in the buffer ran under, which is not necessarily the
   one the next frame will - see buildView */
static int viewScale          = 2;

/* Every frame lands here, not only the ones a `capture` asks for - the window wants
   all of them, and a `capture` wants the last one, so one buffer serves both. Which
   is also why there is no "am I capturing" flag to get wrong: the bytes the harness
   compares are whatever the frame it asked for produced. */
void liveDesktopCaptureRow(uint16_t y, uint16_t height, uint16_t width_, const uint8_t* indices)
{
  rows  = height;
  width = width_;
  /* A row past the buffer is counted rather than clamped silently: a mode taller or
     wider than this file expects has to be visible as a failure, not as a short
     capture that reads like a dropped line. */
  if ((uint32_t)y * width_ + width_ > CAPTURE_BYTES)
  {
    ++missed;
    return;
  }
  memcpy(capture + (uint32_t)y * width_, indices, width_);
}

/* One renderer at a time. A board has core 1 and only core 1 drawing; here the raster
   thread further down and whatever command is in flight both call in, and they share
   the capture buffer, the frame geometry and the vertical scale that goes with it. */
#ifdef _WIN32
static SRWLOCK renderLock = SRWLOCK_INIT;
#define RENDER_LOCK()   AcquireSRWLockExclusive(&renderLock)
#define RENDER_UNLOCK() ReleaseSRWLockExclusive(&renderLock)
#else
static pthread_mutex_t renderLock = PTHREAD_MUTEX_INITIALIZER;
#define RENDER_LOCK()   pthread_mutex_lock(&renderLock)
#define RENDER_UNLOCK() pthread_mutex_unlock(&renderLock)
#endif

/* One frame, in the order the firmware's VGA layer calls it: every visible line, the
   end-of-scanline trigger on the line the geometry named, the porch, then the end of
   frame. The geometry a frame runs under is the PREVIOUS frame's, which is not a
   simplification - pico9918_frame_end is the only thing that recomputes it on the
   device too, and the host arms the trigger register from its return value.

   Take the lock over a whole sequence, not a frame at a time, wherever the frames
   have to belong to each other - a capture is two of them and the counters between. */
static void renderFrameUnlocked(void)
{
  viewScale                     = vPixelScale;
  pico9918_scanline_params_t params = {H_VIRTUAL_PIXELS, (uint16_t)vVirtualPixels, false, 0};
  for (int y = 0; y < vVirtualPixels; ++y)
  {
    pico9918_frame_scanline((uint16_t)y, &params, pixels);
    if ((uint32_t)y == triggerLine) pico9918_frame_end_of_scanline();
  }
  pico9918_frame_porch();

  pico9918_frame_display_t display = {V_DISPLAY_LINES, false, vPixelScale, vVirtualPixels};
  pico9918_frame_geometry_t geom   = pico9918_frame_end(CORE_TEMP_C, FRAME_RATE_HZ, &display);
  vPixelScale                  = display.vPixelScale;
  vVirtualPixels               = display.vVirtualPixels;
  triggerLine                  = geom.triggerScanline;
}

static void renderFrame(void)
{
  RENDER_LOCK();
  renderFrameUnlocked();
  RENDER_UNLOCK();
}

/* The frame as a picture, for the viewer: 512 columns by 384 or 480 lines, which is
   what the glass shows however many bytes carried it.

   Expanded here rather than in the viewer for one reason - it is free here. The
   palette is already in PRAM beside the indices, and a viewer at 60 frames a second
   would otherwise spend its whole budget turning 200,000 indices into RGB. What
   crosses the pipe is a finished P6 PPM, which Tk loads from raw bytes directly.

   The three geometries are the same three the harness's own PNG writer resolves: a
   512-byte line is one index a pixel, a 256-byte line in 80-column text is two
   four-bit indices a byte, and every other 256-byte line is one index doubled
   across. Vertically every row is two lines unless R0 is doubling rows. */
static uint8_t view[32 + 512 * 480 * 3];

static size_t buildView(void)
{
  uint8_t rgb[64][3];
  for (int i = 0; i < 64; ++i)
  {
    const uint16_t v = __builtin_bswap16(tms9918->vram.map.pram[i]); /* 0xFRGB */
    rgb[i][0]        = (uint8_t)(((v >> 8) & 0xF) * 0x11);
    rgb[i][1]        = (uint8_t)(((v >> 4) & 0xF) * 0x11);
    rgb[i][2]        = (uint8_t)((v & 0xF) * 0x11);
  }

  /* The height comes from the frame, NOT from R0's row-doubling bit. The two
     disagree for exactly one frame after a mode change - the registers are current
     while the geometry is still the previous frame's - and a 60-row scene giving way
     to a 24-row one then asks for 960 lines. `viewScale` is the scale the frame
     actually ran under, so this cannot skew. The 80-column bit is read live and is
     right to be: the renderer picks the mode up within the frame, unlike the
     geometry. */
  const int packed = width == 256 && (TMS_REGISTER(tms9918, 0x00) & 0x04);
  const int lines  = (int)rows * viewScale;
  if (!rows || lines > 480) return 0;

  int at = snprintf((char*)view, 32, "P6\n512 %d\n255\n", lines);
  for (int line = 0; line < lines; ++line)
  {
    const uint8_t* row = capture + (size_t)(line / viewScale) * width;
    for (int x = 0; x < 512; ++x)
    {
      uint8_t index;
      if (width == 512)
        index = row[x];
      else if (packed)
        index = (x & 1) ? (row[x >> 1] & 0x0F) : (uint8_t)(row[x >> 1] >> 4);
      else
        index = row[x >> 1];
      const uint8_t* c = rgb[index & 0x3F];
      view[at++]       = c[0];
      view[at++]       = c[1];
      view[at++]       = c[2];
    }
  }
  return (size_t)at;
}

/* -------------------------------------------------------------------------
 * The GPU, on a thread of its own.
 *
 * Not for speed - it is because that is the shape of the machine. On a board core
 * 0 runs the GPU program while core 1 renders, so the drawing appears a frame at a
 * time as the program works. Run inline here, a program would render nothing until
 * it finished, and being able to watch one draw is most of why a harness would run
 * a program that takes twenty-three million instructions.
 *
 * A second thread renders under it, which is core 1's half of the same shape, and it
 * is not optional: a program can WAIT on the raster, and one that does gets nothing
 * back from a host whose raster only moves when a command asks for a frame.
 *
 * The threads share VRAM with no lock, and that is the fidelity rather than an
 * oversight: on the device they share it across two cores with no lock either.
 * Every access is a byte, so a reader sees the old value or the new one, and a
 * half-drawn frame is the truth about a half-drawn picture. Rendering is the one
 * thing that IS locked, and only against this process's own second renderer - see
 * renderFrame.
 * ---------------------------------------------------------------------- */
static volatile int gpuBusy = 0;

typedef struct
{
#ifdef _WIN32
  HANDLE handle;
#else
  pthread_t id;
#endif
  int live;
} thread_t;

#ifdef _WIN32
#define THREAD_BODY(name) static DWORD WINAPI name(LPVOID unused)
#define THREAD_DONE       return 0
typedef LPTHREAD_START_ROUTINE thread_body_t;

static int threadStart(thread_t* t, thread_body_t body)
{
  t->handle = CreateThread(NULL, 0, body, NULL, 0, NULL);
  t->live   = t->handle != NULL;
  return t->live;
}

static void threadJoin(thread_t* t)
{
  if (!t->live) return;
  WaitForSingleObject(t->handle, INFINITE);
  CloseHandle(t->handle);
  t->live = 0;
}
#else
#define THREAD_BODY(name) static void* name(void* unused)
#define THREAD_DONE       return NULL
typedef void* (*thread_body_t)(void*);

static int threadStart(thread_t* t, thread_body_t body)
{
  t->live = pthread_create(&t->id, NULL, body, NULL) == 0;
  return t->live;
}

static void threadJoin(thread_t* t)
{
  if (!t->live) return;
  pthread_join(t->id, NULL);
  t->live = 0;
}
#endif

static thread_t gpuThread;
static thread_t rasterThread;

THREAD_BODY(gpuBody)
{
  (void)unused;
  pico9918_gpu_step();
  gpuBusy = 0;
  THREAD_DONE;
}

/* The raster, for as long as a program runs.
 *
 * A program that waits on the scanline register at >7000 - to page a bitmap in the
 * vertical blank, say - only ever sees it move if something is rendering. A board
 * always has core 1 doing that; nothing here did, so such a program waited forever
 * and the harness called it a timeout.
 *
 * Flat out rather than at 60Hz, which is the one place this deliberately parts with
 * the device. A board's raster is paced by its display, so a program that waits on it
 * waits in real time. A harness has no display to wait for, and pacing this would
 * charge a program's wall clock to the frames it waits through rather than to the
 * work it does. */
THREAD_BODY(rasterBody)
{
  (void)unused;
  while (gpuBusy) renderFrame();
  THREAD_DONE;
}

static void gpuReap(void)
{
  threadJoin(&gpuThread);
  threadJoin(&rasterThread);
}

static void gpuStart(uint16_t addr)
{
  gpuReap(); /* whatever ran last, before its thread handles are overwritten */
  tms9918->gpuAddress = addr;
  tms9918->restart    = 1;
  pico9918_gpu_reset_time();
  gpuBusy = 1;
  if (threadStart(&gpuThread, gpuBody))
  {
    /* after the GPU thread, so a failure to spawn it cannot leave this one spinning
       on a gpuBusy nothing will ever clear */
    threadStart(&rasterThread, rasterBody);
  }
  else
  {
    /* no thread to be had: the program still runs, it just cannot be watched, and a
       program that waits on the raster will not come back */
    pico9918_gpu_step();
    gpuBusy = 0;
  }
}

static uint8_t* base(void)
{
  return (uint8_t*)tms9918;
}

static void reply(const char* text)
{
  fputs(text, stdout);
  fputc('\n', stdout);
  fflush(stdout);
}

static int readExactly(void* into, size_t n)
{
  return fread(into, 1, n, stdin) == n;
}

static void writeExactly(const void* from, size_t n)
{
  fwrite(from, 1, n, stdout);
  fflush(stdout);
}

int main(void)
{
#ifdef _WIN32
  /* payloads are raw bytes, and text mode would translate 0x0a in a palette index */
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  pico9918_init();
  pico9918_gpu_init();
  for (int i = 0; i < FRAMES_AT_START; ++i) renderFrame();

  char line[256];
  while (fgets(line, sizeof(line), stdin))
  {
    char command[32] = {0};
    int consumed = 0;
    if (sscanf(line, "%31s%n", command, &consumed) < 1) continue;

    /* the address is hex and the length decimal, so each is parsed where it is used
       rather than through one format string that has to guess the base */
    char* rest = line + consumed;
    unsigned long addr = strtoul(rest, &rest, 16);
    unsigned long count = strtoul(rest, &rest, 10);

    if (!strcmp(command, "quit")) break;

    if (!strcmp(command, "off"))
    {
      printf("instance %u\n", (unsigned)sizeof(*tms9918));
      printf("vram %u\n", (unsigned)offsetof(struct pico9918_s, vram));
      printf("config %u\n", (unsigned)offsetof(struct pico9918_s, config));
      printf("isUnlocked %u\n", (unsigned)offsetof(struct pico9918_s, isUnlocked));
      printf("lockedMask %u\n", (unsigned)offsetof(struct pico9918_s, lockedMask));
      printf("palDirty %u\n", (unsigned)offsetof(struct pico9918_s, palDirty));
      printf("configDirty %u\n", (unsigned)offsetof(struct pico9918_s, configDirty));
      reply("end");
    }
    else if (!strcmp(command, "palette"))
    {
      uint16_t entries[64];
      for (int i = 0; i < 64; ++i) entries[i] = pico9918_default_palette(i);
      printf("data %u\n", (unsigned)sizeof(entries));
      fflush(stdout);
      writeExactly(entries, sizeof(entries));
    }
    else if (!strcmp(command, "w"))
    {
      if (addr + count > sizeof(*tms9918))
      {
        reply("error out of range");
        continue;
      }
      if (!readExactly(base() + addr, count)) return 1;
      reply("ok");
    }
    else if (!strcmp(command, "r"))
    {
      if (addr + count > sizeof(*tms9918))
      {
        reply("error out of range");
        continue;
      }
      printf("data %lu\n", count);
      fflush(stdout);
      writeExactly(base() + addr, count);
    }
    else if (!strcmp(command, "frames"))
    {
      /* its one argument is a count, so it is decimal - `addr` above is not it */
      unsigned long n = strtoul(line + consumed, NULL, 10);
      for (unsigned long i = 0; i < (n ? n : 1); ++i) renderFrame();
      reply("ok");
    }
    else if (!strcmp(command, "view"))
    {
      /* one frame, as a picture. The viewer paces itself: the render is well under a
         millisecond, so where 60 frames a second is decided is the side with a clock
         that can wait, not this one. */
      RENDER_LOCK();
      renderFrameUnlocked();
      size_t n = buildView(); /* reads the buffer that frame just filled */
      RENDER_UNLOCK();
      if (!n)
      {
        reply("error nothing rendered yet");
        continue;
      }
      printf("view %u\n", (unsigned)n);
      fflush(stdout);
      writeExactly(view, n);
    }
    else if (!strcmp(command, "gpu"))
    {
      gpuStart((uint16_t)addr);
      reply("ok");
    }
    else if (!strcmp(command, "gpupoll"))
    {
      /* The microseconds are the library's own accumulator - the one the diagnostics
         overlay reports from on a board - not a stopwatch held out here. */
      if (gpuBusy)
        reply("gpu running");
      else
      {
        gpuReap();
        printf("gpu done %lu\n", (unsigned long)pico9918_gpu_time(0));
        fflush(stdout);
      }
    }
    else if (!strcmp(command, "gpustop"))
    {
      /* The same switch the device's harness uses and the same one a program uses to
         finish: run9900 tests this byte every instruction. */
      TMS_REGISTER(tms9918, 0x38) = 0;
      reply("ok");
    }
    else if (!strcmp(command, "capture"))
    {
      /* One frame first, discarded. The geometry a frame runs under is the previous
         frame's - pico9918_frame_end is what recomputes it - so a scene that changed
         the row count still renders at the old height for one more frame. The device
         gets this for free: its capture arms at the next frame boundary, so a whole
         frame always separates the scene from the picture. Without it twelve scenes
         come back at the PREVIOUS scene's height. */
      RENDER_LOCK();
      renderFrameUnlocked();

      rows = width = missed = 0;
      renderFrameUnlocked();
      RENDER_UNLOCK();
      if (missed)
      {
        reply("error capture overflowed");
        continue;
      }
      printf("capture %lu %lu\n", (unsigned long)rows, (unsigned long)width);
      fflush(stdout);
      writeExactly(capture, (size_t)rows * width);
    }
    else
    {
      reply("error unknown command");
    }
  }
  return 0;
}
