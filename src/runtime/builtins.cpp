// src/runtime/builtins.cpp — Unified built-in native function implementations
//
// Contains every built-in native function (print, type, len, clock, assert,
// int, float, range, input, bin, oct, hex) and all array/string method
// factories. This is the single source of truth shared by the VM execution
// engine, the REPL, and the C++ unit test harness.

#include "builtins.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "../gc/gc_heap.h"
#include "native_function.h"
#include "value.h"
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

} // namespace vora
