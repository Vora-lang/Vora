#include "value_ops.h"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

#include "../gc/gc_heap.h"

namespace vora {

// =========================================================================
// isTruthy
//
// Python-style truthiness:
//   falsy: null, false, 0, 0.0, "", [], {}
//   truthy: everything else (including non-empty arrays/dicts, objects, etc.)
// =========================================================================

bool isTruthy(const Value& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) return false;
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value);
    if (std::holds_alternative<int64_t>(value)) return std::get<int64_t>(value) != 0;
    if (std::holds_alternative<double>(value)) return std::get<double>(value) != 0.0;
    if (std::holds_alternative<GcPtr<GcString>>(value)) return !std::get<GcPtr<GcString>>(value)->value.empty();
    if (std::holds_alternative<GcPtr<Array>>(value)) return !std::get<GcPtr<Array>>(value)->elements.empty();
    if (std::holds_alternative<GcPtr<Dict>>(value)) return !std::get<GcPtr<Dict>>(value)->pairs.empty();
    if (std::holds_alternative<GcPtr<Set>>(value)) return !std::get<GcPtr<Set>>(value)->elements.empty();
    if (std::holds_alternative<GcPtr<Map>>(value)) return !std::get<GcPtr<Map>>(value)->pairs.empty();
    return true;
}

// =========================================================================
// valuesEqual
// =========================================================================

bool valuesEqual(const Value& a, const Value& b) {
    // Cross-type numeric equality: 42 == 42.0
    if (isNumeric(a) && isNumeric(b))
        return toDouble(a) == toDouble(b);

    if (a.index() != b.index()) return false;

    if (std::holds_alternative<std::nullptr_t>(a)) return true;
    if (std::holds_alternative<bool>(a)) return std::get<bool>(a) == std::get<bool>(b);
    if (std::holds_alternative<GcPtr<GcString>>(a)) return std::get<GcPtr<GcString>>(a)->value == std::get<GcPtr<GcString>>(b)->value;

    if (std::holds_alternative<GcPtr<Array>>(a))
        return std::get<GcPtr<Array>>(a) == std::get<GcPtr<Array>>(b);
    if (std::holds_alternative<GcPtr<Dict>>(a))
        return std::get<GcPtr<Dict>>(a) == std::get<GcPtr<Dict>>(b);
    if (std::holds_alternative<GcPtr<Callable>>(a))
        return std::get<GcPtr<Callable>>(a) == std::get<GcPtr<Callable>>(b);
    if (std::holds_alternative<GcPtr<ObjectInstance>>(a))
        return std::get<GcPtr<ObjectInstance>>(a) == std::get<GcPtr<ObjectInstance>>(b);
    if (std::holds_alternative<GcPtr<FunctionPrototype>>(a))
        return std::get<GcPtr<FunctionPrototype>>(a) == std::get<GcPtr<FunctionPrototype>>(b);
    if (std::holds_alternative<GcPtr<ClassDefinition>>(a))
        return std::get<GcPtr<ClassDefinition>>(a) == std::get<GcPtr<ClassDefinition>>(b);
    if (std::holds_alternative<GcPtr<Iterator>>(a))
        return std::get<GcPtr<Iterator>>(a) == std::get<GcPtr<Iterator>>(b);
    if (std::holds_alternative<GcPtr<Generator>>(a))
        return std::get<GcPtr<Generator>>(a) == std::get<GcPtr<Generator>>(b);
    if (std::holds_alternative<GcPtr<Set>>(a))
        return std::get<GcPtr<Set>>(a) == std::get<GcPtr<Set>>(b);
    if (std::holds_alternative<GcPtr<Map>>(a))
        return std::get<GcPtr<Map>>(a) == std::get<GcPtr<Map>>(b);

    return false;
}

// =========================================================================
// valuesCompare
// =========================================================================

int valuesCompare(const Value& a, const Value& b) {
    if (!isNumeric(a) || !isNumeric(b)) {
        return 0;
    }
    double da = toDouble(a);
    double db = toDouble(b);
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

// =========================================================================
// addValues
// =========================================================================

Value addValues(const Value& a, const Value& b, bool& error) {
    error = false;

    // int + int → int (with overflow detection); otherwise promote to double
    if (isNumeric(a) && isNumeric(b)) {
        if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
            int64_t av = std::get<int64_t>(a);
            int64_t bv = std::get<int64_t>(b);
            // Detect signed overflow: (av > 0 && bv > INT64_MAX - av) ||
            //                       (av < 0 && bv < INT64_MIN - av)
            if ((bv > 0 && av > INT64_MAX - bv) ||
                (bv < 0 && av < INT64_MIN - bv)) {
                return static_cast<double>(av) + static_cast<double>(bv);
            }
            return av + bv;
        }
        return toDouble(a) + toDouble(b);
    }

    if (std::holds_alternative<GcPtr<GcString>>(a) && std::holds_alternative<GcPtr<GcString>>(b)) {
        // Single allocation: copy a, then append b in-place
        std::string result = std::get<GcPtr<GcString>>(a)->value;
        result += std::get<GcPtr<GcString>>(b)->value;
        return GcHeap::instance().alloc<GcString>(std::move(result));
    }

    if (std::holds_alternative<GcPtr<GcString>>(a)) {
        // Single allocation: copy a, then stream-append b
        std::string result = std::get<GcPtr<GcString>>(a)->value;
        valueToStringAppend(result, b);
        return GcHeap::instance().alloc<GcString>(std::move(result));
    }
    if (std::holds_alternative<GcPtr<GcString>>(b)) {
        // Two-alloc tradeoff: stringify a, then append b (order matters for semantics)
        // We stream a into a fresh string, then append b's raw value
        std::string result;
        valueToStringAppend(result, a);
        result += std::get<GcPtr<GcString>>(b)->value;
        return GcHeap::instance().alloc<GcString>(std::move(result));
    }

    if (std::holds_alternative<GcPtr<Array>>(a) && std::holds_alternative<GcPtr<Array>>(b)) {
        auto result = GcHeap::instance().alloc<Array>();
        const auto& leftArr = std::get<GcPtr<Array>>(a)->elements;
        const auto& rightArr = std::get<GcPtr<Array>>(b)->elements;
        result->elements.reserve(leftArr.size() + rightArr.size());
        result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
        result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
        return result;
    }

    if (std::holds_alternative<GcPtr<Dict>>(a) && std::holds_alternative<GcPtr<Dict>>(b)) {
        auto result = GcHeap::instance().alloc<Dict>();
        const auto& leftDict = std::get<GcPtr<Dict>>(a)->pairs;
        const auto& rightDict = std::get<GcPtr<Dict>>(b)->pairs;
        result->pairs = leftDict;
        for (const auto& [k, v] : rightDict) {
            result->pairs[k] = v;  // right overwrites left
        }
        return result;
    }

    if (std::holds_alternative<GcPtr<Set>>(a) && std::holds_alternative<GcPtr<Set>>(b)) {
        auto result = GcHeap::instance().alloc<Set>();
        result->elements = std::get<GcPtr<Set>>(a)->elements;
        for (const auto& e : std::get<GcPtr<Set>>(b)->elements) {
            result->elements.insert(e);
        }
        return result;
    }

    if (std::holds_alternative<GcPtr<Map>>(a) && std::holds_alternative<GcPtr<Map>>(b)) {
        auto result = GcHeap::instance().alloc<Map>();
        result->pairs = std::get<GcPtr<Map>>(a)->pairs;
        for (const auto& [k, v] : std::get<GcPtr<Map>>(b)->pairs) {
            result->pairs[k] = v;  // right overwrites left
        }
        return result;
    }

    if (std::holds_alternative<GcPtr<Array>>(a)) {
        auto result = GcHeap::instance().alloc<Array>();
        const auto& leftArr = std::get<GcPtr<Array>>(a)->elements;
        result->elements.reserve(leftArr.size() + 1);
        result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
        result->elements.push_back(b);
        return result;
    }

    if (std::holds_alternative<GcPtr<Array>>(b)) {
        auto result = GcHeap::instance().alloc<Array>();
        const auto& rightArr = std::get<GcPtr<Array>>(b)->elements;
        result->elements.reserve(1 + rightArr.size());
        result->elements.push_back(a);
        result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
        return result;
    }

    error = true;
    return nullptr;
}

} // namespace vora
