/**
 * \file
 * \brief pico9918-core - the installed package, included and linked from C++
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Every public header compiled as C++, then linked. Two failures a C consumer cannot
 * see, both of which a real host hit:
 *
 *   - a C11 construct in a public header. _Static_assert is the one that bites, because
 *     MSVC does not provide it in C++ mode, and it sits in headers a host includes;
 *   - a declaration without C linkage. The name mangles, and the link fails against an
 *     archive that plainly contains the symbol.
 *
 * So the calls below are chosen for their headers rather than their behaviour: one
 * reference into each public header is what makes this a link test. Written for either
 * instance mode, like the C consumer beside it.
 */

#include "overlay/diag.h"
#include "overlay/splash.h"
#include "pico9918.h"
#include "pico9918_config.h"
#include "pico9918_frame.h"
#include "pico9918_util.h"

#if PICO9918_BUILD_MODE
#include "gpu/gpu.h"
#endif

#include <cstddef>
#include <cstdio>

int main()
{
#if PICO9918_SINGLE_INSTANCE
  pico9918_init();
#else
  pico9918_t* tms9918 = pico9918_new();
  if (!tms9918)
  {
    std::printf("pico9918_new failed\n");
    return 1;
  }
#endif

  pico9918_reset(PICO9918_INST_ONLY);

  /* pico9918_config.h - the header that carries no PICO9918_ macro, so its linkage is
     its own #ifdef __cplusplus guard and nothing else. */
  uint8_t* config = pico9918_config(PICO9918_INST_ONLY);
  if (!config || pico9918_config_field_count == 0)
  {
    std::printf("config block or descriptor table missing\n");
    return 1;
  }
  pico9918_config_apply(PICO9918_INST_ONLY);

  /* pico9918_util.h - exported entry points and an exported table, past the inline
     helpers that would link even unguarded. */
  pico9918_initialise_gfx_i(PICO9918_INST_ONLY);
  const uint32_t white = pico9918_palette[TMS_WHITE];

  /* Address-taken rather than called: these either never return or want a frame's worth
     of host state, and linking them is the whole point here. */
  const void* referenced[] = {
    (const void*)&pico9918_frame_scanline,
    (const void*)&pico9918_diag_init,
    (const void*)&pico9918_splash_reset,
#if PICO9918_BUILD_MODE
    (const void*)&pico9918_gpu_init,
    (const void*)&pico9918_gpu_loop,
#endif
  };

  for (std::size_t i = 0; i < sizeof(referenced) / sizeof(referenced[0]); ++i)
  {
    if (!referenced[i])
    {
      std::printf("a public entry point resolved to null\n");
      return 1;
    }
  }

  std::printf("pico9918-core: %zu config fields, palette white 0x%08x, %zu entry points\n",
              (std::size_t)pico9918_config_field_count, (unsigned)white,
              sizeof(referenced) / sizeof(referenced[0]));

#if !PICO9918_SINGLE_INSTANCE
  pico9918_destroy(tms9918);
#endif
  return 0;
}
