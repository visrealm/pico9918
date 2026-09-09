# The live harness's desktop backend (no Pico SDK).
#
#   cmake -S test/suite/shim -B build-live-desktop -G Ninja -DCMAKE_C_FLAGS=-O2
#   cmake --build build-live-desktop
#
# -O2 or higher is required, for the reason test/golden records: the impl headers
# are static-inline and an unoptimized build emits calls it never defines.
#
# Two tiers, because the goldens are keyed on the line width and a board only ever
# writes its own: the default is the 256-byte line every mode renders, and
# -DLIVE_DESKTOP_TEXT80_8BPP=ON is the PRO's 512-byte 80-column tier, which
# reproduces ../oracle/reference-w512.
cmake_minimum_required(VERSION 3.13)
project(pico9918_live_desktop C)

set(CMAKE_C_STANDARD 99)

option(LIVE_DESKTOP_TEXT80_8BPP "Build the 8bpp 80-column tier (the PRO's)" OFF)

# The ops header is how a host reaches the library's capture hook, and the hook
# EXPANDS INSIDE THE LIBRARY - pico9918_frame.c is where PICO9918_LINE_CAPTURE
# appears. So this is directory-scoped and set before the library is added, which is
# how the firmware's root CMakeLists supplies pico9918HostOps.h for the same reason.
# Put it on the shim target alone and the library compiles the no-op default: the
# build succeeds, the capture is silently empty, and every scene reads as a
# regression with zero rows.
add_definitions(-DPICO9918_HOST_OPS_HEADER="liveDesktopOps.h")
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# The library now defaults wide for emulator writers; this harness still makes both
# tiers explicit because each is compared with a different reference directory.
set(PICO9918_TEXT80_8BPP ${LIVE_DESKTOP_TEXT80_8BPP} CACHE BOOL "" FORCE)

# The library is the directory three levels up, reached by relative path rather
# than by an install. The suite uses the firmware's single-instance calling convention.
set(PICO9918_SINGLE_INSTANCE 1 CACHE STRING "" FORCE)
# every scene is a byte compare against a committed frame, so the shim is in the
# same position as the golden harness: no -march=native
set(PICO9918_PORTABLE_CODEGEN ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../.. pico9918_core)

add_executable(live_shim shim.c)

# A GPU program runs on a thread of its own so frames can be rendered while it
# draws, which is the shape the device has: the program on one core, the renderer on
# the other. Win32 needs nothing linked; everywhere else this is pthreads.
find_package(Threads REQUIRED)
target_link_libraries(live_shim PRIVATE pico9918_core Threads::Threads)

if (MSVC)
  # sscanf, parsing the protocol lines
  target_compile_definitions(live_shim PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_include_directories(live_shim PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
  ${CMAKE_BINARY_DIR}/pico9918_core/src)
