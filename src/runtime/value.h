#pragma once

#include <cstddef>
#include <string>
#include <variant>

namespace vora {

using Value = std::variant<
    std::nullptr_t,
    double,
    bool,
    std::string
>;

void printValue(const Value& value);

}
