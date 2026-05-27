#pragma once

#include <memory>
#include <string>
#include <variant>

namespace vora {

class Callable;

using Value = std::variant<
    std::nullptr_t,
    double,
    bool,
    std::string,
    std::shared_ptr<Callable>
>;

void printValue(const Value& value);

}
