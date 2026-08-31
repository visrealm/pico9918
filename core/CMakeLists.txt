cmake_minimum_required(VERSION 3.22)


set(LIBRARY pico9918_core)

set(CMAKE_C_STANDARD 11)

option(PICO9918_GOLDEN "Build the desktop golden-frame regression harness" OFF)

# Set by any target that compares committed bytes - the goldens and the live
# harness both do - so codegen cannot vary with the machine that ran the build.
option(PICO9918_PORTABLE_CODEGEN "Machine-independent codegen" OFF)

# -Wall -Wextra -Werror. A CI gate rather than a default: a compiler upgrade must
# never break a consumer's build of a released library.
option(PICO9918_WERROR "Treat compiler warnings as errors" OFF)
if (PICO9918_GOLDEN)
  set(PICO9918_PORTABLE_CODEGEN ON)
endif()

# 80-column text at eight bits a pixel: the tile palette select, ECM, the bitmap layer
# and the shared composite there, none of which four bits a pixel can represent. It
# doubles the line to 512 bytes, so it changes a size the public header publishes and
# has to reach consumers. The firmware sets it from the board.
option(PICO9918_TEXT80_8BPP "80-column text at 8bpp: ECM, palette select and bitmap layer in T80" OFF)

# Splash overlay. OFF (the default) still generates the splash asset; ON drops
# both the asset and its render path, so the image costs nothing in flash.
option(PICO9918_NO_SPLASH "Omit the splash overlay and its image asset" OFF)

# Which chip an instance answers as, chosen per instance at runtime rather than by
# this file: a TMS9918A, an F18A, or a PICO9918. OFF is the default and is what a
# board builds - every gate folds to a constant, the instance carries no extra field
# and the entry points are not compiled, so a firmware pays nothing for a switch it
# never throws. A host emulating more than one chip out of one archive turns it on.
# It needs the F18A build: the personalities above the base are not in a MODE=0
# archive to select.
option(PICO9918_RUNTIME_CHIP "Select the chip personality at runtime" OFF)

# Diagnostics overlay: optional GPU-frames row. The count is host-pushed, so the
# row and the setter that feeds it appear together or not at all.
option(PICO9918_DIAG_GPU_FRAME_COUNTER "Include the diag GPU-frames row" OFF)

# The splash PNG, relative to src/overlay/. The host selects the board-
# appropriate image; the generator emits the canonical `splash*` symbols either
# way, so no consuming TU needs a per-board #ifdef.
if(NOT DEFINED PICO9918_SPLASH_IMAGE)
  set(PICO9918_SPLASH_IMAGE res/splash.png)
endif()

# The library's version, and what `find_package(pico9918_core 1.3)` matches. Kept
# in step with the firmware release it is generated from - the firmware's root
# CMakeLists checks the two agree.
project(pico9918_core VERSION 1.3.0 LANGUAGES C)

#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)

if(MSVC)
  add_compile_options(/W4 /WX)
  # /W4 asks for diagnostics the GCC build is not asked for either. Each of
  # these is off there too, so the library is not written to satisfy it:
  #   4244 4245  narrowing and sign conversions     -Wconversion -Wsign-conversion
  #   4459 4456  a local hiding a global or a local -Wshadow
  #   4100       unreferenced formal parameter      -Wunused-parameter
  #   4018       signed/unsigned comparison         -Wsign-compare
  #   4146       unary minus on an unsigned type    the deliberate 0/~0 mask idiom
  #   4324       padding added for an alignment specifier, which is the point of it
  #   4127       a constant condition, which is how a build folds a feature away:
  #              TEXT80_WIDE_ROW without the tier, PICO9918_UNLOCKED without the F18A
  add_compile_options(/wd4244 /wd4245 /wd4459 /wd4456 /wd4100 /wd4018 /wd4146 /wd4324
                      /wd4127)
  # strncpy, in the diagnostics overlay
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

  # The same rule as the GCC side below, and for the same reason: a build whose
  # output is compared byte-for-byte cannot take machine-specific codegen. The
  # optimisation level is CMAKE_BUILD_TYPE's to choose - pinning /Ox here fought
  # it, and Debug's /RTC1 is rejected outright alongside it.
  if (NOT PICO9918_PORTABLE_CODEGEN)
    add_compile_options(/arch:AVX2)
  endif()
else()
  # Four families are excused, each with existing instances and a reason:
  #   unused-parameter      the cloned emitters share one signature, and a clone
  #                         that folds a parameter away still has to take it
  #   sign-compare          index arithmetic against unsigned bounds
  #   implicit-fallthrough  the mode dispatch falls through on purpose
  #   switch                naming TMS_MODE_COUNT in that dispatch grows the jump
  #                         table for a value the mode decode cannot produce
  # Anything outside them is new, and breaks the build.
  if (PICO9918_WERROR)
    add_compile_options(-Wall -Wextra -Wpedantic -Werror
                        -Wno-unused-parameter -Wno-sign-compare
                        -Wno-implicit-fallthrough -Wno-switch)
  endif()

  # A build whose output is compared byte-for-byte across machines cannot take
  # this: the capture and the verification are not the same CPU.
  if (UNIX AND NOT CMAKE_CROSSCOMPILING AND NOT PICO9918_PORTABLE_CODEGEN)
    add_compile_options(-march=native)
  endif()
endif()

# Golden builds select the SHIPPING (Pico BGR16) pixel policy through the
# documented host-override mechanism, so the captured post-palette surface is
# the one the device produces rather than the desktop RGBA8888 default. Applied
# to the library and the harness alike - both must agree on PICO9918_PIXEL_T.
#
# goldenClock.h rides the same mechanism to replace the library's one wall-clock
# read with a deterministic counter. It must reach the LIBRARY's TUs,
# not just the harness: the clock is read inside diag.c and
# pico9918.c, so a harness-only define would change nothing.
if (PICO9918_GOLDEN)
  if (MSVC)
    # /FI takes its file in the same token, so neither of the two can be
    # de-duplicated away.
    add_compile_options(
      "/FI${CMAKE_CURRENT_SOURCE_DIR}/test/golden/goldenPixelPolicy.h"
      "/FI${CMAKE_CURRENT_SOURCE_DIR}/test/golden/goldenClock.h")
  else()
    # One option per -include, and each pairs the flag with its file via SHELL: so
    # CMake does not de-duplicate the two identical "-include" tokens - it does,
    # leaving one bare filename that gcc reads as a second input ("cannot specify
    # '-o' with '-c' ... with multiple files").
    add_compile_options(
      "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/test/golden/goldenPixelPolicy.h"
      "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/test/golden/goldenClock.h")
  endif()
endif()

#include(CTest)

add_subdirectory(src)

if (PICO9918_GOLDEN)
  add_subdirectory(test/golden)
endif()

# The examples build against pico9918::core the way a consumer does, so they are
# also a check that the alias and the public headers are enough on their own.
# Off by default: a host that vendors the library wants the library, not its
# example programs.
option(PICO9918_EXAMPLES "Build the example programs" OFF)
if (PICO9918_EXAMPLES)
  add_subdirectory(examples)
endif()

# The Python module. Off by default; the archive it links into a shared module has to
# be position-independent, which nothing else here needs.
option(PICO9918_PYTHON_BINDING "Build the Python module" OFF)
if (PICO9918_PYTHON_BINDING)
  set_property(TARGET pico9918_core PROPERTY POSITION_INDEPENDENT_CODE ON)
  add_subdirectory(bindings/python)
endif()

# The Doxyfile is generated so that PROJECT_NUMBER comes from project() above and a
# version bump has one place to happen. Unconditional: the docs are built by a CI job
# that has no reason to configure doxygen itself, and a template is cheap.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

# API docs: `cmake --build <dir> --target docs`. Optional - the target only
# exists when doxygen is installed and this is the top-level project, since a
# host that adds the library as a subdirectory has its own docs target.
if (PROJECT_IS_TOP_LEVEL)
  find_package(Doxygen QUIET)
  if (DOXYGEN_FOUND)
    # Doxygen resolves its paths against the working directory, which is why this runs
    # from the library root rather than from the build tree the Doxyfile sits in.
    add_custom_target(docs
      COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating API documentation into doc/code"
      VERBATIM)
  endif()
endif()

# Source and binary archives over the install tree: `cpack -G TGZ` /  `-G ZIP`
# from the build directory. Top-level only - the firmware packages its own UF2.
if (PROJECT_IS_TOP_LEVEL AND NOT PICO9918_PICO_BUILD)
  set(CPACK_PACKAGE_NAME pico9918-core)
  set(CPACK_PACKAGE_VENDOR visrealm)
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "TMS9918A / F18A video display processor emulation in C")
  set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
  set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
  set(CPACK_GENERATOR TGZ ZIP)
  # The archive carries a compiled library, so it is per-platform, not portable.
  set(CPACK_PACKAGE_FILE_NAME
      "pico9918-core-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  include(CPack)
endif()