#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <map>

namespace vora {

class Callable;
class VoraFunction;
class BlockStmt;
struct Array;
struct Dict;
struct ObjectInstance;
struct FunctionPrototype;
struct ClassData;

using Value = std::variant<
    std::nullptr_t,
    double,
    int64_t,
    bool,
    std::string,
    std::shared_ptr<Array>,
    std::shared_ptr<Dict>,
    std::shared_ptr<Callable>,
    std::shared_ptr<ObjectInstance>,
    std::shared_ptr<FunctionPrototype>,
    std::shared_ptr<ClassData>
>;

struct Array {
    std::vector<Value> elements;
};

struct Dict {
    std::unordered_map<std::string, Value> pairs;
};

struct ObjectClass {
    std::string className;
    std::vector<std::string> params;
    std::shared_ptr<BlockStmt> body;
    std::map<std::string, std::shared_ptr<VoraFunction>> methods;
    std::vector<std::string> parentClassNames;                      // names of parent classes
    std::vector<std::weak_ptr<ObjectClass>> parentClasses;          // resolved parent classes
    std::vector<std::weak_ptr<ObjectClass>> mro;                    // C3 linearization cache (self + parents in MRO order)
    std::shared_ptr<FunctionPrototype> ctorProto;                   // compiled constructor
};

struct ObjectInstance {
    std::string className;
    std::map<std::string, Value> properties;
    std::shared_ptr<ObjectClass> classDefinition;
};

void printValue(const Value& value);

std::string valueToString(const Value& value);

// Numeric helpers for dual int64/double type system
inline bool isNumeric(const Value& v) {
    return std::holds_alternative<double>(v) || std::holds_alternative<int64_t>(v);
}

inline double toDouble(const Value& v) {
    // Safe: returns 0.0 for non-numeric types instead of throwing.
    if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    return 0.0;  // non-numeric: bool, string, array, etc.
}

inline Value promoteToFloat(const Value& v) {
    if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
    return v;
}

}
