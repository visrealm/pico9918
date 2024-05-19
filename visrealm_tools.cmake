cmake_minimum_required(VERSION 3.12)


set(IMG_CONV ${CMAKE_SOURCE_DIR}/tools/img2carray.py)
set(BIN_CONV ${CMAKE_SOURCE_DIR}/tools/bin2carray.py)
set(PYTHON python3)

# custom function to generate source code from images using tools/img2carray.py
function(visrealm_generate_image_source TARGET DST ROMSRC)
  set(fullSrc ${CMAKE_CURRENT_SOURCE_DIR}/${ROMSRC})
  cmake_path(GET fullSrc PARENT_PATH srcPath)

  set (RAMSRCARG)
  set (extra_args ${ARGN})
  list(LENGTH extra_args extra_count)
  if (${extra_count} GREATER 0)
    list(GET extra_args 0 RAMSRC)
    set(RAMSRCARG -r ${CMAKE_CURRENT_SOURCE_DIR}/${RAMSRC})
  endif()
  add_custom_command(
      OUTPUT ${DST}.c ${DST}.h
      COMMAND ${PYTHON} ${IMG_CONV} -i ${CMAKE_CURRENT_SOURCE_DIR}/${ROMSRC} ${RAMSRCARG} -o ${DST}.c
      DEPENDS ${IMG_CONV} ${srcPath}
  )
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${DST}.c)
endfunction()



# custom function to generate source code from images using tools/img2carray.py
function(visrealm_generate_bindata_source TARGET DST SRC)
  set(fullSrc ${CMAKE_CURRENT_SOURCE_DIR}/${SRC})
  cmake_path(GET fullSrc PARENT_PATH srcPath)

  add_custom_command(
      OUTPUT ${DST}.c ${DST}.h
      COMMAND ${PYTHON} ${BIN_CONV} -i ${CMAKE_CURRENT_SOURCE_DIR}/${SRC} -o ${DST}.c
      DEPENDS ${BIN_CONV} ${srcPath}
  )
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${DST}.c)
endfunction()
