cmake_minimum_required(VERSION 3.12)


set(IMG_CONV ${CMAKE_SOURCE_DIR}/tools/img2carray.py)
set(BIN_CONV ${CMAKE_SOURCE_DIR}/tools/bin2carray.py)
# ${PYTHON} comes from pico9918_common.cmake.

# The tools take wildcards, so a source argument may be a glob. Ninja needs real
# files in DEPENDS, and a directory there would not notice an edited image.
function(visrealm_resolve_sources OUTVAR)
  set(resolved)
  foreach(pattern ${ARGN})
    file(GLOB matched CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${pattern})
    if(matched)
      list(APPEND resolved ${matched})
    else()
      list(APPEND resolved ${CMAKE_CURRENT_SOURCE_DIR}/${pattern})
    endif()
  endforeach()
  set(${OUTVAR} ${resolved} PARENT_SCOPE)
endfunction()

# custom function to generate source code from images using tools/img2carray.py
function(visrealm_generate_image_source TARGET DST ROMSRC)
  set (RAMSRCARG)
  set (extra_args ${ARGN})
  list(LENGTH extra_args extra_count)
  if (${extra_count} GREATER 0)
    list(GET extra_args 0 RAMSRC)
    set(RAMSRCARG -r ${CMAKE_CURRENT_SOURCE_DIR}/${RAMSRC})
  endif()
  visrealm_resolve_sources(fullSrc ${ROMSRC} ${RAMSRC})
  add_custom_command(
      OUTPUT ${DST}.c ${DST}.h
      COMMAND ${PYTHON} ${IMG_CONV} -i ${CMAKE_CURRENT_SOURCE_DIR}/${ROMSRC} ${RAMSRCARG} -o ${DST}.c
      DEPENDS ${IMG_CONV} ${fullSrc}
  )
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${DST}.c)
endfunction()

# custom function to generate source code from images using tools/img2carray.py
function(visrealm_generate_image_source_ram TARGET DST RAMSRC)
  visrealm_resolve_sources(fullSrc ${RAMSRC})
  add_custom_command(
      OUTPUT ${DST}.c ${DST}.h
      COMMAND ${PYTHON} ${IMG_CONV} -r ${CMAKE_CURRENT_SOURCE_DIR}/${RAMSRC} -o ${DST}.c
      DEPENDS ${IMG_CONV} ${fullSrc}
  )
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${DST}.c)
endfunction()




# custom function to generate source code from images using tools/img2carray.py
function(visrealm_generate_bindata_source TARGET DST SRC)
  visrealm_resolve_sources(fullSrc ${SRC})
  add_custom_command(
      OUTPUT ${DST}.c ${DST}.h
      COMMAND ${PYTHON} ${BIN_CONV} -i ${CMAKE_CURRENT_SOURCE_DIR}/${SRC} -o ${DST}.c
      DEPENDS ${BIN_CONV} ${fullSrc}
  )
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${DST}.c)
endfunction()

