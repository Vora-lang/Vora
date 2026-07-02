/// @file vora.h
/// @brief Vora 语言的单头文件 C++ 嵌入 API。
///
/// 在你的项目中只需要:
///
///   #include "vora.h"
///
/// 然后链接 vora_lib.lib (或 libvora_lib.a)。
///
/// 提供的 API:
///   - 编译管线:  Lexer → Parser → Compiler → Chunk
///   - 运行时:    VM, defineNative(), registerBuiltins()
///   - 数据类型:  Value, GcString, Array, Dict, GcHeap
///   - 错误处理:  ErrorReporter, StderrErrorReporter, RuntimeError

#pragma once

// ── 基础类型 & GC ────────────────────────────────────────────────────────
#include "gc/gc_object.h"
#include "gc/gc_ptr.h"
#include "gc/gc_heap.h"
#include "runtime/value.h"

// ── 错误报告 ──────────────────────────────────────────────────────────────
#include "common/error_reporter.h"

// ── 编译管线 ──────────────────────────────────────────────────────────────
#include "lexer/token.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "vm/opcode.h"
#include "vm/chunk.h"
#include "vm/compiler.h"

// ── 运行时 ────────────────────────────────────────────────────────────────
#include "runtime/callable.h"
#include "runtime/native_function.h"
#include "runtime/vora_function.h"
#include "runtime/class_constructor.h"
#include "runtime/bound_method.h"
#include "runtime/environment.h"
#include "runtime/builtins.h"
#include "runtime/runtime_error.h"
#include "vm/vm.h"
