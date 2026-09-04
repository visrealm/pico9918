/**
 * \file
 * \brief pico9918-core - the installed package, compiled with no build system at all
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Deliberately NOT a target in the CMakeLists.txt beside this file. ci.sh package hands
 * it to the compiler directly, with one include directory, the installed archive, and
 * PICO9918_STATIC - which the LINKAGE MODES block in pico9918.h requires of any static
 * consumer. Nothing else.
 *
 * That is the whole point. The exported CMake target carries the instance mode as an
 * INTERFACE compile definition, so a consumer that finds the package can never disagree
 * with the archive; one that links it by hand has only the generated
 * pico9918_build_config.h to go on. Both ABIs are spelled out below rather than written
 * through PICO9918_INST_ONLY, so a header that picked the wrong mode is a compile error
 * here instead of a wrong argument list at runtime.
 */

#include "pico9918.h"

int main(void)
{
#if PICO9918_BUILD_SINGLE_INSTANCE
  pico9918_init();
  pico9918_reset();
#else
  pico9918_t* tms9918 = pico9918_new();
  if (!tms9918)
  {
    return 1;
  }
  pico9918_reset(tms9918);
  pico9918_destroy(tms9918);
#endif
  return 0;
}
