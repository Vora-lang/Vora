#pragma once

#include "../runtime/value.h"

namespace vora {

/**
 * @file value_ops.h
 * @brief Pure free functions for Value type operations — arithmetic, comparison,
 *        concatenation, and truthiness.
 *
 * These operations were extracted from the VM execution loop so they can be
 * tested independently (in the C++ unit test suite) and keep the VM `run()`
 * method lean. Every function in this module is stateless and side-effect free;
 * the only output beyond the return value is the `error` out-parameter on
 * `addValues()`.
 *
 * Supported operations:
 * - Truthiness testing (Python-style falsy rules)
 * - Equality comparison with cross-type numeric promotion
 * - Three-way numeric ordering comparison
 * - Addition/concatenation with type coercion (numbers, strings, arrays, dicts)
 */

// =========================================================================
// Value operations — pure functions over the Value type.
// These were extracted from VM so they can be tested independently and
// keep the VM execution loop lean.
// =========================================================================

/**
 * @brief Determine whether a Value is truthy using Python-style rules.
 *
 * Truthiness rules: `null`, `false`, `0`, `0.0`, `""`, `[]`, `{}` are
 * falsy. Everything else (including non-empty strings, non-empty
 * collections, non-zero numbers, objects, functions, and class definitions)
 * is truthy.
 *
 * Used by `OP_JUMP_IF_FALSE`, `OP_JUMP_IF_TRUE`, logical `and`/`or`
 * operators, and the condition expressions of `if`, `while`, and `for`
 * statements.
 *
 * @param value The Value to evaluate.
 * @return `true` if the value is truthy, `false` otherwise.
 */
bool isTruthy(const Value& value);

/**
 * @brief Compare two Values for structural equality with cross-type numeric
 *        promotion.
 *
 * Numeric promotion: an `int64_t` and a `double` compare equal if their
 * mathematical values match (e.g., `42 == 42.0`). All other type pairs
 * compare equal only when both sides hold the same variant alternative
 * with the same content. Strings, arrays, and dicts are compared
 * element-by-element.
 *
 * Used by the `==` and `!=` operators, `assert()`, and dictionary key
 * lookups.
 *
 * @param a The left-hand Value operand.
 * @param b The right-hand Value operand.
 * @return `true` if the values are structurally equal, `false` otherwise.
 */
bool valuesEqual(const Value& a, const Value& b);

/**
 * @brief Perform a three-way numeric comparison of two Values.
 *
 * Returns -1, 0, or 1 like a spaceship operator. Non-numeric values (strings,
 * arrays, dicts, objects, null, bool) compare as equal (return 0). Numeric
 * types (`int64_t` and `double`) are promoted so that mixed comparisons
 * (e.g., `3 < 3.14`) work correctly.
 *
 * Used by the `<`, `<=`, `>`, `>=` relational operators.
 *
 * @param a The left-hand Value operand.
 * @param b The right-hand Value operand.
 * @return `-1` if @p a is less than @p b, `0` if equal, `1` if greater.
 */
int valuesCompare(const Value& a, const Value& b);

/**
 * @brief Perform addition (or concatenation) with type coercion.
 *
 * Behaves differently depending on the operand types:
 * - **number + number**: arithmetic addition (int or double).
 * - **string + any / any + string**: string concatenation via `toString()`.
 * - **array + array**: shallow-copies both arrays into a new combined array.
 * - **dict + dict**: shallow-merges @p b into a copy of @p a (keys in @p b
 *   overwrite keys in @p a).
 *
 * Unsupported type combinations set `error` to `true` and return a null
 * Value. The caller (typically `OP_ADD` in the VM loop) must check `error`
 * before using the result.
 *
 * Used by the `+` operator.
 *
 * @param a The left-hand Value operand.
 * @param b The right-hand Value operand.
 * @param error [out] Set to `true` when the operand types are incompatible for
 *              the requested operation; set to `false` on success.
 * @return The result Value on success, or a null Value when `error` is `true`.
 */
Value addValues(const Value& a, const Value& b, bool& error);

} // namespace vora
