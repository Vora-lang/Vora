#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace vora {

class Callable;
struct Array;

using Value = std::variant<
    std::nullptr_t,
    double,
    bool,
    std::string,
    std::shared_ptr<Array>,
    std::shared_ptr<Callable>
>;

struct Array {
    std::vector<Value> elements;
};

void printValue(const Value& value);

}
