# Toolchain for building ARM64 Windows binaries (MSVC)
#
# Requires: Visual Studio Build Tools with ARM64 component:
#   VS Installer → "Individual components" → "MSVC v143 - VS 2022 C++ ARM64 build tools"
#
# Usage (with MSVC generator):
#   cmake -S . -B build -A ARM64
#
# Or with Ninja + MSVC:
#   cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
#       -DCMAKE_GENERATOR_PLATFORM=ARM64

if(NOT DEFINED CMAKE_GENERATOR_PLATFORM)
    set(CMAKE_GENERATOR_PLATFORM ARM64 CACHE STRING "MSVC target platform")
endif()

message(STATUS "windows-arm64: CMAKE_GENERATOR_PLATFORM = ${CMAKE_GENERATOR_PLATFORM}")
