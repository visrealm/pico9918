/**
 * \file
 * \brief pico9918-core - the library as a Python module
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * A VDP you can drive from a REPL, a notebook or a test. The surface is the host's:
 * the two ports a guest machine has, the register and VRAM shortcuts every caller
 * writes on top of them, and the scanline calls that read back what was drawn.
 *
 * Nothing here reaches inside the instance. The suite's `access.VdpAccess` does -
 * it pokes lock flags and palette RAM through a debug transport, because a scene
 * has to set up states a host cannot reach - and the two are not the same tool.
 *
 * The GIL is held across every call, gpu_step() included, and that is deliberate:
 * pico9918_scan_line is not re-entrant, the cached mode and line source are shared
 * between instances, and the GPU time accumulator takes no instance at all. See
 * BINDINGS-PLAN.md for what releasing it would cost.
 */

#define PY_SSIZE_T_CLEAN

/* A standard CPython install ships no debug library, but pyconfig.h asks the linker
   for one whenever _DEBUG is set. Only around the include. */
#if defined(_MSC_VER) && defined(_DEBUG) && !defined(Py_DEBUG)
#define PICO9918_RESTORE_DEBUG
#undef _DEBUG
#endif
#include <Python.h>
/* The member constants moved into Python.h in 3.12 and structmember.h is on its way out. */
#ifndef Py_T_PYSSIZET
#include <structmember.h>
#define Py_T_PYSSIZET T_PYSSIZET
#define Py_READONLY   READONLY
#endif
#ifdef PICO9918_RESTORE_DEBUG
#define _DEBUG 1
#undef PICO9918_RESTORE_DEBUG
#endif

#include "pico9918.h"
#include "pico9918_frame.h"
#include "pico9918_util.h"

#if PICO9918_BUILD_MODE
#include "gpu/gpu.h"
#endif

#if PICO9918_SINGLE_INSTANCE
#error "the module is one class per VDP - build the library PICO9918_SINGLE_INSTANCE=0"
#endif

#include <stdlib.h>
#include <string.h>

/* An explicit weakref slot rather than Py_TPFLAGS_MANAGED_WEAKREF: the managed form puts
   the list in an allocation pre-header that only a GC type's tp_free knows how to give
   back, and this type holds no PyObject and has no business being tracked. */
typedef struct
{
  PyObject_HEAD pico9918_t* vdp;
  PyObject* weakreflist;
  /* where raster() has got to, per instance - see it for why a VDP driven from
     Python has a display at all */
  uint16_t rasterY;
  uint16_t rasterLines;
  uint8_t rasterScale;
} VdpObject;

/* The display raster() drives. A caller reading indices back has no display and needs
   none, so these are not settable: what they are for is giving the F18A's scanline
   register something to count, and 640x480 at 60Hz is the shipping VGA mode. */
#define RASTER_WIDTH 640
#define RASTER_LINES 480

/* -------------------------------------------------------------------------
 * Argument conversion
 *
 * One converter for every integer parameter, raising TypeError for the whole
 * out-of-range domain: splitting one input domain across OverflowError for a byte and
 * ValueError for an address is worse than either alone. PyNumber_Index decides the
 * rest - bool converts, float and str and None do not.
 * ---------------------------------------------------------------------- */

static int convertUnsigned(PyObject* value, unsigned long long limit, unsigned long long* out)
{
  PyObject* index = PyNumber_Index(value);
  if (index == NULL) return 0;

  int overflow     = 0;
  long long asLong = PyLong_AsLongLongAndOverflow(index, &overflow);
  Py_DECREF(index);

  if (asLong == -1 && PyErr_Occurred()) return 0;
  if (overflow != 0 || asLong < 0 || (unsigned long long)asLong > limit)
  {
    PyErr_Format(PyExc_TypeError, "expected an integer in 0..%llu", limit);
    return 0;
  }

  *out = (unsigned long long)asLong;
  return 1;
}

static int convertU8(PyObject* value, void* out)
{
  unsigned long long converted;
  if (!convertUnsigned(value, 0xffull, &converted)) return 0;
  *(uint8_t*)out = (uint8_t)converted;
  return 1;
}

static int convertU16(PyObject* value, void* out)
{
  unsigned long long converted;
  if (!convertUnsigned(value, 0xffffull, &converted)) return 0;
  *(uint16_t*)out = (uint16_t)converted;
  return 1;
}

static int convertU32(PyObject* value, void* out)
{
  unsigned long long converted;
  if (!convertUnsigned(value, 0xffffffffull, &converted)) return 0;
  *(uint32_t*)out = (uint32_t)converted;
  return 1;
}

static int convertSize(PyObject* value, void* out)
{
  unsigned long long converted;
  if (!convertUnsigned(value, (unsigned long long)PY_SSIZE_T_MAX, &converted)) return 0;
  *(size_t*)out = (size_t)converted;
  return 1;
}

/* -------------------------------------------------------------------------
 * Construction
 * ---------------------------------------------------------------------- */

/* The chip leaves VRAM as it found it and pico9918_reset does the same, but an object
   handing back whatever the allocator held would make every result depend on it. */
static void resetInstance(pico9918_t* vdp)
{
  pico9918_reset(vdp);
  pico9918_set_address_write(vdp, 0);
  pico9918_write_byte_rpt(vdp, 0, 0x4000);
}

/* In tp_new rather than tp_init: copy and pickle both reach __new__ without __init__,
   and an object whose instance pointer is not yet set is one a method dereferences. */
static PyObject* vdpNew(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, ":pico9918.Vdp", kwlist)) return NULL;

  VdpObject* self = (VdpObject*)type->tp_alloc(type, 0);
  if (self == NULL) return NULL;

  self->vdp = pico9918_new();
  if (self->vdp == NULL)
  {
    Py_DECREF(self);
    return PyErr_NoMemory();
  }

  self->rasterY     = 0;
  self->rasterLines = RASTER_LINES;
  self->rasterScale = 1;

  resetInstance(self->vdp);
  return (PyObject*)self;
}

/* tp_free before the type's own decref: the decref can drop the type's last reference,
   and tp_free is read from it. pico9918_destroy is free(), so a half-built object is safe. */
static void vdpDealloc(VdpObject* self)
{
  PyTypeObject* type = Py_TYPE(self);

  if (self->weakreflist != NULL) PyObject_ClearWeakRefs((PyObject*)self);

  pico9918_destroy(self->vdp);
  type->tp_free((PyObject*)self);
  Py_DECREF(type);
}

static PyObject* vdpReset(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  resetInstance(self->vdp);
  Py_RETURN_NONE;
}

/* -------------------------------------------------------------------------
 * The host bus, byte for byte
 * ---------------------------------------------------------------------- */

static PyObject* vdpWriteAddr(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"data", NULL};
  uint8_t data;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&:write_addr", kwlist, convertU8, &data)) return NULL;
  pico9918_write_addr(self->vdp, data);
  Py_RETURN_NONE;
}

static PyObject* vdpWriteData(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"data", NULL};
  uint8_t data;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&:write_data", kwlist, convertU8, &data)) return NULL;
  pico9918_write_data(self->vdp, data);
  Py_RETURN_NONE;
}

static PyObject* vdpReadStatus(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  return PyLong_FromLong(pico9918_read_status(self->vdp));
}

static PyObject* vdpPeekStatus(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  return PyLong_FromLong(pico9918_peek_status(self->vdp));
}

static PyObject* vdpReadData(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  return PyLong_FromLong(pico9918_read_data(self->vdp));
}

/* -------------------------------------------------------------------------
 * The two-byte commands that bus carries
 * ---------------------------------------------------------------------- */

static PyObject* vdpWriteReg(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"reg", "value", NULL};
  uint8_t reg, value;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&O&:write_reg", kwlist, convertU8, &reg, convertU8, &value))
    return NULL;
  pico9918_write_register_value(self->vdp, (pico9918_register_t)reg, value);
  Py_RETURN_NONE;
}

static PyObject* vdpWriteRegs(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"values", NULL};
  PyObject* values;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O:write_regs", kwlist, &values)) return NULL;

  /* PySequence_Fast takes any iterable, and a string of register values is a mistake */
  if (PyBytes_Check(values) || PyUnicode_Check(values))
  {
    PyErr_SetString(PyExc_TypeError, "expected a sequence of register values");
    return NULL;
  }

  PyObject* fast = PySequence_Fast(values, "expected a sequence of register values");
  if (fast == NULL) return NULL;

  const Py_ssize_t count = PySequence_Fast_GET_SIZE(fast);
  for (Py_ssize_t i = 0; i < count; ++i)
  {
    uint8_t value;
    if (!convertU8(PySequence_Fast_GET_ITEM(fast, i), &value))
    {
      Py_DECREF(fast);
      return NULL;
    }
    pico9918_write_register_value(self->vdp, (pico9918_register_t)(uint8_t)i, value);
  }

  Py_DECREF(fast);
  Py_RETURN_NONE;
}

static PyObject* vdpReg(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"index", NULL};
  uint8_t index;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&:reg", kwlist, convertU8, &index)) return NULL;
  return PyLong_FromLong(pico9918_reg_value(self->vdp, (pico9918_register_t)index));
}

/* The converter runs ahead of s*, so a rejected address cannot leave a buffer exported. */
static PyObject* vdpWriteVram(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"addr", "data", NULL};
  uint16_t addr;
  Py_buffer data;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&s*:write_vram", kwlist, convertU16, &addr, &data))
    return NULL;

  pico9918_set_address_write(self->vdp, addr);
  pico9918_write_bytes(self->vdp, (const uint8_t*)data.buf, (size_t)data.len);
  PyBuffer_Release(&data);
  Py_RETURN_NONE;
}

static PyObject* vdpReadVram(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"addr", "length", NULL};
  uint16_t addr;
  size_t length;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&O&:read_vram", kwlist, convertU16, &addr, convertSize,
                                   &length))
    return NULL;

  if (length == 0) return PyBytes_FromStringAndSize(NULL, 0);

  uint8_t* out = (uint8_t*)malloc(length);
  if (out == NULL) return PyErr_NoMemory();

  for (size_t i = 0; i < length; ++i) out[i] = pico9918_vram_value(self->vdp, (uint16_t)(addr + i));

  PyObject* result = PyBytes_FromStringAndSize((const char*)out, (Py_ssize_t)length);
  free(out);
  return result;
}

/* Two writes of 0x1c to R57, which is the only register write a locked device takes. */
static PyObject* vdpUnlock(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  pico9918_write_register_value(self->vdp, (pico9918_register_t)0x39, 0x1c);
  pico9918_write_register_value(self->vdp, (pico9918_register_t)0x39, 0x1c);
  Py_RETURN_NONE;
}

/* -------------------------------------------------------------------------
 * Rendering: one line at a time, into a buffer the instance owns
 * ---------------------------------------------------------------------- */

static PyObject* vdpScanLine(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"y", NULL};
  uint16_t y;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&:scan_line", kwlist, convertU16, &y)) return NULL;
  return PyLong_FromLong(pico9918_scan_line(self->vdp, y));
}

static PyObject* vdpLineBytes(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  return PyLong_FromUnsignedLong(pico9918_line_bytes(self->vdp));
}

/* No copy: the source is NULL until the first scan, and PyBytes_FromStringAndSize
   returns an uninitialised object of that length for one where a memcpy would fault. */
static PyObject* vdpLine(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  return PyBytes_FromStringAndSize((const char*)pico9918_line_source(self->vdp),
                                   (Py_ssize_t)pico9918_line_bytes(self->vdp));
}

/*
 * Sized from the compile-time maximum, never from a probe: pico9918_line_bytes is
 * 512 for an unlocked 80-column row on the 8bpp tier and 256 otherwise, the mode it
 * reads is cached in a file-scope static shared between instances, and the first
 * scan_line of the loop can change it. The running total is the length.
 */
static PyObject* renderFrame(VdpObject* self, uint16_t rows, int asRgb)
{
  if (rows == 0) return PyBytes_FromStringAndSize(NULL, 0);

  const size_t stride = (size_t)PICO9918_SCANLINE_BUFFER_SIZE * (asRgb ? 3u : 1u);
  uint8_t* out        = (uint8_t*)malloc((size_t)rows * stride);
  if (out == NULL) return PyErr_NoMemory();

  size_t total = 0;
  for (uint16_t y = 0; y < rows; ++y)
  {
    pico9918_scan_line(self->vdp, y);

    const uint8_t* src  = pico9918_line_source(self->vdp);
    const uint32_t used = pico9918_line_bytes(self->vdp);

    if (asRgb)
    {
      for (uint32_t x = 0; x < used; ++x)
      {
        const uint16_t argb = pico9918_default_palette(src[x] & 0x3f);
        out[total++]        = (uint8_t)(((argb >> 8) & 0xf) * 17);
        out[total++]        = (uint8_t)(((argb >> 4) & 0xf) * 17);
        out[total++]        = (uint8_t)((argb & 0xf) * 17);
      }
    }
    else
    {
      memcpy(out + total, src, used);
      total += used;
    }
  }

  PyObject* result = PyBytes_FromStringAndSize((const char*)out, (Py_ssize_t)total);
  free(out);
  return result;
}

/* One palette index a byte, `rows` of them - what an assertion wants, because it is
   what the renderer decided rather than what a palette turned that into. */
static PyObject* vdpIndices(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"rows", NULL};
  uint16_t rows         = 192;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O&:indices", kwlist, convertU16, &rows)) return NULL;
  return renderFrame(self, rows, 0);
}

/* The same frame as RGB triples, coloured by the library's boot palette. A program
   that writes palette RAM is not drawn by this - read indices() and colour them. */
static PyObject* vdpRgb(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"rows", NULL};
  uint16_t rows         = 192;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O&:rgb", kwlist, convertU16, &rows)) return NULL;
  return renderFrame(self, rows, 1);
}

/* -------------------------------------------------------------------------
 * Status
 * ---------------------------------------------------------------------- */

static PyObject* vdpInterruptStatus(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  return PyBool_FromLong(pico9918_interrupt_status(self->vdp));
}

static PyObject* vdpInterruptSet(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  pico9918_interrupt_set(self->vdp);
  Py_RETURN_NONE;
}

static PyObject* vdpDisplayEnabled(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  return PyBool_FromLong(pico9918_display_enabled(self->vdp));
}

static PyObject* vdpDisplayMode(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  return PyLong_FromLong((long)pico9918_display_mode(self->vdp));
}

/* -------------------------------------------------------------------------
 * The GPU. Its time accumulator takes no instance - it is process-wide.
 * ---------------------------------------------------------------------- */

#if PICO9918_BUILD_MODE

static PyObject* vdpGpuInit(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  pico9918_gpu_init(self->vdp);
  Py_RETURN_NONE;
}

static PyObject* vdpGpuStep(VdpObject* self, PyObject* Py_UNUSED(ignored))
{
  pico9918_gpu_step(self->vdp);
  Py_RETURN_NONE;
}

static PyObject* vdpGpuStepN(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[]  = {"instructions", NULL};
  uint32_t instructions = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&:gpu_step_n", kwlist, convertU32,
                                   &instructions))
    return NULL;
  return PyBool_FromLong(pico9918_gpu_step_n(self->vdp, instructions));
}

static PyObject* vdpGpuTime(VdpObject* Py_UNUSED(self), PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"total_time", NULL};
  uint32_t totalTime    = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O&:gpu_time", kwlist, convertU32, &totalTime)) return NULL;
  return PyLong_FromUnsignedLong(pico9918_gpu_time(totalTime));
}

static PyObject* vdpGpuResetTime(VdpObject* Py_UNUSED(self), PyObject* Py_UNUSED(ignored))
{
  pico9918_gpu_reset_time();
  Py_RETURN_NONE;
}

/* Advance the display raster, which is the one thing a GPU program can wait on and the
   one thing scan_line() does not do. scan_line renders a line; it does not publish one,
   and the F18A's scanline register at >7000 is what a program polls to page a bitmap in
   the vertical blank. Without this, such a program never comes back - and interleaving
   it with gpu_step_n() is the whole shape of a single-threaded host.

   The pixels are discarded. A caller reading indices back through rgb() or indices()
   wants nothing from this but the raster's position. */
static PyObject* vdpRaster(VdpObject* self, PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"lines", NULL};
  uint32_t lines        = 1;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O&:raster", kwlist, convertU32, &lines))
    return NULL;

  static PICO9918_PIXEL_T pixels[RASTER_WIDTH + 16];
  unsigned long frames = 0;

  while (lines--)
  {
    if (self->rasterY < self->rasterLines)
    {
      pico9918_scanline_params_t params = {RASTER_WIDTH, self->rasterLines, false, 0};
      pico9918_frame_scanline(self->vdp, self->rasterY++, &params, pixels);
      continue;
    }

    pico9918_frame_porch(self->vdp);
    pico9918_frame_display_t display = {RASTER_LINES, false, self->rasterScale,
                                        self->rasterLines};
    pico9918_frame_end(self->vdp, 30.0f, 60.0f, &display);
    self->rasterScale = display.vPixelScale;
    self->rasterLines = display.vVirtualPixels;
    self->rasterY     = 0;
    ++frames;
  }
  return PyLong_FromUnsignedLong(frames);
}

#endif /* PICO9918_BUILD_MODE */

/* -------------------------------------------------------------------------
 * The type
 * ---------------------------------------------------------------------- */

#define VDP_KW(name, fn, doc)     {name, (PyCFunction)(void (*)(void))fn, METH_VARARGS | METH_KEYWORDS, doc}
#define VDP_NOARGS(name, fn, doc) {name, (PyCFunction)fn, METH_NOARGS, doc}

static PyMethodDef vdpMethods[] = {
  VDP_NOARGS("reset", vdpReset, "reset the registers and clear VRAM"),
  VDP_KW("write_addr", vdpWriteAddr, NULL),
  VDP_KW("write_data", vdpWriteData, NULL),
  VDP_NOARGS("read_status", vdpReadStatus, "SR0, clearing the frame and fifth-sprite flags"),
  VDP_NOARGS("peek_status", vdpPeekStatus, "SR0, clearing nothing"),
  VDP_NOARGS("read_data", vdpReadData, NULL),
  VDP_KW("write_reg", vdpWriteReg, NULL),
  VDP_KW("write_regs", vdpWriteRegs, "registers 0 upwards, in order"),
  VDP_KW("reg", vdpReg, NULL),
  VDP_KW("write_vram", vdpWriteVram, NULL),
  VDP_KW("read_vram", vdpReadVram, NULL),
  VDP_NOARGS("unlock", vdpUnlock, "the F18A unlock sequence"),
  VDP_KW("scan_line", vdpScanLine, "render one line, returning SR0"),
  VDP_NOARGS("line_bytes", vdpLineBytes, NULL),
  VDP_NOARGS("line", vdpLine, "the last rendered line, one palette index a byte"),
  VDP_KW("indices", vdpIndices, NULL),
  VDP_KW("rgb", vdpRgb, NULL),
  VDP_NOARGS("interrupt_status", vdpInterruptStatus, "/INT: R1's enable bit and SR0's frame flag"),
  VDP_NOARGS("interrupt_set", vdpInterruptSet, NULL),
  VDP_NOARGS("display_enabled", vdpDisplayEnabled, NULL),
  VDP_NOARGS("display_mode", vdpDisplayMode, NULL),
#if PICO9918_BUILD_MODE
  VDP_NOARGS("gpu_init", vdpGpuInit, NULL),
  VDP_NOARGS("gpu_step", vdpGpuStep, "run the pending GPU program to completion"),
  VDP_KW("gpu_step_n", vdpGpuStepN,
         "run at most `instructions` of it, True while it still has work"),
  VDP_KW("raster", vdpRaster,
         "advance the display raster `lines` lines, returning the frames completed"),
  VDP_KW("gpu_time", vdpGpuTime, NULL),
  VDP_NOARGS("gpu_reset_time", vdpGpuResetTime, NULL),
#endif
  {NULL, NULL, 0, NULL}};

static PyMemberDef vdpMembers[] = {
  {"__weaklistoffset__", Py_T_PYSSIZET, offsetof(VdpObject, weakreflist), Py_READONLY, NULL},
  {NULL, 0, 0, 0, NULL}};

static PyType_Slot vdpSlots[] = {{Py_tp_new, (void*)vdpNew},
                                 {Py_tp_dealloc, (void*)vdpDealloc},
                                 {Py_tp_methods, (void*)vdpMethods},
                                 {Py_tp_members, (void*)vdpMembers},
                                 {Py_tp_doc, (void*)"one VDP instance"},
                                 {0, NULL}};

static PyType_Spec vdpSpec = {"pico9918.Vdp", sizeof(VdpObject), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
                              vdpSlots};

/* -------------------------------------------------------------------------
 * The module
 * ---------------------------------------------------------------------- */

static PyObject* moduleDefaultPalette(PyObject* Py_UNUSED(self), PyObject* args, PyObject* kwds)
{
  static char* kwlist[] = {"index", NULL};
  int index;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "i:default_palette", kwlist, &index)) return NULL;
  return PyLong_FromLong(pico9918_default_palette(index));
}

static PyMethodDef moduleMethods[] = {{"default_palette", (PyCFunction)(void (*)(void))moduleDefaultPalette,
                                       METH_VARARGS | METH_KEYWORDS, "one entry of the boot palette, 0xFRGB"},
                                      {NULL, NULL, 0, NULL}};

/* Single-phase: a multi-phase module without Py_mod_multiple_interpreters declares
   subinterpreter support, and the cached mode, the line source and the GPU time
   accumulator are file-scope statics that no per-interpreter copy would separate. */
static struct PyModuleDef moduleDef = {PyModuleDef_HEAD_INIT,
                                       "pico9918",
                                       "pico9918-core: a TMS9918A / F18A video display processor",
                                       -1,
                                       moduleMethods,
                                       NULL,
                                       NULL,
                                       NULL,
                                       NULL};

/* PyModule_AddObject steals only on success. */
static int addObject(PyObject* module, const char* name, PyObject* value)
{
  if (value == NULL) return -1;
  if (PyModule_AddObject(module, name, value) < 0)
  {
    Py_DECREF(value);
    return -1;
  }
  return 0;
}

PyMODINIT_FUNC PyInit_pico9918(void)
{
  PyObject* module = PyModule_Create(&moduleDef);
  if (module == NULL) return NULL;

  PyObject* type = PyType_FromSpec(&vdpSpec);
  if (addObject(module, "Vdp", type) < 0) goto failed;

  if (PyModule_AddIntConstant(module, "PIXELS_X", TMS9918_PIXELS_X) < 0) goto failed;
  if (PyModule_AddIntConstant(module, "PIXELS_Y", TMS9918_PIXELS_Y) < 0) goto failed;
  if (PyModule_AddIntConstant(module, "SCANLINE_BUFFER_SIZE", PICO9918_SCANLINE_BUFFER_SIZE) < 0) goto failed;

  /* what the linked library was compiled as, not what this module asked for */
  if (PyModule_AddIntConstant(module, "MODE", PICO9918_BUILD_MODE) < 0) goto failed;
  if (PyModule_AddIntConstant(module, "PIXEL_SIZE", PICO9918_BUILD_PIXEL_SIZE) < 0) goto failed;
  if (addObject(module, "TEXT80_8BPP", PyBool_FromLong(PICO9918_BUILD_TEXT80_8BPP)) < 0) goto failed;

  return module;

failed:
  Py_DECREF(module);
  return NULL;
}
