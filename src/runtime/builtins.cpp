// src/runtime/builtins.cpp — Unified built-in native function implementations
//
// Contains every built-in native function (print, type, len, clock, assert,
// int, float, range, input, bin, oct, hex) and all array/string method
// factories. This is the single source of truth shared by the VM execution
// engine, the REPL, and the C++ unit test harness.

#include "builtins.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

#include "../gc/gc_heap.h"
#include "nlohmann/json.hpp"  // nlohmann/json — JSON parsing engine
#include "native_function.h"
#include "runtime_error.h"
#include "value.h"
#include "vora_function.h"
#include "../vm/compiler.h"   // FunctionPrototype
#include "vm/value_ops.h"
#include "vm/vm.h"

namespace vora {

// ============================================================================
// User-facing built-in native functions
// ============================================================================

void registerBuiltins(VM& vm) {
    vm.defineNative("print", -1,
        [](const std::vector<Value>& arguments) -> Value {
            for (size_t i = 0; i < arguments.size(); ++i) {
                if (i > 0) std::cout << ' ';
                printValue(arguments[i]);
            }
            std::cout << std::endl;
            return nullptr;
        });

    vm.defineNative("type", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& v = arguments[0];
            switch (v.dispatchTag()) {
                case DispatchTag::Null:   return GcHeap::instance().alloc<GcString>("null");
                case DispatchTag::Bool:   return GcHeap::instance().alloc<GcString>("boolean");
                case DispatchTag::Int:    return GcHeap::instance().alloc<GcString>("int");
                case DispatchTag::Double: return GcHeap::instance().alloc<GcString>("float");
                case DispatchTag::GcString: return GcHeap::instance().alloc<GcString>("string");
                case DispatchTag::Array:  return GcHeap::instance().alloc<GcString>("array");
                case DispatchTag::Dict:   return GcHeap::instance().alloc<GcString>("dict");
                case DispatchTag::Set:    return GcHeap::instance().alloc<GcString>("set");
                case DispatchTag::Map:    return GcHeap::instance().alloc<GcString>("map");
                case DispatchTag::Callable: return GcHeap::instance().alloc<GcString>("function");
                case DispatchTag::ObjectInstance: return GcHeap::instance().alloc<GcString>("object");
                case DispatchTag::FunctionPrototype: return GcHeap::instance().alloc<GcString>("function");
                case DispatchTag::ClassDefinition: return GcHeap::instance().alloc<GcString>("class");
                case DispatchTag::Generator: return GcHeap::instance().alloc<GcString>("generator");
                case DispatchTag::Task: return GcHeap::instance().alloc<GcString>("task");
                default: return GcHeap::instance().alloc<GcString>("unknown");
            }
        });

    vm.defineNative("len", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& v = arguments[0];
            if (v.isArray())
                return static_cast<int64_t>(v.asArray()->elements.size());
            if (v.isGcString())
                return static_cast<int64_t>(v.asGcString()->value.size());
            if (v.isDict())
                return static_cast<int64_t>(v.asDict()->pairs.size());
            if (v.isSet())
                return static_cast<int64_t>(v.asSet()->elements.size());
            if (v.isMap())
                return static_cast<int64_t>(v.asMap()->pairs.size());
            return nullptr;  // will be caught by runtime
        });

    vm.defineNative("clock", 0,
        [](const std::vector<Value>&) -> Value {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return static_cast<double>(millis) / 1000.0;
        });

    vm.defineNative("assert", -1,
        [&vm](const std::vector<Value>& arguments) -> Value {
            if (arguments.empty()) return nullptr;
            if (!isTruthy(arguments[0])) {
                std::string msg = "Assertion failed";
                if (arguments.size() >= 2 && arguments[1].isGcString()) {
                    msg = arguments[1].asGcString()->value;
                }
                // Route through VM exception mechanism so assert failures
                // are catchable by try/catch (consistent with interpreter).
                vm.nativeError = true;
                vm.nativeErrorValue = GcHeap::instance().alloc<GcString>(msg);
                return nullptr;
            }
            return nullptr;
        });

    vm.defineNative("int", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (arg.isInt())
                return arg;
            if (arg.isDouble())
                return static_cast<int64_t>(std::trunc(arg.asDouble()));
            if (arg.isGcString()) {
                try {
                    return static_cast<int64_t>(std::trunc(std::stod(arg.asGcString()->value)));
                } catch (const std::exception&) {
                    return nullptr;
                }
            }
            if (arg.isBool())
                return arg.asBool() ? static_cast<int64_t>(1) : static_cast<int64_t>(0);
            return nullptr;
        });

    vm.defineNative("float", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (arg.isInt()) return static_cast<double>(arg.asInt());
            if (arg.isDouble()) return arg;
            if (arg.isGcString()) {
                try {
                    return std::stod(arg.asGcString()->value);
                } catch (const std::exception&) {
                    return nullptr;
                }
            }
            if (arg.isBool())
                return arg.asBool() ? 1.0 : 0.0;
            return nullptr;
        });

    vm.defineNative("range", -1,
        [](const std::vector<Value>& arguments) -> Value {
            // Determine whether to use integer or float arithmetic.
            // If all arguments are integers and step is integer, produce ints.
            bool useInt = true;
            for (auto& arg : arguments) {
                if (!arg.isInt()) {
                    useInt = false;
                    break;
                }
            }

            auto arr = GcHeap::instance().alloc<Array>();

            if (useInt) {
                int64_t start = 0, end = 0, step = 1;
                if (arguments.size() >= 1) {
                    if (!arguments[0].isInt()) return nullptr;
                    end = arguments[0].asInt();
                }
                if (arguments.size() >= 2) {
                    if (!arguments[1].isInt()) return nullptr;
                    start = arguments[0].asInt();
                    end = arguments[1].asInt();
                }
                if (arguments.size() >= 3) {
                    if (!arguments[2].isInt()) return nullptr;
                    start = arguments[0].asInt();
                    end = arguments[1].asInt();
                    step = arguments[2].asInt();
                }
                if (step == 0) return nullptr;
                if (step > 0) {
                    for (int64_t i = start; i < end; i += step)
                        arr->elements.push_back(i);
                } else {
                    for (int64_t i = start; i > end; i += step)
                        arr->elements.push_back(i);
                }
            } else {
                double start = 0, end = 0, step = 1;
                if (arguments.size() >= 1) {
                    if (!isNumeric(arguments[0])) return nullptr;
                    end = toDouble(arguments[0]);
                }
                if (arguments.size() >= 2) {
                    if (!isNumeric(arguments[1])) return nullptr;
                    start = toDouble(arguments[0]);
                    end = toDouble(arguments[1]);
                }
                if (arguments.size() >= 3) {
                    if (!isNumeric(arguments[2])) return nullptr;
                    start = toDouble(arguments[0]);
                    end = toDouble(arguments[1]);
                    step = toDouble(arguments[2]);
                }
                if (step == 0.0) return nullptr;
                if (step > 0) {
                    for (double i = start; i < end; i += step)
                        arr->elements.push_back(i);
                } else if (step < 0) {
                    for (double i = start; i > end; i += step)
                        arr->elements.push_back(i);
                }
            }
            return arr;
        });

    vm.defineNative("input", -1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments.empty()) {
                std::cout << valueToString(arguments[0]);
                std::cout << std::flush;  // ensure prompt visible before blocking on stdin
            }
            std::string line;
            if (!std::getline(std::cin, line)) {
                // EOF → null; stream error → also null (with clear so stream
                // is usable again — critical for REPL after Ctrl+Z).
                std::cin.clear();
                return nullptr;
            }
            // Strip trailing \r (Windows CRLF) for cross-platform consistency.
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return GcHeap::instance().alloc<GcString>(line);
        });

    vm.defineNative("bin", 1,
        [](const std::vector<Value>& arguments) -> Value {
            int64_t n;
            if (arguments[0].isInt())
                n = arguments[0].asInt();
            else if (arguments[0].isDouble())
                n = static_cast<int64_t>(std::trunc(arguments[0].asDouble()));
            else
                return nullptr;  // non-numeric argument
            if (n == 0) return GcHeap::instance().alloc<GcString>("0b0");
            bool neg = n < 0;
            // static_cast<uint64_t>(n) is well-defined even for negative n
            // (two's complement). Avoids signed overflow UB when n == INT64_MIN.
            uint64_t u = static_cast<uint64_t>(n);
            std::string bits;
            while (u > 0) { bits = (u & 1 ? '1' : '0') + bits; u >>= 1; }
            return GcHeap::instance().alloc<GcString>((neg ? std::string("-0b") : std::string("0b")) + bits);
        });

    vm.defineNative("oct", 1,
        [](const std::vector<Value>& arguments) -> Value {
            int64_t n;
            if (arguments[0].isInt())
                n = arguments[0].asInt();
            else if (arguments[0].isDouble())
                n = static_cast<int64_t>(std::trunc(arguments[0].asDouble()));
            else
                return nullptr;  // non-numeric argument
            if (n == 0) return GcHeap::instance().alloc<GcString>("0o0");
            bool neg = n < 0;
            uint64_t u = static_cast<uint64_t>(n);
            std::string digits;
            while (u > 0) { digits = static_cast<char>('0' + (u & 7)) + digits; u >>= 3; }
            return GcHeap::instance().alloc<GcString>((neg ? std::string("-0o") : std::string("0o")) + digits);
        });

    vm.defineNative("hex", 1,
        [](const std::vector<Value>& arguments) -> Value {
            int64_t n;
            if (arguments[0].isInt())
                n = arguments[0].asInt();
            else if (arguments[0].isDouble())
                n = static_cast<int64_t>(std::trunc(arguments[0].asDouble()));
            else
                return nullptr;  // non-numeric argument
            if (n == 0) return GcHeap::instance().alloc<GcString>("0x0");
            bool neg = n < 0;
            uint64_t u = static_cast<uint64_t>(n);
            const char* hexChars = "0123456789abcdef";
            std::string digits;
            while (u > 0) { digits = hexChars[u & 0xF] + digits; u >>= 4; }
            return GcHeap::instance().alloc<GcString>((neg ? std::string("-0x") : std::string("0x")) + digits);
        });

    vm.defineNative("toString", 1,
        [](const std::vector<Value>& arguments) -> Value {
            return GcHeap::instance().alloc<GcString>(valueToString(arguments[0]));
        });

    // --- iter(collection) — create an iterator over an array, string, or dict ---
    // iter(iterator) returns the iterator itself (Python convention).
    vm.defineNative("iter", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const Value& arg = arguments[0];

            // iter(iterator) → itself
            if (arg.isIterator()) {
                return arg;
            }

            // iter(generator) → the generator itself (it is its own iterator,
            // driven by next() which already handles Generator)
            if (arg.isGenerator()) {
                return arg;
            }

            auto it = GcHeap::instance().alloc<Iterator>();
            it->source = arg;
            it->index = 0;

            // Snapshot dict keys for stable iteration order
            if (arg.isDict()) {
                auto d = arg.asDict();
                it->dictKeys.reserve(d->pairs.size());
                for (const auto& [k, v] : d->pairs) {
                    it->dictKeys.push_back(k);
                }
            } else if (arg.isSet()) {
                auto s = arg.asSet();
                // Snapshot set elements for stable iteration
                it->valueKeys.reserve(s->elements.size());
                for (const auto& e : s->elements) {
                    it->valueKeys.push_back(e);
                }
            } else if (arg.isMap()) {
                auto m = arg.asMap();
                // Snapshot map keys for stable iteration
                it->valueKeys.reserve(m->pairs.size());
                for (const auto& [k, v] : m->pairs) {
                    it->valueKeys.push_back(k);
                }
            } else if (arg.isObjectInstance()) {
                auto inst = arg.asObjectInstance();
                // Snapshot object property keys for stable iteration
                it->dictKeys.reserve(inst->properties.size());
                for (const auto& [k, v] : inst->properties) {
                    it->dictKeys.push_back(k);
                }
            } else if (!arg.isArray() &&
                       !arg.isGcString()) {
                // iter() on non-iterable → null (caller can check)
                // We still return an iterator — next() will throw StopIteration
            }

            return it;
        });

    // --- next(iterator|generator) — advance and return next element ---
    // Supports both Iterator (array/string/dict) and Generator (yield).
    // Uses vm.nativeError to signal StopIteration (catchable via try/catch).
    vm.defineNative("next", 1,
        [&vm](const std::vector<Value>& arguments) -> Value {
            const Value& arg = arguments[0];

            // --- Generator path ---
            if (arg.isGenerator()) {
                auto gen = arg.asGenerator();
                if (!gen) {
                    vm.nativeError = true;
                    vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("TypeError: next() argument is null");
                    return nullptr;
                }

                if (gen->done) {
                    vm.nativeError = true;
                    vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("StopIteration");
                    return nullptr;
                }

                // Save caller's execution context into the generator
                gen->callerIp = vm.ip;
                gen->callerChunk = vm.currentChunk;
                gen->callerFrameBase = vm.frameBaseIndex;
                gen->callerFramesSize = vm.frames.size();

                // Compute target IP for pending resume
                const FunctionPrototype* proto = gen->function->getPrototype();
                if (gen->firstResume) {
                    vm.pendingIp = proto->chunk.code.data();
                } else {
                    vm.pendingIp = proto->chunk.code.data() + gen->savedIpOffset;
                }
                vm.pendingChunk = &proto->chunk;
                vm.pendingGenerator = gen;
                vm.pendingResume = true;

                return nullptr;  // Dummy — popped by pendingResume handler
            }

            // --- Iterator path ---
            if (!arg.isIterator()) {
                vm.nativeError = true;
                vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("TypeError: next() requires an iterator or generator");
                return nullptr;
            }

            auto it = arg.asIterator();
            const Value& source = it->source;
            size_t idx = it->index;

            // Array iteration
            if (source.isArray()) {
                auto arr = source.asArray();
                if (idx >= arr->elements.size()) {
                    vm.nativeError = true;
                    vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("StopIteration");
                    return nullptr;
                }
                it->index = idx + 1;
                return arr->elements[idx];
            }

            // String iteration — returns 1-char strings (consistent with for-in)
            if (source.isGcString()) {
                auto s = source.asGcString();
                if (idx >= s->value.size()) {
                    vm.nativeError = true;
                    vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("StopIteration");
                    return nullptr;
                }
                it->index = idx + 1;
                return GcHeap::instance().alloc<GcString>(std::string(1, s->value[idx]));
            }

            // Dict iteration — iterates over keys (snapshot taken at iter() time)
            if (source.isDict()) {
                if (idx >= it->dictKeys.size()) {
                    vm.nativeError = true;
                    vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("StopIteration");
                    return nullptr;
                }
                it->index = idx + 1;
                return GcHeap::instance().alloc<GcString>(it->dictKeys[idx]);
            }

            // Set iteration — iterates over elements
            if (source.isSet()) {
                if (idx >= it->valueKeys.size()) {
                    vm.nativeError = true;
                    vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("StopIteration");
                    return nullptr;
                }
                it->index = idx + 1;
                return it->valueKeys[idx];
            }

            // Map iteration — iterates over keys
            if (source.isMap()) {
                if (idx >= it->valueKeys.size()) {
                    vm.nativeError = true;
                    vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("StopIteration");
                    return nullptr;
                }
                it->index = idx + 1;
                return it->valueKeys[idx];
            }

            // ObjectInstance iteration — iterates over property keys
            if (source.isObjectInstance()) {
                if (idx >= it->dictKeys.size()) {
                    vm.nativeError = true;
                    vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("StopIteration");
                    return nullptr;
                }
                it->index = idx + 1;
                return GcHeap::instance().alloc<GcString>(it->dictKeys[idx]);
            }

            // Exhausted or non-iterable source
            vm.nativeError = true;
            vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("StopIteration");
            return nullptr;
        });

    // --- Set(iterable?) — create a Set ---
    vm.defineNative("Set", -1,
        [](const std::vector<Value>& arguments) -> Value {
            auto set = GcHeap::instance().alloc<Set>();
            if (!arguments.empty()) {
                const auto& arg = arguments[0];
                if (arg.isArray()) {
                    for (const auto& e : arg.asArray()->elements) {
                        set->elements.insert(e);
                    }
                } else if (arg.isGcString()) {
                    for (char c : arg.asGcString()->value) {
                        set->elements.insert(GcHeap::instance().alloc<GcString>(std::string(1, c)));
                    }
                } else if (arg.isSet()) {
                    set->elements = arg.asSet()->elements;
                } else if (arg.isDict()) {
                    for (const auto& [k, v] : arg.asDict()->pairs) {
                        set->elements.insert(GcHeap::instance().alloc<GcString>(k));
                    }
                }
                // Other types: ignore, return empty set
            }
            return set;
        });

    // --- Map() — create an empty Map ---
    vm.defineNative("Map", 0,
        [](const std::vector<Value>&) -> Value {
            return GcHeap::instance().alloc<Map>();
        });

    // --- run(task) — drive an async Task one step ---
    // For simple async functions where all awaits resolve immediately,
    // one call to run() completes the task. For pending awaits, call
    // run() again after the awaited task completes.
    vm.defineNative("run", 1,
        [&vm](const std::vector<Value>& arguments) -> Value {
            const Value& arg = arguments[0];
            if (!arg.isTask()) {
                vm.nativeError = true;
                vm.nativeErrorValue = GcHeap::instance().alloc<GcString>(
                    "TypeError: run() requires a Task");
                return nullptr;
            }

            auto task = arg.asTask();
            if (task->state == Task::Fulfilled) {
                return task->result;
            }
            if (task->state == Task::Rejected) {
                vm.nativeError = true;
                vm.nativeErrorValue = task->result;
                return nullptr;
            }

            auto gen = task->generator;
            if (gen->done) {
                task->state = Task::Fulfilled;
                return task->result;
            }

            // Resume the generator one step (same logic as next() builtin)
            gen->callerIp = vm.ip;
            gen->callerChunk = vm.currentChunk;
            gen->callerFrameBase = vm.frameBaseIndex;
            gen->callerFramesSize = vm.frames.size();

            const FunctionPrototype* proto = gen->function->getPrototype();
            if (gen->firstResume) {
                vm.pendingIp = proto->chunk.code.data();
            } else {
                vm.pendingIp = proto->chunk.code.data() + gen->savedIpOffset;
            }
            vm.pendingChunk = &proto->chunk;
            vm.pendingGenerator = gen;
            vm.pendingResume = true;

            return nullptr;  // Popped by pendingResume handler
        });

    // Register math builtins
    registerMathBuiltins(vm);

    // Register JSON builtins
    registerJsonBuiltins(vm);

    // Register filesystem builtins
    registerFsBuiltins(vm);

    // Register OS builtins
    registerOsBuiltins(vm);

    // Register datetime builtins
    registerDatetimeBuiltins(vm);

    // Register regex builtins
    registerRegexBuiltins(vm);
}

// ============================================================================
// Array built-in methods
// ============================================================================

GcPtr<NativeFunction> getArrayMethod(
    const std::string& name,
    GcPtr<Array> arr
) {
    if (name == "add") {
        return GcHeap::instance().alloc<NativeFunction>("add", 1,
            [arr](const std::vector<Value>& args) -> Value {
                arr->elements.push_back(args[0]);
                return nullptr;
            });
    }
    if (name == "pop") {
        return GcHeap::instance().alloc<NativeFunction>("pop", 0,
            [arr](const std::vector<Value>&) -> Value {
                if (arr->elements.empty()) return nullptr;
                Value v = std::move(arr->elements.back());
                arr->elements.pop_back();
                return v;
            });
    }
    if (name == "insert") {
        return GcHeap::instance().alloc<NativeFunction>("insert", 2,
            [arr](const std::vector<Value>& args) -> Value {
                if (!isNumeric(args[0])) return nullptr;
                double idx = toDouble(args[0]);
                if (idx < 0) idx = 0;
                size_t pos = static_cast<size_t>(idx);
                if (pos >= arr->elements.size()) {
                    arr->elements.push_back(args[1]);
                } else {
                    arr->elements.insert(arr->elements.begin() + pos, args[1]);
                }
                return nullptr;
            });
    }
    if (name == "remove") {
        return GcHeap::instance().alloc<NativeFunction>("remove", 1,
            [arr](const std::vector<Value>& args) -> Value {
                if (!isNumeric(args[0])) return nullptr;
                double idx = toDouble(args[0]);
                if (idx < 0 || static_cast<size_t>(idx) >= arr->elements.size())
                    return nullptr;
                Value v = std::move(arr->elements[static_cast<size_t>(idx)]);
                arr->elements.erase(arr->elements.begin() + static_cast<size_t>(idx));
                return v;
            });
    }
    if (name == "indexOf") {
        return GcHeap::instance().alloc<NativeFunction>("indexOf", 1,
            [arr](const std::vector<Value>& args) -> Value {
                for (size_t i = 0; i < arr->elements.size(); ++i) {
                    if (arr->elements[i].dispatchTag() == args[0].dispatchTag()) {
                        if (arr->elements[i].isNull()) return static_cast<int64_t>(i);
                        if (arr->elements[i].isBool() &&
                            arr->elements[i].asBool() == args[0].asBool()) return static_cast<int64_t>(i);
                        if (arr->elements[i].isInt() &&
                            arr->elements[i].asInt() == args[0].asInt()) return static_cast<int64_t>(i);
                        if (arr->elements[i].isDouble() &&
                            arr->elements[i].asDouble() == args[0].asDouble()) return static_cast<int64_t>(i);
                        if (arr->elements[i].isGcString() &&
                            arr->elements[i].asGcString()->value == args[0].asGcString()->value) return static_cast<int64_t>(i);
                    }
                    if (isNumeric(arr->elements[i]) && isNumeric(args[0]) &&
                        toDouble(arr->elements[i]) == toDouble(args[0])) return static_cast<int64_t>(i);
                }
                return static_cast<int64_t>(-1);
            });
    }
    if (name == "clear") {
        return GcHeap::instance().alloc<NativeFunction>("clear", 0,
            [arr](const std::vector<Value>&) -> Value {
                arr->elements.clear();
                return nullptr;
            });
    }
    return nullptr;
}

// ============================================================================
// Dict built-in methods
// ============================================================================

GcPtr<NativeFunction> getDictMethod(
    const std::string& name,
    GcPtr<Dict> dict
) {
    if (name == "keys") {
        return GcHeap::instance().alloc<NativeFunction>("keys", 0,
            [dict](const std::vector<Value>&) -> Value {
                auto arr = GcHeap::instance().alloc<Array>();
                for (const auto& [k, v] : dict->pairs) {
                    arr->elements.push_back(GcHeap::instance().alloc<GcString>(k));
                }
                return arr;
            });
    }
    if (name == "values") {
        return GcHeap::instance().alloc<NativeFunction>("values", 0,
            [dict](const std::vector<Value>&) -> Value {
                auto arr = GcHeap::instance().alloc<Array>();
                for (const auto& [k, v] : dict->pairs) {
                    arr->elements.push_back(v);
                }
                return arr;
            });
    }
    if (name == "has") {
        return GcHeap::instance().alloc<NativeFunction>("has", 1,
            [dict](const std::vector<Value>& args) -> Value {
                std::string key = valueToString(args[0]);
                return dict->pairs.find(key) != dict->pairs.end();
            });
    }
    if (name == "remove") {
        return GcHeap::instance().alloc<NativeFunction>("remove", 1,
            [dict](const std::vector<Value>& args) -> Value {
                std::string key = valueToString(args[0]);
                auto it = dict->pairs.find(key);
                if (it == dict->pairs.end()) return nullptr;
                Value v = it->second;
                dict->pairs.erase(it);
                return v;
            });
    }
    return nullptr;
}

// ============================================================================
// Set built-in methods
// ============================================================================

GcPtr<NativeFunction> getSetMethod(
    const std::string& name,
    GcPtr<Set> set
) {
    if (name == "add") {
        return GcHeap::instance().alloc<NativeFunction>("add", 1,
            [set](const std::vector<Value>& args) -> Value {
                set->elements.insert(args[0]);
                return nullptr;
            });
    }
    if (name == "has") {
        return GcHeap::instance().alloc<NativeFunction>("has", 1,
            [set](const std::vector<Value>& args) -> Value {
                return set->elements.find(args[0]) != set->elements.end();
            });
    }
    if (name == "remove") {
        return GcHeap::instance().alloc<NativeFunction>("remove", 1,
            [set](const std::vector<Value>& args) -> Value {
                set->elements.erase(args[0]);
                return nullptr;
            });
    }
    if (name == "clear") {
        return GcHeap::instance().alloc<NativeFunction>("clear", 0,
            [set](const std::vector<Value>&) -> Value {
                set->elements.clear();
                return nullptr;
            });
    }
    if (name == "values") {
        return GcHeap::instance().alloc<NativeFunction>("values", 0,
            [set](const std::vector<Value>&) -> Value {
                auto arr = GcHeap::instance().alloc<Array>();
                arr->elements.reserve(set->elements.size());
                for (const auto& e : set->elements) {
                    arr->elements.push_back(e);
                }
                return arr;
            });
    }
    return nullptr;
}

// ============================================================================
// Map built-in methods
// ============================================================================

GcPtr<NativeFunction> getMapMethod(
    const std::string& name,
    GcPtr<Map> map
) {
    if (name == "set") {
        return GcHeap::instance().alloc<NativeFunction>("set", 2,
            [map](const std::vector<Value>& args) -> Value {
                map->pairs[args[0]] = args[1];
                return nullptr;
            });
    }
    if (name == "get") {
        return GcHeap::instance().alloc<NativeFunction>("get", 1,
            [map](const std::vector<Value>& args) -> Value {
                auto it = map->pairs.find(args[0]);
                if (it == map->pairs.end()) return nullptr;
                return it->second;
            });
    }
    if (name == "has") {
        return GcHeap::instance().alloc<NativeFunction>("has", 1,
            [map](const std::vector<Value>& args) -> Value {
                return map->pairs.find(args[0]) != map->pairs.end();
            });
    }
    if (name == "remove") {
        return GcHeap::instance().alloc<NativeFunction>("remove", 1,
            [map](const std::vector<Value>& args) -> Value {
                auto it = map->pairs.find(args[0]);
                if (it == map->pairs.end()) return nullptr;
                Value v = it->second;
                map->pairs.erase(it);
                return v;
            });
    }
    if (name == "keys") {
        return GcHeap::instance().alloc<NativeFunction>("keys", 0,
            [map](const std::vector<Value>&) -> Value {
                auto arr = GcHeap::instance().alloc<Array>();
                arr->elements.reserve(map->pairs.size());
                for (const auto& [k, v] : map->pairs) {
                    arr->elements.push_back(k);
                }
                return arr;
            });
    }
    if (name == "values") {
        return GcHeap::instance().alloc<NativeFunction>("values", 0,
            [map](const std::vector<Value>&) -> Value {
                auto arr = GcHeap::instance().alloc<Array>();
                arr->elements.reserve(map->pairs.size());
                for (const auto& [k, v] : map->pairs) {
                    arr->elements.push_back(v);
                }
                return arr;
            });
    }
    if (name == "clear") {
        return GcHeap::instance().alloc<NativeFunction>("clear", 0,
            [map](const std::vector<Value>&) -> Value {
                map->pairs.clear();
                return nullptr;
            });
    }
    return nullptr;
}

// ============================================================================
// String built-in methods
// ============================================================================

GcPtr<NativeFunction> getStringMethod(
    const std::string& name,
    std::string str
) {
    if (name == "substring") {
        return GcHeap::instance().alloc<NativeFunction>("substring", -1,
            [str](const std::vector<Value>& args) -> Value {
                int64_t len = static_cast<int64_t>(str.size());
                if (len == 0) return GcHeap::instance().alloc<GcString>("");
                int64_t start = (args.size() >= 1 && isNumeric(args[0]))
                    ? static_cast<int64_t>(toDouble(args[0])) : 0;
                int64_t end   = (args.size() >= 2 && isNumeric(args[1]))
                    ? static_cast<int64_t>(toDouble(args[1])) : len;
                if (start < 0) start += len;
                if (end < 0) end += len;
                if (start < 0) start = 0;
                if (end > len) end = len;
                if (start >= end) return GcHeap::instance().alloc<GcString>("");
                return GcHeap::instance().alloc<GcString>(str.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)));
            });
    }
    if (name == "include") {
        return GcHeap::instance().alloc<NativeFunction>("include", 1,
            [str](const std::vector<Value>& args) -> Value {
                if (!args[0].isGcString()) return false;
                return str.find(args[0].asGcString()->value) != std::string::npos;
            });
    }
    if (name == "startsWith") {
        return GcHeap::instance().alloc<NativeFunction>("startsWith", 1,
            [str](const std::vector<Value>& args) -> Value {
                if (!args[0].isGcString()) return false;
                const auto& prefix = args[0].asGcString()->value;
                return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
            });
    }
    if (name == "endsWith") {
        return GcHeap::instance().alloc<NativeFunction>("endsWith", 1,
            [str](const std::vector<Value>& args) -> Value {
                if (!args[0].isGcString()) return false;
                const auto& suffix = args[0].asGcString()->value;
                return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
            });
    }
    if (name == "upper") {
        return GcHeap::instance().alloc<NativeFunction>("upper", 0,
            [str](const std::vector<Value>&) -> Value {
                std::string result = str;
                for (auto& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return GcHeap::instance().alloc<GcString>(result);
            });
    }
    if (name == "lower") {
        return GcHeap::instance().alloc<NativeFunction>("lower", 0,
            [str](const std::vector<Value>&) -> Value {
                std::string result = str;
                for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return GcHeap::instance().alloc<GcString>(result);
            });
    }
    if (name == "trim") {
        return GcHeap::instance().alloc<NativeFunction>("trim", 0,
            [str](const std::vector<Value>&) -> Value {
                size_t start = 0;
                while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) ++start;
                if (start == str.size()) return GcHeap::instance().alloc<GcString>("");
                size_t end = str.size() - 1;
                while (end > start && std::isspace(static_cast<unsigned char>(str[end]))) --end;
                return GcHeap::instance().alloc<GcString>(str.substr(start, end - start + 1));
            });
    }
    if (name == "replace") {
        return GcHeap::instance().alloc<NativeFunction>("replace", 2,
            [str](const std::vector<Value>& args) -> Value {
                if (!args[0].isGcString() || !args[1].isGcString())
                    return GcHeap::instance().alloc<GcString>(str);
                const auto& oldStr = args[0].asGcString()->value;
                const auto& newStr = args[1].asGcString()->value;
                if (oldStr.empty()) return GcHeap::instance().alloc<GcString>(str);
                size_t pos = str.find(oldStr);
                if (pos == std::string::npos) return GcHeap::instance().alloc<GcString>(str);
                std::string result = str;
                result.replace(pos, oldStr.size(), newStr);
                return GcHeap::instance().alloc<GcString>(result);
            });
    }
    if (name == "replaceAll") {
        return GcHeap::instance().alloc<NativeFunction>("replaceAll", 2,
            [str](const std::vector<Value>& args) -> Value {
                if (!args[0].isGcString() || !args[1].isGcString())
                    return GcHeap::instance().alloc<GcString>(str);
                const auto& oldStr = args[0].asGcString()->value;
                const auto& newStr = args[1].asGcString()->value;
                if (oldStr.empty()) return GcHeap::instance().alloc<GcString>(str);
                std::string result = str;
                size_t pos = 0;
                while ((pos = result.find(oldStr, pos)) != std::string::npos) {
                    result.replace(pos, oldStr.size(), newStr);
                    pos += newStr.size();
                }
                return GcHeap::instance().alloc<GcString>(result);
            });
    }
    if (name == "split") {
        return GcHeap::instance().alloc<NativeFunction>("split", 1,
            [str](const std::vector<Value>& args) -> Value {
                auto result = GcHeap::instance().alloc<Array>();
                if (!args[0].isGcString()) {
                    result->elements.push_back(GcHeap::instance().alloc<GcString>(str));
                    return result;
                }
                const auto& delim = args[0].asGcString()->value;
                if (delim.empty()) {
                    for (char c : str) result->elements.push_back(GcHeap::instance().alloc<GcString>(std::string(1, c)));
                    return result;
                }
                size_t pos = 0;
                size_t found;
                while ((found = str.find(delim, pos)) != std::string::npos) {
                    result->elements.push_back(GcHeap::instance().alloc<GcString>(str.substr(pos, found - pos)));
                    pos = found + delim.size();
                }
                result->elements.push_back(GcHeap::instance().alloc<GcString>(str.substr(pos)));
                return result;
            });
    }
    return nullptr;
}

// ============================================================================
// Math built-in native functions — used by std/math.va
// ============================================================================

void registerMathBuiltins(VM& vm) {
    // abs(x) — absolute value (int64 → abs, double → fabs, other → null/error)
    vm.defineNative("abs", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (arg.isInt()) {
                int64_t v = arg.asInt();
                return (v < 0) ? -v : v;
            }
            if (arg.isDouble()) {
                double v = arg.asDouble();
                return (v < 0.0) ? -v : v;
            }
            return nullptr;  // non-numeric → null (treated as error by user)
        });

    // sqrt(x) — square root (numeric → double, negative → null)
    vm.defineNative("sqrt", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            double x = 0.0;
            if (arg.isInt())
                x = static_cast<double>(arg.asInt());
            else if (arg.isDouble())
                x = arg.asDouble();
            else
                return nullptr;
            if (x < 0.0) return nullptr;
            return std::sqrt(x);
        });

    // sin(x) — sine of x in radians
    vm.defineNative("sin", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            double x = 0.0;
            if (arg.isInt())
                x = static_cast<double>(arg.asInt());
            else if (arg.isDouble())
                x = arg.asDouble();
            else
                return nullptr;
            return std::sin(x);
        });

    // cos(x) — cosine of x in radians
    vm.defineNative("cos", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            double x = 0.0;
            if (arg.isInt())
                x = static_cast<double>(arg.asInt());
            else if (arg.isDouble())
                x = arg.asDouble();
            else
                return nullptr;
            return std::cos(x);
        });

    // min(arr) — minimum value in array (all elements must be numeric)
    vm.defineNative("min", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (!arg.isArray())
                return nullptr;
            const auto& elems = arg.asArray()->elements;
            if (elems.empty()) return nullptr;

            Value minVal = elems[0];
            for (size_t i = 1; i < elems.size(); i++) {
                double a = 0.0, b = 0.0;
                if (elems[i].isInt())
                    a = static_cast<double>(elems[i].asInt());
                else if (elems[i].isDouble())
                    a = elems[i].asDouble();
                else
                    return nullptr;
                if (minVal.isInt())
                    b = static_cast<double>(minVal.asInt());
                else if (minVal.isDouble())
                    b = minVal.asDouble();
                else
                    return nullptr;
                if (a < b) minVal = elems[i];
            }
            return minVal;
        });

    // max(arr) — maximum value in array (all elements must be numeric)
    vm.defineNative("max", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (!arg.isArray())
                return nullptr;
            const auto& elems = arg.asArray()->elements;
            if (elems.empty()) return nullptr;

            Value maxVal = elems[0];
            for (size_t i = 1; i < elems.size(); i++) {
                double a = 0.0, b = 0.0;
                if (elems[i].isInt())
                    a = static_cast<double>(elems[i].asInt());
                else if (elems[i].isDouble())
                    a = elems[i].asDouble();
                else
                    return nullptr;
                if (maxVal.isInt())
                    b = static_cast<double>(maxVal.asInt());
                else if (maxVal.isDouble())
                    b = maxVal.asDouble();
                else
                    return nullptr;
                if (a > b) maxVal = elems[i];
            }
            return maxVal;
        });

    // random_int(min, max) — random integer in [min, max]
    vm.defineNative("random_int", 2,
        [](const std::vector<Value>& arguments) -> Value {
            double minD = 0.0, maxD = 0.0;
            if (arguments[0].isInt())
                minD = static_cast<double>(arguments[0].asInt());
            else if (arguments[0].isDouble())
                minD = arguments[0].asDouble();
            else return nullptr;
            if (arguments[1].isInt())
                maxD = static_cast<double>(arguments[1].asInt());
            else if (arguments[1].isDouble())
                maxD = arguments[1].asDouble();
            else return nullptr;

            int64_t minVal = static_cast<int64_t>(minD);
            int64_t maxVal = static_cast<int64_t>(maxD);
            if (minVal > maxVal) return nullptr;
            int64_t range = maxVal - minVal + 1;
            return static_cast<int64_t>(minVal + (std::rand() % range));
        });

    // random_float(min, max, decimals) — random float with N decimal places
    vm.defineNative("random_float", 3,
        [](const std::vector<Value>& arguments) -> Value {
            double minVal = 0.0, maxVal = 0.0;
            int decimals = 0;

            if (arguments[0].isInt())
                minVal = static_cast<double>(arguments[0].asInt());
            else if (arguments[0].isDouble())
                minVal = arguments[0].asDouble();
            else return nullptr;

            if (arguments[1].isInt())
                maxVal = static_cast<double>(arguments[1].asInt());
            else if (arguments[1].isDouble())
                maxVal = arguments[1].asDouble();
            else return nullptr;

            if (arguments[2].isInt())
                decimals = static_cast<int>(arguments[2].asInt());
            else if (arguments[2].isDouble())
                decimals = static_cast<int>(arguments[2].asDouble());
            else return nullptr;

            if (minVal > maxVal || decimals < 0) return nullptr;
            double r = minVal + (static_cast<double>(std::rand()) / RAND_MAX) * (maxVal - minVal);
            double factor = std::pow(10.0, decimals);
            return std::round(r * factor) / factor;
        });
}

// ============================================================================
// JSON built-in native functions — used by std/json.va
// ============================================================================
//
// Powered by nlohmann/json v3.11.3 (header-only, MIT license).
// This gives us full JSON spec compliance (RFC 8259), correct Unicode
// handling including surrogate pairs, and robust number parsing — all
// through a thin bridge layer between nlohmann::json and Vora::Value.

namespace {

/// Convert a nlohmann::json value to a Vora Value.
///
/// Mapping:
///   null          → nullptr
///   boolean       → bool
///   number_integer/unsigned → int64_t
///   number_float  → double
///   string        → GcPtr<GcString> (GC-allocated)
///   array         → GcPtr<Array>    (GC-allocated)
///   object        → GcPtr<Dict>     (GC-allocated, string keys only)
Value jsonToValue(const nlohmann::json& j) {
    switch (j.type()) {
        case nlohmann::json::value_t::null:
            return nullptr;
        case nlohmann::json::value_t::boolean:
            return j.get<bool>();
        case nlohmann::json::value_t::number_integer:
            return static_cast<int64_t>(j.get<int64_t>());
        case nlohmann::json::value_t::number_unsigned:
            return static_cast<int64_t>(j.get<uint64_t>());
        case nlohmann::json::value_t::number_float:
            return j.get<double>();
        case nlohmann::json::value_t::string:
            return GcHeap::instance().alloc<GcString>(j.get<std::string>());
        case nlohmann::json::value_t::array: {
            auto arr = GcHeap::instance().alloc<Array>();
            arr->elements.reserve(j.size());
            for (const auto& elem : j)
                arr->elements.push_back(jsonToValue(elem));
            return arr;
        }
        case nlohmann::json::value_t::object: {
            auto dict = GcHeap::instance().alloc<Dict>();
            for (auto it = j.begin(); it != j.end(); ++it)
                dict->pairs[it.key()] = jsonToValue(it.value());
            return dict;
        }
        default:
            return nullptr;  // binary, discarded — shouldn't occur in JSON
    }
}

/// Convert a Vora Value to a nlohmann::json value.
///
/// Mapping (reverse of above).  Functions, objects, classes, and other
/// non-serializable Vora types are mapped to JSON null.
nlohmann::json valueToJson(const Value& val) {
    if (val.isDouble()) {
        return val.asDouble();
    }
    switch (val.tag()) {
        case ValueTag::Null:
            return nullptr;
        case ValueTag::Bool:
            return val.asBool();
        case ValueTag::Int:
            return val.asInt();
        case ValueTag::GcString:
            return val.asGcString()->value;
        case ValueTag::Array: {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& e : val.asArray()->elements)
                arr.push_back(valueToJson(e));
            return arr;
        }
        case ValueTag::Dict: {
            nlohmann::json obj = nlohmann::json::object();
            for (const auto& [k, v] : val.asDict()->pairs)
                obj[k] = valueToJson(v);
            return obj;
        }
        case ValueTag::Set: {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& e : val.asSet()->elements)
                arr.push_back(valueToJson(e));
            return arr;
        }
        case ValueTag::Map: {
            // Serialize as array of [key, value] pairs (lossless for non-string keys)
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& [k, v] : val.asMap()->pairs) {
                nlohmann::json pair = nlohmann::json::array();
                pair.push_back(valueToJson(k));
                pair.push_back(valueToJson(v));
                arr.push_back(std::move(pair));
            }
            return arr;
        }
        default:
            // Functions, objects, classes, generators, etc. → JSON null
            return nullptr;
    }
}

} // anonymous namespace

void registerJsonBuiltins(VM& vm) {
    /// jsonParse(str) — parse a JSON string into a Vora value.
    /// Returns null on parse error (including empty string or non-string input).
    vm.defineNative("jsonParse", 1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString())
                return nullptr;
            const auto& str = arguments[0].asGcString()->value;
            try {
                nlohmann::json j = nlohmann::json::parse(str);
                return jsonToValue(j);
            } catch (const nlohmann::json::parse_error&) {
                return nullptr;
            }
        });

    /// jsonStringify(value, indent?) — serialize a Vora value to a JSON string.
    ///
    /// indent < 0  → compact, single-line output (default)
    /// indent >= 0 → pretty-printed with that many spaces per nesting level
    ///
    /// Returns null if serialization fails (e.g., deeply-nested structures
    /// that exhaust the output buffer).
    vm.defineNative("jsonStringify", -1,
        [](const std::vector<Value>& arguments) -> Value {
            if (arguments.empty()) return nullptr;
            try {
                nlohmann::json j = valueToJson(arguments[0]);
                int indent = -1;  // compact by default
                if (arguments.size() >= 2) {
                    if (arguments[1].isInt())
                        indent = static_cast<int>(arguments[1].asInt());
                    else if (arguments[1].isDouble())
                        indent = static_cast<int>(arguments[1].asDouble());
                }
                std::string result = (indent >= 0) ? j.dump(indent) : j.dump();
                return GcHeap::instance().alloc<GcString>(result);
            } catch (...) {
                return nullptr;
            }
        });
}

// ============================================================================
// Filesystem built-in native functions — used by std/fs.va
// ============================================================================

void registerFsBuiltins(VM& vm) {
    // readFile(path) — read entire file as string, returns null on error
    vm.defineNative("vora_fs_readFile", 1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString())
                return nullptr;
            const auto& path = arguments[0].asGcString()->value;
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return nullptr;
            std::stringstream ss;
            ss << file.rdbuf();
            return GcHeap::instance().alloc<GcString>(ss.str());
        });

    // writeFile(path, content) — write string to file, returns true on success
    vm.defineNative("vora_fs_writeFile", 2,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString() ||
                !arguments[1].isGcString())
                return false;
            const auto& path = arguments[0].asGcString()->value;
            const auto& content = arguments[1].asGcString()->value;
            std::ofstream file(path, std::ios::binary);
            if (!file.is_open()) return false;
            file << content;
            return file.good();
        });

    // exists(path) — check if file or directory exists
    vm.defineNative("vora_fs_exists", 1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString())
                return false;
            const auto& path = arguments[0].asGcString()->value;
            std::error_code ec;
            bool result = std::filesystem::exists(path, ec);
            return result && !ec;
        });

    // listDir(path) — list directory contents, returns array of filenames
    vm.defineNative("vora_fs_listDir", 1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString())
                return nullptr;
            const auto& path = arguments[0].asGcString()->value;
            auto arr = GcHeap::instance().alloc<Array>();
            try {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    auto filename = entry.path().filename().string();
                    arr->elements.push_back(
                        GcHeap::instance().alloc<GcString>(filename));
                }
            } catch (const std::filesystem::filesystem_error&) {
                return nullptr;
            }
            return arr;
        });

    // mkdir(path) — create directory, returns true on success
    vm.defineNative("vora_fs_mkdir", 1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString())
                return false;
            const auto& path = arguments[0].asGcString()->value;
            std::error_code ec;
            bool ok = std::filesystem::create_directory(path, ec);
            return ok && !ec;
        });

    // remove(path) — remove file or empty directory, returns true on success
    vm.defineNative("vora_fs_remove", 1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString())
                return false;
            const auto& path = arguments[0].asGcString()->value;
            std::error_code ec;
            bool ok = std::filesystem::remove(path, ec);
            return ok && !ec;
        });
}

// ============================================================================
// OS built-in native functions — used by std/os.va
// ============================================================================

namespace {
    std::vector<std::string> g_programArgs;
}

void setProgramArgs(int argc, char* argv[]) {
    g_programArgs.clear();
    for (int i = 0; i < argc; i++) {
        g_programArgs.push_back(argv[i] ? argv[i] : "");
    }
}

void registerOsBuiltins(VM& vm) {
    // getenv(name) — get environment variable, returns null if not set
    vm.defineNative("vora_os_getenv", 1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString())
                return nullptr;
            const auto& name = arguments[0].asGcString()->value;
            const char* val = std::getenv(name.c_str());
            if (!val) return nullptr;
            return GcHeap::instance().alloc<GcString>(std::string(val));
        });

    // getcwd() — get current working directory
    vm.defineNative("vora_os_getcwd", 0,
        [](const std::vector<Value>&) -> Value {
            std::error_code ec;
            auto p = std::filesystem::current_path(ec);
            if (ec) return nullptr;
            return GcHeap::instance().alloc<GcString>(p.string());
        });

    // exit(code) — exit the process with given code
    vm.defineNative("vora_os_exit", 1,
        [](const std::vector<Value>& arguments) -> Value {
            int code = 0;
            if (arguments[0].isInt())
                code = static_cast<int>(arguments[0].asInt());
            else if (arguments[0].isDouble())
                code = static_cast<int>(arguments[0].asDouble());
            std::exit(code);
            return nullptr;
        });

    // shell(cmd) — execute shell command and capture stdout, returns string
    vm.defineNative("vora_os_shell", 1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString())
                return nullptr;
            const auto& cmd = arguments[0].asGcString()->value;
#ifdef _WIN32
            std::string fullCmd = "cmd /c \"" + cmd + "\" 2>&1";
#else
            std::string fullCmd = cmd + " 2>&1";
#endif
            std::array<char, 256> buffer;
            std::string result;
#ifdef _WIN32
            FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
            FILE* pipe = popen(fullCmd.c_str(), "r");
#endif
            if (!pipe) return nullptr;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
                result += buffer.data();
            }
#ifdef _WIN32
            int rc = _pclose(pipe);
#else
            int rc = pclose(pipe);
#endif
            // Strip trailing newline if present
            if (!result.empty() && result.back() == '\n') result.pop_back();
            if (!result.empty() && result.back() == '\r') result.pop_back();
            return GcHeap::instance().alloc<GcString>(result);
        });

    // args() — get command-line arguments
    vm.defineNative("vora_os_args", 0,
        [](const std::vector<Value>&) -> Value {
            auto arr = GcHeap::instance().alloc<Array>();
            for (const auto& a : g_programArgs) {
                arr->elements.push_back(
                    GcHeap::instance().alloc<GcString>(a));
            }
            return arr;
        });
}

// ============================================================================
// Datetime built-in native functions — used by std/datetime.va
// ============================================================================

void registerDatetimeBuiltins(VM& vm) {
    // now() — current Unix timestamp in seconds (double)
    vm.defineNative("vora_time_now", 0,
        [](const std::vector<Value>&) -> Value {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto secs = std::chrono::duration_cast<std::chrono::duration<double>>(now).count();
            return secs;
        });

    // nowMs() — current Unix timestamp in milliseconds (int64_t)
    vm.defineNative("vora_time_nowMs", 0,
        [](const std::vector<Value>&) -> Value {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return static_cast<int64_t>(millis);
        });
}

// ============================================================================
// Regex built-in native functions — used by std/regex.va
// ============================================================================

void registerRegexBuiltins(VM& vm) {
    // match(pattern, text) — test if regex pattern matches anywhere in text
    vm.defineNative("vora_regex_match", 2,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString() ||
                !arguments[1].isGcString())
                return false;
            const auto& pattern = arguments[0].asGcString()->value;
            const auto& text = arguments[1].asGcString()->value;
            try {
                std::regex re(pattern);
                return std::regex_search(text, re);
            } catch (const std::regex_error&) {
                return nullptr;  // invalid pattern
            }
        });

    // find(pattern, text) — find all regex matches, returns array of match strings
    vm.defineNative("vora_regex_find", 2,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString() ||
                !arguments[1].isGcString())
                return nullptr;
            const auto& pattern = arguments[0].asGcString()->value;
            const auto& text = arguments[1].asGcString()->value;
            try {
                std::regex re(pattern);
                auto arr = GcHeap::instance().alloc<Array>();
                auto begin = std::sregex_iterator(text.begin(), text.end(), re);
                auto end = std::sregex_iterator();
                for (auto it = begin; it != end; ++it) {
                    arr->elements.push_back(
                        GcHeap::instance().alloc<GcString>(it->str()));
                }
                return arr;
            } catch (const std::regex_error&) {
                return nullptr;  // invalid pattern
            }
        });

    // replace(pattern, text, replacement) — replace all regex matches
    vm.defineNative("vora_regex_replace", 3,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString() ||
                !arguments[1].isGcString() ||
                !arguments[2].isGcString())
                return nullptr;
            const auto& pattern = arguments[0].asGcString()->value;
            const auto& text = arguments[1].asGcString()->value;
            const auto& replacement = arguments[2].asGcString()->value;
            try {
                std::regex re(pattern);
                std::string result = std::regex_replace(text, re, replacement);
                return GcHeap::instance().alloc<GcString>(result);
            } catch (const std::regex_error&) {
                return nullptr;  // invalid pattern
            }
        });

    // split(pattern, text) — split string by regex delimiter
    vm.defineNative("vora_regex_split", 2,
        [](const std::vector<Value>& arguments) -> Value {
            if (!arguments[0].isGcString() ||
                !arguments[1].isGcString())
                return nullptr;
            const auto& pattern = arguments[0].asGcString()->value;
            const auto& text = arguments[1].asGcString()->value;
            try {
                std::regex re(pattern);
                auto arr = GcHeap::instance().alloc<Array>();
                auto begin = std::sregex_token_iterator(text.begin(), text.end(), re, -1);
                auto end = std::sregex_token_iterator();
                for (auto it = begin; it != end; ++it) {
                    arr->elements.push_back(
                        GcHeap::instance().alloc<GcString>(*it));
                }
                // If no matches found, return the original string in an array
                if (arr->elements.empty()) {
                    arr->elements.push_back(
                        GcHeap::instance().alloc<GcString>(text));
                }
                return arr;
            } catch (const std::regex_error&) {
                return nullptr;  // invalid pattern
            }
        });
}

} // namespace vora
