#include "value_ops.h"

#include <cstdint>
#include <memory>
#include <string>

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
    if (value.isNull()) return false;
    if (value.isBool()) return value.asBool();
    if (value.isInt()) return value.asInt() != 0;
    if (value.isDouble()) return value.asDouble() != 0.0;
    if (value.isGcString()) return !value.asGcString()->value.empty();
    if (value.isArray()) return !value.asArray()->elements.empty();
    if (value.isDict()) return !value.asDict()->pairs.empty();
    if (value.isSet()) return !value.asSet()->elements.empty();
    if (value.isMap()) return !value.asMap()->pairs.empty();
    return true;
}

// =========================================================================
// valuesEqual
// =========================================================================

bool valuesEqual(const Value& a, const Value& b) {
    // Cross-type numeric equality: 42 == 42.0
    if (isNumeric(a) && isNumeric(b))
        return toDouble(a) == toDouble(b);

    if (a.dispatchTag() != b.dispatchTag()) return false;

    if (a.isDouble()) return a.raw() == b.raw();
    if (a.isNull()) return true;
    if (a.isBool()) return a.asBool() == b.asBool();
    if (a.isInt()) return a.asInt() == b.asInt();
    if (a.isGcString()) return a.asGcString()->value == b.asGcString()->value;

    if (a.isArray())
        return a.asArray() == b.asArray();
    if (a.isDict())
        return a.asDict() == b.asDict();
    if (a.isCallable())
        return a.asCallable() == b.asCallable();
    if (a.isObjectInstance())
        return a.asObjectInstance() == b.asObjectInstance();
    if (a.isFunctionPrototype())
        return a.asFunctionPrototype() == b.asFunctionPrototype();
    if (a.isClassDefinition())
        return a.asClassDefinition() == b.asClassDefinition();
    if (a.isIterator())
        return a.asIterator() == b.asIterator();
    if (a.isGenerator())
        return a.asGenerator() == b.asGenerator();
    if (a.isSet())
        return a.asSet() == b.asSet();
    if (a.isMap())
        return a.asMap() == b.asMap();

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
        if (a.isInt() && b.isInt()) {
            int64_t av = a.asInt();
            int64_t bv = b.asInt();
            // Detect signed overflow: (av > 0 && bv > INT64_MAX - av) ||
            //                       (av < 0 && bv < INT64_MIN - av)
            if ((bv > 0 && av > INT64_MAX - bv) ||
                (bv < 0 && av < INT64_MIN - bv)) {
                return Value(static_cast<double>(av) + static_cast<double>(bv));
            }
            return Value(av + bv);
        }
        return Value(toDouble(a) + toDouble(b));
    }

    if (a.isGcString() && b.isGcString()) {
        // Single allocation: copy a, then append b in-place
        std::string result = a.asGcString()->value;
        result += b.asGcString()->value;
        return Value(GcHeap::instance().alloc<GcString>(std::move(result)));
    }

    if (a.isGcString()) {
        // Single allocation: copy a, then stream-append b
        std::string result = a.asGcString()->value;
        valueToStringAppend(result, b);
        return Value(GcHeap::instance().alloc<GcString>(std::move(result)));
    }
    if (b.isGcString()) {
        // Two-alloc tradeoff: stringify a, then append b
        std::string result;
        valueToStringAppend(result, a);
        result += b.asGcString()->value;
        return Value(GcHeap::instance().alloc<GcString>(std::move(result)));
    }

    if (a.isArray() && b.isArray()) {
        auto result = GcHeap::instance().alloc<Array>();
        const auto& leftArr = a.asArray()->elements;
        const auto& rightArr = b.asArray()->elements;
        result->elements.reserve(leftArr.size() + rightArr.size());
        result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
        result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
        return Value(result);
    }

    if (a.isDict() && b.isDict()) {
        auto result = GcHeap::instance().alloc<Dict>();
        const auto& leftDict = a.asDict()->pairs;
        const auto& rightDict = b.asDict()->pairs;
        result->pairs = leftDict;
        for (const auto& [k, v] : rightDict) {
            result->pairs[k] = v;  // right overwrites left
        }
        return Value(result);
    }

    if (a.isSet() && b.isSet()) {
        auto result = GcHeap::instance().alloc<Set>();
        result->elements = a.asSet()->elements;
        for (const auto& e : b.asSet()->elements) {
            result->elements.insert(e);
        }
        return Value(result);
    }

    if (a.isMap() && b.isMap()) {
        auto result = GcHeap::instance().alloc<Map>();
        result->pairs = a.asMap()->pairs;
        for (const auto& [k, v] : b.asMap()->pairs) {
            result->pairs[k] = v;  // right overwrites left
        }
        return Value(result);
    }

    if (a.isArray()) {
        auto result = GcHeap::instance().alloc<Array>();
        const auto& leftArr = a.asArray()->elements;
        result->elements.reserve(leftArr.size() + 1);
        result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
        result->elements.push_back(b);
        return Value(result);
    }

    if (b.isArray()) {
        auto result = GcHeap::instance().alloc<Array>();
        const auto& rightArr = b.asArray()->elements;
        result->elements.reserve(1 + rightArr.size());
        result->elements.push_back(a);
        result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
        return Value(result);
    }

    error = true;
    return Value(nullptr);
}

} // namespace vora
