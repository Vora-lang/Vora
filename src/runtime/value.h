#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <map>

namespace vora {

class Callable;
class VoraFunction;
class BlockStmt;
struct Array;
struct ObjectInstance;
struct FunctionPrototype;
struct ClassData;

using Value = std::variant<
    std::nullptr_t,
    double,
    bool,
    std::string,
    std::shared_ptr<Array>,
    std::shared_ptr<Callable>,
    std::shared_ptr<ObjectInstance>,
    std::shared_ptr<FunctionPrototype>,
    std::shared_ptr<ClassData>
>;

struct Array {
    std::vector<Value> elements;
};

struct ObjectClass {
    std::string className;
    std::vector<std::string> params;
    std::shared_ptr<BlockStmt> body;
    std::map<std::string, std::shared_ptr<VoraFunction>> methods;
    std::string parentClassName;              // empty = no parent
    std::shared_ptr<ObjectClass> parentClass;  // resolved at runtime
    std::shared_ptr<FunctionPrototype> ctorProto;  // compiled constructor
};

struct ObjectInstance {
    std::string className;
    std::map<std::string, Value> properties;
    std::shared_ptr<ObjectClass> classDefinition;
};

void printValue(const Value& value);

std::string valueToString(const Value& value);

}
