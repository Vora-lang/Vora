// src/runtime/builtins.h — Unified built-in native functions
//
// All user-facing built-in natives (print, type, len, clock, assert, int,
// float, range, input, bin, oct, hex) and array/string method factories
// are defined here. Both the VM execution engine and the test harness
// share this single source of truth.

#pragma once

#include <memory>
#include <string>

namespace vora {

class VM;
struct Array;
struct Dict;
class NativeFunction;

// Register all user-facing built-in native functions on a VM.
// Call after initGlobals() so global slots are pre-allocated.
void registerBuiltins(VM& vm);

// Array method factory — returns a bound method for the given array.
// Used by OP_GET_PROPERTY dispatch in the VM.
// Returns nullptr if the method name is unknown.
std::shared_ptr<NativeFunction> getArrayMethod(
    const std::string& name,
    std::shared_ptr<Array> arr);

// String method factory — returns a bound method for the given string.
// Used by OP_GET_PROPERTY dispatch in the VM.
// Returns nullptr if the method name is unknown.
std::shared_ptr<NativeFunction> getStringMethod(
    const std::string& name,
    std::string str);

// Dict method factory — returns a bound method for the given dict.
// Used by OP_GET_PROPERTY dispatch in the VM.
// Returns nullptr if the method name is unknown.
std::shared_ptr<NativeFunction> getDictMethod(
    const std::string& name,
    std::shared_ptr<Dict> dict);

} // namespace vora
