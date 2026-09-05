# examples/embed/CMakeLists.txt
# ============================================================================
# Vora C++ 嵌入示例 —— 统一构建，支持树内 + 独立两种模式
#
# 树内模式 (VORA_EMBED_DEMO=ON):
#   cmake -B build -DVORA_EMBED_DEMO=ON
#   cmake --build build --target embed_demo
#
# 独立模式 A (源码树):
#   cmake -B build -S . -DVORA_ROOT=<vora-repo>
#
# 独立模式 B (已安装 SDK):
#   cmake -B build -S . -DVORA_SDK=<install-prefix>
# ============================================================================
cmake_minimum_required(VERSION 3.16)

# ── 如果作为子目录（树内模式），沿用父项目 target ─────────────────────
if(TARGET vora_lib)
    # 树内模式：vora_lib 和 vora_hpp 已由父 CMakeLists.txt 定义
    set(VORA_LIB_TARGET vora_lib)

    if(TARGET vora_hpp)
        set(VORA_HPP_DEP vora_hpp)
    endif()

    # 定位 vora.hpp（由 vora_hpp target 生成在 build 目录）
    if(EXISTS "${CMAKE_BINARY_DIR}/vora.hpp")
        set(VORA_INCLUDE_DIR "${CMAKE_BINARY_DIR}")
    else()
        # vora.hpp 尚未生成，使用源码树 include
        get_filename_component(_vora_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
        set(VORA_INCLUDE_DIR "${_vora_root}/src")
        set(VORA_THIRD_PARTY_DIR "${_vora_root}/include")
    endif()
else()
    # ── 独立模式：定位 Vora ──────────────────────────────────────────
    if(DEFINED VORA_SDK AND NOT DEFINED VORA_LIB)
        if(WIN32)
            set(VORA_LIB "${VORA_SDK}/lib/vora_lib.lib")
        else()
            set(VORA_LIB "${VORA_SDK}/lib/libvora_lib.a")
        endif()
        set(VORA_INCLUDE_DIR "${VORA_SDK}/include/vora")
    endif()

    if(DEFINED VORA_ROOT AND NOT DEFINED VORA_LIB)
        foreach(cfg Release Debug)
            if(WIN32)
                set(candidate "${VORA_ROOT}/build/windows-x64-${cfg}/${cfg}/vora_lib.lib")
            else()
                set(candidate "${VORA_ROOT}/build/linux-x64-${cfg}/libvora_lib.a")
            endif()
            if(EXISTS "${candidate}")
                set(VORA_LIB "${candidate}")
                break()
            endif()
        endforeach()
        # 优先用合并后的 vora.hpp
        file(GLOB _hpp_candidates "${VORA_ROOT}/build/*/vora.hpp")
        if(_hpp_candidates)
            list(GET _hpp_candidates 0 _hpp)
            get_filename_component(VORA_INCLUDE_DIR "${_hpp}" DIRECTORY)
        endif()
        if(NOT VORA_INCLUDE_DIR)
            set(VORA_INCLUDE_DIR "${VORA_ROOT}/src")
            set(VORA_THIRD_PARTY_DIR "${VORA_ROOT}/include")
        endif()
    endif()

    if(NOT DEFINED VORA_LIB)
        message(FATAL_ERROR
            "请设置 VORA_ROOT 或 VORA_SDK:\n"
            "  cmake -B build -S . -DVORA_ROOT=<repo>\n"
            "  cmake -B build -S . -DVORA_SDK=<sdk>")
    endif()
endif()

# ── embed_demo 可执行文件 ──────────────────────────────────────────────
add_executable(embed_demo main.cpp)

if(DEFINED VORA_LIB_TARGET)
    target_link_libraries(embed_demo PRIVATE ${VORA_LIB_TARGET})
    if(DEFINED VORA_HPP_DEP)
        add_dependencies(embed_demo ${VORA_HPP_DEP})
    endif()
else()
    target_link_libraries(embed_demo PRIVATE "${VORA_LIB}")
endif()

target_include_directories(embed_demo PRIVATE "${VORA_INCLUDE_DIR}")
if(DEFINED VORA_THIRD_PARTY_DIR)
    target_include_directories(embed_demo PRIVATE "${VORA_THIRD_PARTY_DIR}")
endif()

target_compile_features(embed_demo PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(embed_demo PRIVATE /utf-8)
endif()

message(STATUS "embed_demo: VORA_INCLUDE_DIR = ${VORA_INCLUDE_DIR}")
