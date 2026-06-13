#include "value_ops.h"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

namespace vora {

// =========================================================================
// isTruthy
// =========================================================================

bool isTruthy(const Value& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) return false;
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value);
    if (std::holds_alternative<int64_t>(value)) return std::get<int64_t>(value) != 0;
    if (std::holds_alternative<double>(value)) return std::get<double>(value) != 0.0;
    if (std::holds_alternative<std::string>(value)) return !std::get<std::string>(value).empty();
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
    if (std::holds_alternative<std::string>(a)) return std::get<std::string>(a) == std::get<std::string>(b);

    if (std::holds_alternative<std::shared_ptr<Array>>(a))
        return std::get<std::shared_ptr<Array>>(a) == std::get<std::shared_ptr<Array>>(b);
    if (std::holds_alternative<std::shared_ptr<Dict>>(a))
        return std::get<std::shared_ptr<Dict>>(a) == std::get<std::shared_ptr<Dict>>(b);
    if (std::holds_alternative<std::shared_ptr<Callable>>(a))
        return std::get<std::shared_ptr<Callable>>(a) == std::get<std::shared_ptr<Callable>>(b);
    if (std::holds_alternative<std::shared_ptr<ObjectInstance>>(a))
        return std::get<std::shared_ptr<ObjectInstance>>(a) == std::get<std::shared_ptr<ObjectInstance>>(b);
    if (std::holds_alternative<std::shared_ptr<FunctionPrototype>>(a))
        return std::get<std::shared_ptr<FunctionPrototype>>(a) == std::get<std::shared_ptr<FunctionPrototype>>(b);
    if (std::holds_alternative<std::shared_ptr<ClassData>>(a))
        return std::get<std::shared_ptr<ClassData>>(a) == std::get<std::shared_ptr<ClassData>>(b);

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

    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return std::get<std::string>(a) + std::get<std::string>(b);
    }

    if (std::holds_alternative<std::string>(a)) {
        return std::get<std::string>(a) + valueToString(b);
    }
    if (std::holds_alternative<std::string>(b)) {
        return valueToString(a) + std::get<std::string>(b);
    }

    if (std::holds_alternative<std::shared_ptr<Array>>(a) && std::holds_alternative<std::shared_ptr<Array>>(b)) {
        auto result = std::make_shared<Array>();
        const auto& leftArr = std::get<std::shared_ptr<Array>>(a)->elements;
        const auto& rightArr = std::get<std::shared_ptr<Array>>(b)->elements;
        result->elements.reserve(leftArr.size() + rightArr.size());
        result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
        result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
        return result;
    }

    if (std::holds_alternative<std::shared_ptr<Dict>>(a) && std::holds_alternative<std::shared_ptr<Dict>>(b)) {
        auto result = std::make_shared<Dict>();
        const auto& leftDict = std::get<std::shared_ptr<Dict>>(a)->pairs;
        const auto& rightDict = std::get<std::shared_ptr<Dict>>(b)->pairs;
        result->pairs = leftDict;
        for (const auto& [k, v] : rightDict) {
            result->pairs[k] = v;  // right overwrites left
        }
        return result;
    }

    if (std::holds_alternative<std::shared_ptr<Array>>(a)) {
        auto result = std::make_shared<Array>();
        const auto& leftArr = std::get<std::shared_ptr<Array>>(a)->elements;
        result->elements.reserve(leftArr.size() + 1);
        result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
        result->elements.push_back(b);
        return result;
    }

    if (std::holds_alternative<std::shared_ptr<Array>>(b)) {
        auto result = std::make_shared<Array>();
        const auto& rightArr = std::get<std::shared_ptr<Array>>(b)->elements;
        result->elements.reserve(1 + rightArr.size());
        result->elements.push_back(a);
        result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
        return result;
    }

    error = true;
    return nullptr;
}

} // namespace vora
