# Toolchain for building 32-bit x86 Windows binaries (MSVC)
#
# This is a thin wrapper — MSVC architecture selection is controlled by
# CMAKE_GENERATOR_PLATFORM.  When used with a CMakePreset that sets
# "architecture": "Win32", this file is not strictly needed, but it
# provides a fallback for direct cmake invocations.
#
# Usage:
#   cmake --preset windows-x86-debug
#   cmake --build --preset windows-x86-debug
#
# Or with a custom generator:
#   cmake -S . -B build/windows-x86 -G Ninja -DCMAKE_GENERATOR_PLATFORM=Win32

if(NOT DEFINED CMAKE_GENERATOR_PLATFORM)
    set(CMAKE_GENERATOR_PLATFORM Win32 CACHE STRING "MSVC target platform (Win32 = 32-bit x86)")
endif()

# When cross-compiling from x64 to x86 on Windows with Ninja, ensure
# the correct MSVC environment is loaded (e.g., x86 Native Tools Command Prompt).
# The generator platform tells CMake which MSVC toolchain to use.
message(STATUS "windows-i386: CMAKE_GENERATOR_PLATFORM = ${CMAKE_GENERATOR_PLATFORM}")
