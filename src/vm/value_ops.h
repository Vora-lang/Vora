#pragma once

#include "../runtime/value.h"

namespace vora {

// =========================================================================
// Value operations — pure functions over the Value type.
// These were extracted from VM so they can be tested independently and
// keep the VM execution loop lean.
// =========================================================================

// Truthiness: null, false, 0, 0.0, "" are falsy; everything else is truthy.
bool isTruthy(const Value& value);

// Equality with cross-type numeric promotion (42 == 42.0).
bool valuesEqual(const Value& a, const Value& b);

// Numeric comparison: returns -1, 0, or 1. Non-numeric values compare as 0.
int valuesCompare(const Value& a, const Value& b);

// Addition with type coercion (numbers, strings, arrays, dicts).
// Sets `error` to true and returns nullptr for unsupported operand types.
Value addValues(const Value& a, const Value& b, bool& error);

} // namespace vora
