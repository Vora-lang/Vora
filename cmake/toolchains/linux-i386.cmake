# Toolchain for cross-compiling to 32-bit x86 Linux (i386)
# Requires: gcc-multilib g++-multilib (or: gcc-i686-linux-gnu g++-i686-linux-gnu)
#
# Usage:
#   cmake --preset linux-x86-debug
#   cmake --build --preset linux-x86-debug

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i386)

# Prefer the i686-linux-gnu cross-compiler if available; fall back to host gcc -m32
find_program(CROSS_GCC i686-linux-gnu-gcc)
find_program(CROSS_GXX i686-linux-gnu-g++)

if(CROSS_GCC AND CROSS_GXX)
    set(CMAKE_C_COMPILER   "${CROSS_GCC}"   CACHE FILEPATH "C compiler")
    set(CMAKE_CXX_COMPILER "${CROSS_GXX}"   CACHE FILEPATH "C++ compiler")
else()
    # Use host compiler with -m32 flag
    set(CMAKE_C_COMPILER   gcc   CACHE FILEPATH "C compiler")
    set(CMAKE_CXX_COMPILER g++   CACHE FILEPATH "C++ compiler")
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -m32" CACHE STRING "C flags")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32" CACHE STRING "C++ flags")
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS}    -m32" CACHE STRING "Linker flags")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32" CACHE STRING "Shared linker flags")
endif()

# Only search the host system (no custom sysroot for native multilib)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
