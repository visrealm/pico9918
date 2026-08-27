# pico9918_image_assets.cmake - generate overlay image assets from PNGs.
#
# Vendored from the pico9918 firmware's visrealm_tools.cmake so the library
# builds standalone: nothing here references the firmware's tools/ directory.
# The generator emits PICO9918_PIXEL_FROM_RGB12(...) initializers, so a generated
# asset never carries a host pixel format.
#
# Only the RAM variant is provided - the overlays are the only consumers and
# they all need RAM-resident data. (The firmware's ROM and bindata variants stay
# in the firmware, where their callers are.)
#
# Requires Python 3 with pillow. Resolves the interpreter itself rather than
# inheriting the host's ${PYTHON}, so the library configures standalone.
#
# pillow is checked HERE, at configure time. Finding only the interpreter is not
# enough: a runner or a fresh consumer machine usually has Python 3 but not
# pillow, so `find_package(... REQUIRED)` alone passes configure and the build
# then dies on an "No module named 'PIL'" traceback from inside a codegen step -
# a confusing place to learn about a missing build dependency.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

execute_process(
  COMMAND ${Python3_EXECUTABLE} -c "import PIL"
  RESULT_VARIABLE PICO9918_PILLOW_MISSING
  OUTPUT_QUIET ERROR_QUIET)

if(PICO9918_PILLOW_MISSING)
  message(FATAL_ERROR
    "The overlay image assets need Python 3 with pillow, and pillow was not "
    "found for ${Python3_EXECUTABLE}.\n"
    "  Install it with:  ${Python3_EXECUTABLE} -m pip install pillow")
endif()

set(PICO9918_IMG_CONV ${CMAKE_CURRENT_LIST_DIR}/tools/img2carray.py)

# pico9918_generate_image_source_ram(TARGET DST SRC [SYMBOL])
#
#   TARGET  target that compiles the generated .c
#   DST     output base name (DST.c / DST.h in the binary dir)
#   SRC     PNG path, relative to the calling CMakeLists' source dir
#   SYMBOL  optional array base name, overriding the one derived from the file
#           name - so a board-conditional asset (splash_pro.png) still produces
#           the canonical symbols and the consuming TU needs no #ifdef aliases
#
# Output lands in an `overlay/` subdirectory of the binary dir and is included as
# `overlay/<DST>.h`, NEVER by bare name. That is deliberate: a consumer's own
# build dir is on its include path (the Pico SDK's pico_generate_pio_header adds
# ${CMAKE_CURRENT_BINARY_DIR} unconditionally) and precedes ours, so a bare
# `bmp_splash.h` silently resolves to any same-named leftover sitting there,
# which can declare a different type or even a different image width. Qualifying
# the path removes the ambiguity structurally rather than relying on directory
# hygiene.
function(pico9918_generate_image_source_ram TARGET DST SRC)
  set(fullSrc ${CMAKE_CURRENT_SOURCE_DIR}/${SRC})

  set(symbolArg)
  if(ARGC GREATER 3)
    set(symbolArg -s ${ARGV3})
  endif()

  add_custom_command(
      OUTPUT overlay/${DST}.c overlay/${DST}.h
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/overlay
      COMMAND ${Python3_EXECUTABLE} ${PICO9918_IMG_CONV} -r ${fullSrc} ${symbolArg}
              -o ${CMAKE_CURRENT_BINARY_DIR}/overlay/${DST}.c
      DEPENDS ${PICO9918_IMG_CONV} ${fullSrc}
      COMMENT "Generating overlay/${DST}.c from ${SRC}"
  )
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/overlay/${DST}.c)
endfunction()
