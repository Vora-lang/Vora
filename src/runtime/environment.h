#pragma once

#include <string>
#include <unordered_map>

#include "value.h"

namespace vora {

class Environment {
public:

    void define(
        const std::string& name,
        const Value& value
    );

    Value get(
        const std::string& name
    );

    void assign(
        const std::string& name,
        const Value& value
    );

private:

    std::unordered_map<
        std::string,
        Value
    > values;
};

}
