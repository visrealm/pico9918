# pico9918_common.cmake - shared Python, version, and artifact-suffix helpers.
#
# Inputs (set BEFORE include): PICO9918_{MAJOR,MINOR,PATCH}_VER and optional
# PICO9918_VERSION_SUFFIX (overrides git branch in artifact names).
# Outputs: PYTHON, PICO9918_GIT_BRANCH, PICO9918_VERSION, PICO9918_VERSION_STR,
# PICO9918_BRANCH_STR, function pico9918_compute_binary_suffix().

# Resolve Python to a real interpreter. On Windows, bare "python3" on PATH is
# usually the Store App Execution Alias, which fails from console-less child
# processes (the .pyw GUI builder hits this). We also need to honor whichever
# Python is first on PATH so we match the interpreter that `pip install` used
# (CI installs Pillow into PATH-python; find_package's newest-wins search can
# otherwise pick a different install that lacks PIL).
find_program(_pico9918_python NAMES python3 python)
if(_pico9918_python)
  set(Python3_EXECUTABLE "${_pico9918_python}" CACHE FILEPATH "" FORCE)
endif()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  set(PYTHON ${Python3_EXECUTABLE})
else()
  set(PYTHON python3)
endif()

set(PICO9918_VERSION "${PICO9918_MAJOR_VER}.${PICO9918_MINOR_VER}.${PICO9918_PATCH_VER}")
string(REPLACE "." "-" PICO9918_VERSION_STR "${PICO9918_VERSION}")

execute_process(
  COMMAND git symbolic-ref --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE PICO9918_GIT_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

# Explicit suffix overrides branch; "main" produces no suffix.
set(PICO9918_BRANCH_STR "")
if (NOT "${PICO9918_VERSION_SUFFIX}" STREQUAL "")
  set(PICO9918_BRANCH_STR "-${PICO9918_VERSION_SUFFIX}")
elseif (NOT "${PICO9918_GIT_BRANCH}" STREQUAL "main" AND NOT "${PICO9918_GIT_BRANCH}" STREQUAL "")
  string(REPLACE "/" "-" PICO9918_GIT_BRANCH_SAFE "${PICO9918_GIT_BRANCH}")
  set(PICO9918_BRANCH_STR "-${PICO9918_GIT_BRANCH_SAFE}")
endif()

# Artifact suffix: ${board_part}${output_str}-v${version}${branch}[-diag]
# board_part is "pro" / "" / "-${board}" depending on board.
function(pico9918_compute_binary_suffix board output_str diag outvar)
  if (board STREQUAL "pico9918pro")
    set(board_part "pro")
  elseif (NOT board STREQUAL "pico9918")
    set(board_part "-${board}")
  else()
    set(board_part "")
  endif()

  set(suffix "${board_part}${output_str}-v${PICO9918_VERSION_STR}${PICO9918_BRANCH_STR}")
  if (diag)
    set(suffix "${suffix}-diag")
  endif()
  set(${outvar} "${suffix}" PARENT_SCOPE)
endfunction()
