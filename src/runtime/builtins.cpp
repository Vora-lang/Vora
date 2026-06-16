// src/runtime/builtins.cpp — Unified built-in native function implementations
//
// Contains every built-in native function (print, type, len, clock, assert,
// int, float, range, input, bin, oct, hex) and all array/string method
// factories. This is the single source of truth shared by the VM execution
// engine, the REPL, and the C++ unit test harness.

#include "builtins.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "../gc/gc_heap.h"
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
            return std::visit([](auto&& arg) -> GcPtr<GcString> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::nullptr_t>) return GcHeap::instance().alloc<GcString>("null");
                else if constexpr (std::is_same_v<T, double>) return GcHeap::instance().alloc<GcString>("float");
                else if constexpr (std::is_same_v<T, int64_t>) return GcHeap::instance().alloc<GcString>("int");
                else if constexpr (std::is_same_v<T, bool>) return GcHeap::instance().alloc<GcString>("boolean");
                else if constexpr (std::is_same_v<T, GcPtr<GcString>>) return GcHeap::instance().alloc<GcString>("string");
                else if constexpr (std::is_same_v<T, GcPtr<Array>>) return GcHeap::instance().alloc<GcString>("array");
                else if constexpr (std::is_same_v<T, GcPtr<Dict>>) return GcHeap::instance().alloc<GcString>("dict");
                else if constexpr (std::is_same_v<T, GcPtr<Callable>>) return GcHeap::instance().alloc<GcString>("function");
                else if constexpr (std::is_same_v<T, GcPtr<ObjectInstance>>) return GcHeap::instance().alloc<GcString>("object");
                else if constexpr (std::is_same_v<T, GcPtr<FunctionPrototype>>) return GcHeap::instance().alloc<GcString>("function");
                else if constexpr (std::is_same_v<T, GcPtr<ClassDefinition>>) return GcHeap::instance().alloc<GcString>("class");
                else return GcHeap::instance().alloc<GcString>("unknown");
            }, arguments[0]);
        });

    vm.defineNative("len", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& v = arguments[0];
            if (std::holds_alternative<GcPtr<Array>>(v))
                return static_cast<int64_t>(std::get<GcPtr<Array>>(v)->elements.size());
            if (std::holds_alternative<GcPtr<GcString>>(v))
                return static_cast<int64_t>(std::get<GcPtr<GcString>>(v)->value.size());
            if (std::holds_alternative<GcPtr<Dict>>(v))
                return static_cast<int64_t>(std::get<GcPtr<Dict>>(v)->pairs.size());
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
                if (arguments.size() >= 2 && std::holds_alternative<GcPtr<GcString>>(arguments[1])) {
                    msg = std::get<GcPtr<GcString>>(arguments[1])->value;
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
            if (std::holds_alternative<int64_t>(arg))
                return arg;
            if (std::holds_alternative<double>(arg))
                return static_cast<int64_t>(std::trunc(std::get<double>(arg)));
            if (std::holds_alternative<GcPtr<GcString>>(arg)) {
                try {
                    return static_cast<int64_t>(std::trunc(std::stod(std::get<GcPtr<GcString>>(arg)->value)));
                } catch (const std::exception&) {
                    return nullptr;
                }
            }
            if (std::holds_alternative<bool>(arg))
                return std::get<bool>(arg) ? static_cast<int64_t>(1) : static_cast<int64_t>(0);
            return nullptr;
        });

    vm.defineNative("float", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (std::holds_alternative<int64_t>(arg)) return static_cast<double>(std::get<int64_t>(arg));
            if (std::holds_alternative<double>(arg)) return arg;
            if (std::holds_alternative<GcPtr<GcString>>(arg)) {
                try {
                    return std::stod(std::get<GcPtr<GcString>>(arg)->value);
                } catch (const std::exception&) {
                    return nullptr;
                }
            }
            if (std::holds_alternative<bool>(arg))
                return std::get<bool>(arg) ? 1.0 : 0.0;
            return nullptr;
        });

    vm.defineNative("range", -1,
        [](const std::vector<Value>& arguments) -> Value {
            double start = 0, end = 0, step = 1;
            // Guard toDouble against non-numeric arguments (would throw bad_variant_access).
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
            if (step == 0.0) return nullptr;  // infinite loop guard
            auto arr = GcHeap::instance().alloc<Array>();
            if (step > 0) {
                for (double i = start; i < end; i += step)
                    arr->elements.push_back(i);
            } else if (step < 0) {
                for (double i = start; i > end; i += step)
                    arr->elements.push_back(i);
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
            return GcHeap::instance().alloc<GcString>(line);
        });

    vm.defineNative("bin", 1,
        [](const std::vector<Value>& arguments) -> Value {
            int64_t n;
            if (std::holds_alternative<int64_t>(arguments[0]))
                n = std::get<int64_t>(arguments[0]);
            else if (std::holds_alternative<double>(arguments[0]))
                n = static_cast<int64_t>(std::trunc(std::get<double>(arguments[0])));
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
            if (std::holds_alternative<int64_t>(arguments[0]))
                n = std::get<int64_t>(arguments[0]);
            else if (std::holds_alternative<double>(arguments[0]))
                n = static_cast<int64_t>(std::trunc(std::get<double>(arguments[0])));
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
            if (std::holds_alternative<int64_t>(arguments[0]))
                n = std::get<int64_t>(arguments[0]);
            else if (std::holds_alternative<double>(arguments[0]))
                n = static_cast<int64_t>(std::trunc(std::get<double>(arguments[0])));
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
            if (std::holds_alternative<GcPtr<Iterator>>(arg)) {
                return arg;
            }

            auto it = GcHeap::instance().alloc<Iterator>();
            it->source = arg;
            it->index = 0;

            // Snapshot dict keys for stable iteration order
            if (auto* d = std::get_if<GcPtr<Dict>>(&arg)) {
                it->dictKeys.reserve((*d)->pairs.size());
                for (const auto& [k, v] : (*d)->pairs) {
                    it->dictKeys.push_back(k);
                }
            } else if (!std::holds_alternative<GcPtr<Array>>(arg) &&
                       !std::holds_alternative<GcPtr<GcString>>(arg)) {
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
            if (auto* genPtr = std::get_if<GcPtr<Generator>>(&arg)) {
                auto gen = *genPtr;
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
            if (!std::holds_alternative<GcPtr<Iterator>>(arg)) {
                vm.nativeError = true;
                vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("TypeError: next() requires an iterator or generator");
                return nullptr;
            }

            auto it = std::get<GcPtr<Iterator>>(arg);
            const Value& source = it->source;
            size_t idx = it->index;

            // Array iteration
            if (auto* arr = std::get_if<GcPtr<Array>>(&source)) {
                if (idx >= (*arr)->elements.size()) {
                    vm.nativeError = true;
                    vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("StopIteration");
                    return nullptr;
                }
                it->index = idx + 1;
                return (*arr)->elements[idx];
            }

            // String iteration — returns 1-char strings (consistent with for-in)
            if (auto* s = std::get_if<GcPtr<GcString>>(&source)) {
                if (idx >= (*s)->value.size()) {
                    vm.nativeError = true;
                    vm.nativeErrorValue = GcHeap::instance().alloc<GcString>("StopIteration");
                    return nullptr;
                }
                it->index = idx + 1;
                return GcHeap::instance().alloc<GcString>(std::string(1, (*s)->value[idx]));
            }

            // Dict iteration — iterates over keys (snapshot taken at iter() time)
            if (std::holds_alternative<GcPtr<Dict>>(source)) {
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

    // Register math builtins
    registerMathBuiltins(vm);

    // Register JSON builtins
    registerJsonBuiltins(vm);
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
                    if (arr->elements[i].index() == args[0].index()) {
                        if (std::holds_alternative<std::nullptr_t>(arr->elements[i])) return static_cast<int64_t>(i);
                        if (std::holds_alternative<bool>(arr->elements[i]) &&
                            std::get<bool>(arr->elements[i]) == std::get<bool>(args[0])) return static_cast<int64_t>(i);
                        if (std::holds_alternative<int64_t>(arr->elements[i]) &&
                            std::get<int64_t>(arr->elements[i]) == std::get<int64_t>(args[0])) return static_cast<int64_t>(i);
                        if (std::holds_alternative<double>(arr->elements[i]) &&
                            std::get<double>(arr->elements[i]) == std::get<double>(args[0])) return static_cast<int64_t>(i);
                        if (std::holds_alternative<GcPtr<GcString>>(arr->elements[i]) &&
                            std::get<GcPtr<GcString>>(arr->elements[i])->value == std::get<GcPtr<GcString>>(args[0])->value) return static_cast<int64_t>(i);
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
                if (!std::holds_alternative<GcPtr<GcString>>(args[0])) return false;
                return str.find(std::get<GcPtr<GcString>>(args[0])->value) != std::string::npos;
            });
    }
    if (name == "startsWith") {
        return GcHeap::instance().alloc<NativeFunction>("startsWith", 1,
            [str](const std::vector<Value>& args) -> Value {
                if (!std::holds_alternative<GcPtr<GcString>>(args[0])) return false;
                const auto& prefix = std::get<GcPtr<GcString>>(args[0])->value;
                return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
            });
    }
    if (name == "endsWith") {
        return GcHeap::instance().alloc<NativeFunction>("endsWith", 1,
            [str](const std::vector<Value>& args) -> Value {
                if (!std::holds_alternative<GcPtr<GcString>>(args[0])) return false;
                const auto& suffix = std::get<GcPtr<GcString>>(args[0])->value;
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
                if (!std::holds_alternative<GcPtr<GcString>>(args[0]) || !std::holds_alternative<GcPtr<GcString>>(args[1]))
                    return GcHeap::instance().alloc<GcString>(str);
                const auto& oldStr = std::get<GcPtr<GcString>>(args[0])->value;
                const auto& newStr = std::get<GcPtr<GcString>>(args[1])->value;
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
                if (!std::holds_alternative<GcPtr<GcString>>(args[0]) || !std::holds_alternative<GcPtr<GcString>>(args[1]))
                    return GcHeap::instance().alloc<GcString>(str);
                const auto& oldStr = std::get<GcPtr<GcString>>(args[0])->value;
                const auto& newStr = std::get<GcPtr<GcString>>(args[1])->value;
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
                if (!std::holds_alternative<GcPtr<GcString>>(args[0])) {
                    result->elements.push_back(GcHeap::instance().alloc<GcString>(str));
                    return result;
                }
                const auto& delim = std::get<GcPtr<GcString>>(args[0])->value;
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
            if (std::holds_alternative<int64_t>(arg)) {
                int64_t v = std::get<int64_t>(arg);
                return (v < 0) ? -v : v;
            }
            if (std::holds_alternative<double>(arg)) {
                double v = std::get<double>(arg);
                return (v < 0.0) ? -v : v;
            }
            return nullptr;  // non-numeric → null (treated as error by user)
        });

    // sqrt(x) — square root (numeric → double, negative → null)
    vm.defineNative("sqrt", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            double x = 0.0;
            if (std::holds_alternative<int64_t>(arg))
                x = static_cast<double>(std::get<int64_t>(arg));
            else if (std::holds_alternative<double>(arg))
                x = std::get<double>(arg);
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
            if (std::holds_alternative<int64_t>(arg))
                x = static_cast<double>(std::get<int64_t>(arg));
            else if (std::holds_alternative<double>(arg))
                x = std::get<double>(arg);
            else
                return nullptr;
            return std::sin(x);
        });

    // cos(x) — cosine of x in radians
    vm.defineNative("cos", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            double x = 0.0;
            if (std::holds_alternative<int64_t>(arg))
                x = static_cast<double>(std::get<int64_t>(arg));
            else if (std::holds_alternative<double>(arg))
                x = std::get<double>(arg);
            else
                return nullptr;
            return std::cos(x);
        });

    // min(arr) — minimum value in array (all elements must be numeric)
    vm.defineNative("min", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (!std::holds_alternative<GcPtr<Array>>(arg))
                return nullptr;
            const auto& elems = std::get<GcPtr<Array>>(arg)->elements;
            if (elems.empty()) return nullptr;

            Value minVal = elems[0];
            for (size_t i = 1; i < elems.size(); i++) {
                double a = 0.0, b = 0.0;
                if (std::holds_alternative<int64_t>(elems[i]))
                    a = static_cast<double>(std::get<int64_t>(elems[i]));
                else if (std::holds_alternative<double>(elems[i]))
                    a = std::get<double>(elems[i]);
                else
                    return nullptr;
                if (std::holds_alternative<int64_t>(minVal))
                    b = static_cast<double>(std::get<int64_t>(minVal));
                else if (std::holds_alternative<double>(minVal))
                    b = std::get<double>(minVal);
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
            if (!std::holds_alternative<GcPtr<Array>>(arg))
                return nullptr;
            const auto& elems = std::get<GcPtr<Array>>(arg)->elements;
            if (elems.empty()) return nullptr;

            Value maxVal = elems[0];
            for (size_t i = 1; i < elems.size(); i++) {
                double a = 0.0, b = 0.0;
                if (std::holds_alternative<int64_t>(elems[i]))
                    a = static_cast<double>(std::get<int64_t>(elems[i]));
                else if (std::holds_alternative<double>(elems[i]))
                    a = std::get<double>(elems[i]);
                else
                    return nullptr;
                if (std::holds_alternative<int64_t>(maxVal))
                    b = static_cast<double>(std::get<int64_t>(maxVal));
                else if (std::holds_alternative<double>(maxVal))
                    b = std::get<double>(maxVal);
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
            if (std::holds_alternative<int64_t>(arguments[0]))
                minD = static_cast<double>(std::get<int64_t>(arguments[0]));
            else if (std::holds_alternative<double>(arguments[0]))
                minD = std::get<double>(arguments[0]);
            else return nullptr;
            if (std::holds_alternative<int64_t>(arguments[1]))
                maxD = static_cast<double>(std::get<int64_t>(arguments[1]));
            else if (std::holds_alternative<double>(arguments[1]))
                maxD = std::get<double>(arguments[1]);
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

            if (std::holds_alternative<int64_t>(arguments[0]))
                minVal = static_cast<double>(std::get<int64_t>(arguments[0]));
            else if (std::holds_alternative<double>(arguments[0]))
                minVal = std::get<double>(arguments[0]);
            else return nullptr;

            if (std::holds_alternative<int64_t>(arguments[1]))
                maxVal = static_cast<double>(std::get<int64_t>(arguments[1]));
            else if (std::holds_alternative<double>(arguments[1]))
                maxVal = std::get<double>(arguments[1]);
            else return nullptr;

            if (std::holds_alternative<int64_t>(arguments[2]))
                decimals = static_cast<int>(std::get<int64_t>(arguments[2]));
            else if (std::holds_alternative<double>(arguments[2]))
                decimals = static_cast<int>(std::get<double>(arguments[2]));
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

namespace {

// JSON parser state
struct JsonParser {
    const std::string& src;
    size_t pos;

    explicit JsonParser(const std::string& s) : src(s), pos(0) {}

    void skipWhitespace() {
        while (pos < src.size() &&
               (src[pos] == ' ' || src[pos] == '\t' ||
                src[pos] == '\n' || src[pos] == '\r')) {
            pos++;
        }
    }

    char peek() const { return pos < src.size() ? src[pos] : '\0'; }
    char next() { return pos < src.size() ? src[pos++] : '\0'; }

    Value parseValue();

    Value parseString() {
        if (next() != '"') return nullptr;  // skip opening quote
        std::string result;
        while (pos < src.size()) {
            char c = next();
            if (c == '"') {
                return GcHeap::instance().alloc<GcString>(result);
            }
            if (c == '\\') {
                c = next();
                switch (c) {
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/';  break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        // Parse \uXXXX — extract BMP codepoint as UTF-8
                        std::string hex;
                        for (int i = 0; i < 4 && pos < src.size(); i++)
                            hex += next();
                        try {
                            unsigned long cp = std::stoul(hex, nullptr, 16);
                            if (cp < 0x80) {
                                result += static_cast<char>(cp);
                            } else if (cp < 0x800) {
                                result += static_cast<char>(0xC0 | (cp >> 6));
                                result += static_cast<char>(0x80 | (cp & 0x3F));
                            } else {
                                result += static_cast<char>(0xE0 | (cp >> 12));
                                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                result += static_cast<char>(0x80 | (cp & 0x3F));
                            }
                        } catch (...) { return nullptr; }
                        break;
                    }
                    default: return nullptr;
                }
            } else {
                result += c;
            }
        }
        return nullptr;  // unterminated string
    }

    Value parseNumber() {
        size_t start = pos;
        if (peek() == '-') pos++;
        if (peek() < '0' || peek() > '9') return nullptr;
        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
            pos++;
        bool isFloat = false;
        if (pos < src.size() && src[pos] == '.') {
            isFloat = true;
            pos++;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
                pos++;
        }
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            isFloat = true;
            pos++;
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) pos++;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
                pos++;
        }
        std::string numStr = src.substr(start, pos - start);
        try {
            if (isFloat) {
                return std::stod(numStr);
            } else {
                return static_cast<int64_t>(std::stoll(numStr));
            }
        } catch (...) { return nullptr; }
    }

    Value parseArray() {
        next();  // skip '['
        auto arr = GcHeap::instance().alloc<Array>();
        skipWhitespace();
        if (peek() == ']') { next(); return arr; }
        while (true) {
            skipWhitespace();
            Value elem = parseValue();
            if (std::holds_alternative<std::nullptr_t>(elem) && peek() != 'n')
                return nullptr;
            arr->elements.push_back(elem);
            skipWhitespace();
            if (peek() == ']') { next(); return arr; }
            if (peek() != ',') return nullptr;
            next();  // skip ','
        }
    }

    Value parseObject() {
        next();  // skip '{'
        auto dict = GcHeap::instance().alloc<Dict>();
        skipWhitespace();
        if (peek() == '}') { next(); return dict; }
        while (true) {
            skipWhitespace();
            Value keyVal = parseString();
            if (!std::holds_alternative<GcPtr<GcString>>(keyVal))
                return nullptr;
            std::string key = std::get<GcPtr<GcString>>(keyVal)->value;
            skipWhitespace();
            if (next() != ':') return nullptr;
            skipWhitespace();
            Value val = parseValue();
            if (std::holds_alternative<std::nullptr_t>(val) && peek() != 'n')
                return nullptr;
            dict->pairs[key] = val;
            skipWhitespace();
            if (peek() == '}') { next(); return dict; }
            if (peek() != ',') return nullptr;
            next();  // skip ','
        }
    }
};

Value JsonParser::parseValue() {
    skipWhitespace();
    char c = peek();
    if (c == '"') return parseString();
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == 't') {
        if (src.substr(pos, 4) == "true") { pos += 4; return true; }
        return nullptr;
    }
    if (c == 'f') {
        if (src.substr(pos, 5) == "false") { pos += 5; return false; }
        return nullptr;
    }
    if (c == 'n') {
        if (src.substr(pos, 4) == "null") { pos += 4; return nullptr; }
        return nullptr;
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
        return parseNumber();
    }
    return nullptr;
}

// JSON stringify — serialize a Value to JSON string
std::string jsonStringifyValue(const Value& val) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            std::string s = std::to_string(arg);
            // Remove trailing zeros: "3.140000" → "3.14"
            if (s.find('.') != std::string::npos) {
                while (s.size() > 1 && s.back() == '0') s.pop_back();
                if (s.back() == '.') s.pop_back();
            }
            return s;
        } else if constexpr (std::is_same_v<T, GcPtr<GcString>>) {
            std::string result = "\"";
            for (char c : arg->value) {
                switch (c) {
                    case '"':  result += "\\\""; break;
                    case '\\': result += "\\\\"; break;
                    case '\n': result += "\\n";  break;
                    case '\r': result += "\\r";  break;
                    case '\t': result += "\\t";  break;
                    default:   result += c;       break;
                }
            }
            return result + "\"";
        } else if constexpr (std::is_same_v<T, GcPtr<Array>>) {
            std::string result = "[";
            for (size_t i = 0; i < arg->elements.size(); i++) {
                if (i > 0) result += ",";
                result += jsonStringifyValue(arg->elements[i]);
            }
            return result + "]";
        } else if constexpr (std::is_same_v<T, GcPtr<Dict>>) {
            std::string result = "{";
            bool first = true;
            for (const auto& [k, v] : arg->pairs) {
                if (!first) result += ",";
                first = false;
                result += "\"" + k + "\":" + jsonStringifyValue(v);
            }
            return result + "}";
        } else {
            return "null";  // functions, objects, etc.
        }
    }, val);
}

} // anonymous namespace

void registerJsonBuiltins(VM& vm) {
    // jsonParse(str) — parse a JSON string into a Vora value
    vm.defineNative("jsonParse", 1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!std::holds_alternative<GcPtr<GcString>>(arguments[0])) {
                return nullptr;
            }
            const auto& str = std::get<GcPtr<GcString>>(arguments[0])->value;
            JsonParser parser(str);
            Value result = parser.parseValue();
            if (std::holds_alternative<std::nullptr_t>(result) &&
                !str.empty() && str != "null") {
                // nullptr could mean parsing "null" or an error.
                // If the top-level string wasn't "null", it's an error.
                // Let parseValue handle "null" detection and check if
                // we consumed all whitespace + valid JSON.
                parser.skipWhitespace();
                if (parser.pos < str.size()) return nullptr;  // trailing garbage
            }
            // For the "null" case, re-parse with a clean check
            parser.pos = 0;
            result = parser.parseValue();
            parser.skipWhitespace();
            if (parser.pos < str.size()) return nullptr;  // trailing garbage
            return result;
        });

    // jsonStringify(value) — serialize a Vora value to JSON string
    vm.defineNative("jsonStringify", 1,
        [](const std::vector<Value>& arguments) -> Value {
            std::string json = jsonStringifyValue(arguments[0]);
            return GcHeap::instance().alloc<GcString>(json);
        });
}

} // namespace vora
