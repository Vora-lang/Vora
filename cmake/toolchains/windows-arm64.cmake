# Toolchain for building ARM64 Windows binaries (MSVC)
#
# Requires: Visual Studio Build Tools with ARM64 component:
#   VS Installer → "Individual components" → "MSVC v143 - VS 2022 C++ ARM64 build tools"
#
# Usage:
#   cmake --preset windows-arm64-debug
#   cmake --build --preset windows-arm64-debug
#
# Or with a custom generator:
#   cmake -S . -B build/windows-arm64 -G Ninja -DCMAKE_GENERATOR_PLATFORM=ARM64

if(NOT DEFINED CMAKE_GENERATOR_PLATFORM)
    set(CMAKE_GENERATOR_PLATFORM ARM64 CACHE STRING "MSVC target platform")
endif()

message(STATUS "windows-arm64: CMAKE_GENERATOR_PLATFORM = ${CMAKE_GENERATOR_PLATFORM}")
