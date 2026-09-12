cmake_minimum_required(VERSION 3.12)

# Detect whether we're building for a Pico target
if(DEFINED PICO_BUILD OR DEFINED PICO_PLATFORM OR DEFINED PICO_SDK_PATH)
  set(PICO9918_PICO_BUILD ON)
else()
  set(PICO9918_PICO_BUILD OFF)
endif()

# Determine the assembly suffix for the TMS9900 core
if(PICO_PLATFORM STREQUAL "rp2040")
  set(PICO9918_ASM_SUFFIX "_m0")
else()
  set(PICO9918_ASM_SUFFIX "_m33")
endif()

# pico9918-core always carries the enhanced renderer and GPU. For a small,
# base-chip-only emulator, use vrEmuTms9918; a host that wants one library for the
# whole family selects the personality at runtime.
set(PICO9918_SOURCES pico9918.c pico9918_util.c pico9918_config.c pico9918_palette.c
                   pico9918_frame.c
                   overlay/splash.c overlay/diag.c
                   gpu/gpu.c)

if(PICO9918_PICO_BUILD AND NOT PICO9918_GPU_C_CORE)
  # Pico builds use the hand-written assembly core.
  list(APPEND PICO9918_SOURCES
    gpu/platform/thumb9900${PICO9918_ASM_SUFFIX}.S)
else()
  # Desktop builds (and Pico builds with PICO9918_GPU_C_CORE) use the C core. Only it has
  # the address decode the F18A personality wants; every other one sees the same flat 64KB.
  list(APPEND PICO9918_SOURCES
    gpu/tms9900.c)
endif()

# V9938 base scaffold - an additive capability. Default OFF.
if(PICO9918_V9938_BASE)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gpu/v9938cmd.c)
    list(APPEND PICO9918_SOURCES gpu/v9938cmd.c)
  endif()
endif()

# Adds entry points and nothing else - no instance field, no layout change, no calling
# convention - so a consumer that disagrees gets a link error rather than corruption. The
# stamp goes in the generated header anyway, because "is this build debuggable" is a
# question a host wants answered at compile time rather than by a failed link.
if(PICO9918_DEBUG_API)
  set(PICO9918_BUILD_DEBUG_API 1)
  list(APPEND PICO9918_SOURCES pico9918_debug.c)
else()
  set(PICO9918_BUILD_DEBUG_API 0)
endif()

# Desktop builds follow BUILD_SHARED_LIBS. A Pico executable has no dynamic loader, so
# its in-firmware archive stays static even if a parent project happens to set the
# standard option globally.
if(PICO9918_PICO_BUILD)
  add_library(${LIBRARY} STATIC ${PICO9918_SOURCES})
else()
  add_library(${LIBRARY} ${PICO9918_SOURCES})
endif()

# The GPU's C core mirrors the assembly core's opcode-group structure, helpers
# the interpreter never reaches included, so it keeps its unused ones.
if(PICO9918_WERROR AND NOT MSVC)
  set_source_files_properties(gpu/tms9900.c PROPERTIES COMPILE_OPTIONS -Wno-unused-function)
endif()

# The name an installed consumer links, available in-tree under the same spelling
# so vendored and installed builds are written the same way.
add_library(pico9918::core ALIAS ${LIBRARY})
set_target_properties(${LIBRARY} PROPERTIES EXPORT_NAME core)

# Linkage mode. A static Windows consumer must inherit PICO9918_STATIC or the header
# declares every entry point dllimport. A shared Windows library instead needs the
# private export spelling while it compiles; its consumers deliberately inherit
# neither definition and therefore get dllimport. Other desktop platforms export the
# public symbols without a decoration, and a Pico build is always the static arm.
if(BUILD_SHARED_LIBS AND NOT PICO9918_PICO_BUILD)
  if(WIN32)
    target_compile_definitions(${LIBRARY} PRIVATE PICO9918_COMPILING_DLL)
  endif()
  set_target_properties(${LIBRARY} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
else()
  target_compile_definitions(${LIBRARY} PUBLIC PICO9918_STATIC)
endif()

# Both install paths: `include/pico9918` so a consumer's existing
# `#include "pico9918.h"` and `#include "impl/pico9918_priv.h"` resolve against
# an installed tree unchanged, and `include` so new code can write
# `#include <pico9918/pico9918.h>`.
target_include_directories(${LIBRARY} INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
  $<INSTALL_INTERFACE:include/pico9918>)
target_include_directories(${LIBRARY} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/gpu
  ${CMAKE_CURRENT_SOURCE_DIR}/platform)

# Overlay image assets. The generated arrays are compiled into the library; the
# generated headers declare them. Both assets are consumed only from inside the
# library, so no consumer needs to see the generated overlay headers.
include(${CMAKE_CURRENT_LIST_DIR}/../pico9918_image_assets.cmake)

if(NOT PICO9918_NO_SPLASH)
  pico9918_generate_image_source_ram(${LIBRARY} bmp_splash overlay/${PICO9918_SPLASH_IMAGE} splash)
else()
  # PUBLIC: the guard elides the render call's body in the library TU and must
  # reach any consumer that includes overlay/splash.h.
  target_compile_definitions(${LIBRARY} PUBLIC PICO9918_NO_SPLASH=1)
endif()

pico9918_generate_image_source_ram(${LIBRARY} bmp_font overlay/res/font.png)

# INTERFACE on the binary dir: the generated pico9918_build_config.h is included
# by the public overlay headers, so consumers must be able to find it. (The
# generated overlay/bmp_*.h are also here, but nothing outside the library
# includes them any more.)
target_include_directories(${LIBRARY} INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

# PUBLIC because the flag gates `pico9918_gpu_frame_count` itself on the impl
# surface, which the firmware's reset handler and the golden harness both
# reference - a consumer compiled with a different value would see a different
# set of symbols.
if(PICO9918_DIAG_GPU_FRAME_COUNTER)
  target_compile_definitions(${LIBRARY} PUBLIC PICO9918_DIAG_GPU_FRAME_COUNTER=1)
endif()

# Stamp the pixel width the assets were built with, PUBLIC so consumers inherit
# it and the generated headers' _Static_assert can catch a policy mismatch. The
# arrays live inside the library while the declarations are included by consumers,
# and a width disagreement is otherwise silent (wrong element size and stride, no
# link error). One width ships, on both platforms - see the note on
# PICO9918_PIXEL_T in platform/desktop/platform_std.h - so this is what a host
# overriding the policy is held to.
set(PICO9918_ASSET_PIXEL_SIZE 2)

if(PICO9918_TEXT80_8BPP)
  set(PICO9918_BUILD_TEXT80_8BPP 1)
else()
  set(PICO9918_BUILD_TEXT80_8BPP 0)
endif()

# It adds instance fields and two entry points, so it belongs in the generated header
# with the other choices a consumer cannot be allowed to disagree with. Nothing else
# carries it - a compile definition would reach the library's TUs and not the
# consumer's, which is the mismatch that header exists to prevent.
if(PICO9918_RUNTIME_CHIP)
  set(PICO9918_BUILD_RUNTIME_CHIP 1)

  # The F18A personality's power-on badge artwork, generated only here so a board
  # never carries this non-const array. Deliberately not gated on
  # PICO9918_NO_SPLASH: that option drops the PICO9918's own splash, and an F18A
  # still shows its badge.
  pico9918_generate_image_source_ram(${LIBRARY} bmp_f18a_badge overlay/res/f18a_badge.png f18aBadge)

  # The PRO tier's splash, for the same reason: a board resolves PICO9918_SPLASH_IMAGE to
  # the one artwork it ships, and only a build that can switch personality needs both.
  if(NOT PICO9918_NO_SPLASH)
    pico9918_generate_image_source_ram(${LIBRARY} bmp_splash_pro overlay/res/splash_pro.png splashPro)
  endif()
else()
  set(PICO9918_BUILD_RUNTIME_CHIP 0)
endif()

# The version a settings block is stamped with, and what its stored version is compared
# against. Checked here rather than asserted in C: these are CMake variables, and a packed
# byte cannot show the overlap anyway - 1.16 packs exactly as 1.0 does.
if(PROJECT_VERSION_MAJOR GREATER 15 OR PROJECT_VERSION_MINOR GREATER 15
   OR PROJECT_VERSION_PATCH GREATER 255)
  message(FATAL_ERROR
    "pico9918-core ${PROJECT_VERSION} does not fit the stored identity: PICO9918_CONF_SW_VERSION "
    "is one nibble each for major and minor, PICO9918_CONF_SW_PATCH_VERSION one byte. Widening "
    "them is a config-block ABI migration, not a version bump.")
endif()
math(EXPR PICO9918_BUILD_SW_VERSION "(${PROJECT_VERSION_MAJOR} << 4) | ${PROJECT_VERSION_MINOR}"
     OUTPUT_FORMAT HEXADECIMAL)
set(PICO9918_BUILD_SW_PATCH ${PROJECT_VERSION_PATCH})

# The most dangerous of the lot: it changes the calling convention of nearly every
# public entry point, and C symbols carry no argument types, so a consumer that
# disagrees links cleanly and then passes arguments the library never expects.
if(PICO9918_SINGLE_INSTANCE)
  set(PICO9918_BUILD_SINGLE_INSTANCE 1)
else()
  set(PICO9918_BUILD_SINGLE_INSTANCE 0)
endif()

# Also record these in a GENERATED HEADER, not only as compile definitions. A
# definition reaches the library's own TUs and nothing else, so a consumer built
# by a different build system silently falls back to the platform default and the
# mismatch is undetectable - a clean compile and clean link, then corrupt pixels
# The header travels with the sources, so any consumer that can include a public
# header can see the real width.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/pico9918_build_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/pico9918_build_config.h
  @ONLY)

target_compile_definitions(${LIBRARY} PUBLIC
  PICO9918_ASSET_PIXEL_SIZE=${PICO9918_ASSET_PIXEL_SIZE})

# PUBLIC, not just this project's CMAKE_C_STANDARD: the public headers assert their
# pixel width with _Static_assert, so a consumer compiling as C99 has to be raised
# too. MSVC is where it bites - it takes no /std flag below c11 and rejects the
# keyword outright, while GCC and Clang accept it as an extension and hide the gap.
target_compile_features(${LIBRARY} PUBLIC c_std_11)

# PUBLIC for the same reason as PICO9918_SINGLE_INSTANCE below: it changes a size
# pico9918.h publishes.
if(PICO9918_TEXT80_8BPP)
  target_compile_definitions(${LIBRARY} PUBLIC PICO9918_TEXT80_8BPP=1)
endif()

# Single-instance mode changes the public API signatures (pico9918.h). The generated
# header is what a consumer actually derives it from, so this definition is only for
# the library's own translation units and for a consumer that states it explicitly -
# pico9918.h holds such a one to agreeing with the stamp.
if(PICO9918_SINGLE_INSTANCE)
  target_compile_definitions(${LIBRARY} PUBLIC PICO9918_SINGLE_INSTANCE=1)
endif()

if(PICO9918_V9938_BASE)
  # PUBLIC: the flag gates public-header enum entries (TMS_MODE_COUNT)
  target_compile_definitions(${LIBRARY} PUBLIC PICO9918_V9938_BASE)
endif()

if(PICO9918_GPU_C_CORE)
  target_compile_definitions(${LIBRARY} PRIVATE PICO9918_GPU_C_CORE)
endif()

# Only link Pico-specific libraries when building for a Pico target
if(PICO9918_PICO_BUILD)
  # hardware_pio / hardware_irq: a host ops header may expand PIO and IRQ calls
  # into library TUs (the PICO9918's tier-1 critical section and read-ahead push).
  # Both are header-inline in the SDK, so this buys declarations and include paths,
  # not extra code.
  # hardware_interp: the RP2040 scanline expansion drives the two interpolators.
  # pico_divider: the F18A timing counters' divmod.
  target_link_libraries(${LIBRARY} PRIVATE pico_stdlib hardware_dma hardware_pio hardware_irq
                                           hardware_interp pico_divider)
  target_link_libraries(${LIBRARY} PRIVATE hardware_flash)
endif()


# The install tree, and the CMake package a consumer finds:
#
#   find_package(pico9918_core CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE pico9918::core)
#
# Top-level only. A host that vendors the library owns its own install rules, and
# a Pico build's archive carries SDK targets that are not in this export set, so
# generating one would fail rather than produce something useful.
if(PROJECT_IS_TOP_LEVEL AND NOT PICO9918_PICO_BUILD)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS ${LIBRARY} EXPORT pico9918_coreTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  # The whole header tree ships. impl/, platform/, gpu/ and overlay/ are all
  # reached by relative include from a public header, so which headers are API is
  # documented rather than enforced by omission - leaving impl/ out would only
  # mean the library does not compile.
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pico9918
          FILES_MATCHING PATTERN "*.h")
  # generated, and included by the public overlay headers
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pico9918_build_config.h
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pico9918)

  install(EXPORT pico9918_coreTargets
          NAMESPACE pico9918::
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pico9918_core)

  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/pico9918_coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/pico9918_coreConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pico9918_core)

  # SameMinorVersion, not SameMajorVersion: the pixel policy, the instance mode
  # and the config-byte layout are all baked into the installed headers, so a
  # patch release is the most a consumer can be promised.
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/pico9918_coreConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)

  install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/pico9918_coreConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/pico9918_coreConfigVersion.cmake
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pico9918_core)
endif()
