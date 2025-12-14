# Windows MSVC toolchain for Pico SDK builds
# This ensures external projects (pioasm, picotool) use MSVC instead of MinGW

# Force MSVC C/C++ compilers
set(CMAKE_C_COMPILER cl.exe CACHE STRING "C compiler" FORCE)
set(CMAKE_CXX_COMPILER cl.exe CACHE STRING "C++ compiler" FORCE)

# Skip compiler tests that might fail due to environment
set(CMAKE_C_COMPILER_WORKS 1 CACHE BOOL "C compiler works" FORCE)
set(CMAKE_CXX_COMPILER_WORKS 1 CACHE BOOL "C++ compiler works" FORCE)
