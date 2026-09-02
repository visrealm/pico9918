# Golden-frame regression harness (desktop only - no Pico SDK).
#
# Configure the library root with:
#   cmake -B build -DPICO9918_GOLDEN=ON -DPICO9918_MODE=1 \
#         -DPICO9918_SINGLE_INSTANCE=1 -DCMAKE_C_FLAGS=-O2
#
# Note: -O2 (or higher) is required.

# One pixel policy ships, but it is written out twice: platform/pico cannot be
# included off-target (it pulls in hardware/dma.h) and platform/desktop cannot use
# its interpolator arm. The goldens pin what a device produces only as long as the
# two stay identical, so drift is a configure error rather than something a
# reviewer has to notice.
file(READ ${CMAKE_SOURCE_DIR}/src/platform/desktop/platform_std.h PICO9918_STD_POLICY)
file(READ ${CMAKE_SOURCE_DIR}/src/platform/pico/platform_pico.h PICO9918_PICO_POLICY)
# Every formula the goldens pin must appear in both. Type identity is not checkable
# by substring (one guards its typedef with #ifndef) - golden.c static-asserts those.
foreach(snippet
    "((PICO9918_PIXEL_T)(((rgb) & 0xFF0F) | ((((rgb) & 0xFF0F) >> 12) << 4)))"
    "((PICO9918_PIXEL_T)(((p) >> 2) & 0x333))"
    "(((w) >> 1) & 0x07770777u)"
    "_d[7] = _l[_s[7]];")
  string(FIND "${PICO9918_STD_POLICY}" "${snippet}" _inStd)
  string(FIND "${PICO9918_PICO_POLICY}" "${snippet}" _inPico)
  if(_inStd EQUAL -1 OR _inPico EQUAL -1)
    message(FATAL_ERROR
      "the two pixel policies have drifted - not found in both:\n"
      "  ${snippet}\n"
      "Re-sync src/platform/desktop/platform_std.h with "
      "src/platform/pico/platform_pico.h.")
  endif()
endforeach()

add_executable(golden_runner golden.c)
target_link_libraries(golden_runner PRIVATE pico9918_core)

# The post-palette surfaces reach into impl/ for the palette LUT and the expansion
# macro, so the harness must see the same struct layout the library was compiled
# with - PICO9918_MODE gates it.
target_compile_definitions(golden_runner PRIVATE
  GOLDEN_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  PICO9918_MODE=${PICO9918_MODE})
# CMAKE_BINARY_DIR/src: the generated overlay/bmp_font.h and overlay/bmp_splash.h,
# which the overlay surface reads for the asset geometry it static-asserts against
# (see the asset note in golden.c). The target-level dependency that makes those
# headers exist before this TU compiles already comes from target_link_libraries
# above; no add_dependencies is needed.
target_include_directories(golden_runner PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src)
