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
///   - 数据类型:  Value, GcString, GcBigInt, Array, Dict, GcHeap
///   - 错误处理:  ErrorReporter, StderrErrorReporter, RuntimeError
///
///
/// ## 整数与任意精度（嵌入方契约，v1.0 冻结）
///
/// Vora 的整数是**混合表示**：能放进 NaN-boxing 46 位载荷的值（±2^45 以内）
/// 是内联 Int；更大的值是堆上的 GcBigInt。对嵌入方而言，这两者都是**整数**，
/// 区别只在表示，不在语义。取值请按以下顺序：
///
/// @code
///   vora::Value v = vm.getGlobal("x");
///
///   if (v.isInt() || v.isBigInt()) {          // 1. 先判断是不是整数
///       int64_t n;
///       if (v.toInt64Exact(n)) {              // 2. 放得下 int64 → 直接取
///           useSmall(n);
///       } else {                              // 3. 放不下 → 走十进制字符串
///           std::string digits = vora::valueToString(v);
///           useArbitraryPrecision(digits);
///       }
///   }
/// @endcode
///
/// 要点：
///   - **判类型用 `isBigInt()`**，取「是不是整数」用 `isInt() || isBigInt()`；
///     只测 `isInt()` 会把大整数当成非整数。
///   - **`fitsInt64()` / `toInt64Exact(int64_t&)`** 是唯一无损的 int64 取用方式。
///   - **`asInt()` 对 GcBigInt 不会返回截断值**：Debug 下断言失败，Release 下抛
///     `RuntimeError`。这是刻意的——静默截断正是本特性要消灭的那类错误。
///     `asInt()` 只应作用于已确认 `isInt()` 的值。
///   - **`valueToString()`** 对 GcBigInt 给出无损十进制表示，是任意精度的通用
///     出口（往返经 `jsonParse` 亦可，见下）。
///   - **`toDouble()` / `asDouble()` 会丢精度**，仅用于确知数值量级安全的场合。
///   - **数值比较不要用 `asDouble()` 比较**：请用 `numericValuesCompare()` /
///     `numericValuesEqual()`，它们按精确值比较，不经过 double 舍入。
///   - **不暴露 limb 布局**：GcBigInt 内部表示（当前为符号 + 2^64 limb 数组）不
///     属于 ABI，可以随时更换；请只依赖上述接口。
///
/// 数值范围：超大字面量在超过 `kMaxBigIntLimbs`（4096 limb ≈ 78,900 位十进制）
/// 时报编译期错误，而不是静默近似。JSON 序列化时，放得进 int64 的整数输出为
/// JSON 数字，放不下的输出为十进制字符串（JSON 数字承载不了任意精度）。
///

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
