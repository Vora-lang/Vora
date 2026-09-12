#include "value_ops.h"

#include <cstdint>
#include <memory>
#include <string>

#include "../gc/gc_heap.h"

namespace vora {

// =========================================================================
// Exact integer arithmetic
//
// Integers are a hybrid of inline Int and heap BigInt.  These helpers keep that
// split invisible to the operators: each accepts either representation, computes
// exactly, and returns a Value that is inline whenever the result fits — which
// is what stops a value from acquiring two different identities (and so two
// different dict keys) depending on how it was produced.
//
// Every helper requires both operands to be integers; callers must have checked
// isNumeric() and excluded the double case first.
// =========================================================================

namespace {

/// @brief Convert an integer Value (inline or boxed) to BigInt.
BigInt toBigInt(const Value& v) {
    if (v.isBigInt()) return v.asBigInt()->value;
    return BigInt::fromInt64(v.asInt());
}

/// @brief Box a BigInt result, which demotes to inline when it fits.
Value fromBigInt(BigInt b) {
    if (b.fitsInt64() && b.toInt64() <= INT47_MAX && b.toInt64() >= INT47_MIN) {
        return Value(b.toInt64());
    }
    return Value(GcHeap::instance().alloc<GcBigInt>(std::move(b)));
}

} // namespace

Value intAddExact(const Value& a, const Value& b) {
    if (a.isInt() && b.isInt()) {
        // Both inline: each is within ±2^45, so the sum cannot overflow int64.
        return Value(a.asInt() + b.asInt());
    }
    return fromBigInt(BigInt::add(toBigInt(a), toBigInt(b)));
}

Value intSubExact(const Value& a, const Value& b) {
    if (a.isInt() && b.isInt()) {
        return Value(a.asInt() - b.asInt());
    }
    return fromBigInt(BigInt::sub(toBigInt(a), toBigInt(b)));
}

Value intMulExact(const Value& a, const Value& b) {
    if (a.isInt() && b.isInt()) {
        // The product of two inline ints can exceed int64, so widen first —
        // multiplying in int64 would be signed overflow (undefined behaviour).
        const int64_t ai = a.asInt();
        const int64_t bi = b.asInt();
        if (ai == 0 || bi == 0) return Value(static_cast<int64_t>(0));
        const __int128 product = static_cast<__int128>(ai) * static_cast<__int128>(bi);
        if (product >= INT64_MIN && product <= INT64_MAX) {
            return Value(static_cast<int64_t>(product));
        }
        return fromBigInt(BigInt::mul(BigInt::fromInt64(ai), BigInt::fromInt64(bi)));
    }
    return fromBigInt(BigInt::mul(toBigInt(a), toBigInt(b)));
}

Value intModExact(const Value& a, const Value& b) {
    // Truncated remainder (sign follows the dividend), matching C `%` and so
    // matching what Vora's inline-integer `%` already did.
    if (a.isInt() && b.isInt()) {
        const int64_t ai = a.asInt();
        const int64_t bi = b.asInt();
        if (ai == INT64_MIN && bi == -1) {
            return Value(static_cast<int64_t>(0));
        }
        return Value(ai % bi);
    }
    BigInt q, r;
    if (!BigInt::divModTrunc(toBigInt(a), toBigInt(b), q, r)) {
        return Value(nullptr);
    }
    return fromBigInt(std::move(r));
}

Value intNegateExact(const Value& a) {
    if (a.isInt()) {
        // -(-2^45) is 2^45, which is out of inline range, so this can box.
        return Value(-a.asInt());
    }
    return fromBigInt(BigInt::negate(toBigInt(a)));
}

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
    if (value.isBigInt()) return !value.asBigInt()->value.isZero();
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
    // Cross-type numeric equality: 42 == 42.0.  Exact, so that 2^70 and
    // 2^70 + 1 never compare equal merely because both round to one double.
    if (isNumeric(a) && isNumeric(b))
        return numericValuesEqual(a, b);

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
    return numericValuesCompare(a, b);
}

// =========================================================================
// addValues
// =========================================================================

Value addValues(const Value& a, const Value& b, bool& error) {
    error = false;

    // int + int → int (exact, boxing only when the sum leaves inline range);
    // any float operand keeps the historical double result.
    if (isNumeric(a) && isNumeric(b)) {
        if (!a.isDouble() && !b.isDouble()) {
            return intAddExact(a, b);
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
