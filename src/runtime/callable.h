#pragma once

#include <memory>
#include <vector>

#include "value.h"

namespace vora {

class Interpreter;

class Callable {
public:
    virtual ~Callable() = default;

    virtual Value call(
        Interpreter& interpreter,
        const std::vector<Value>& arguments
    ) = 0;
};

using CallablePtr = std::shared_ptr<Callable>;

}
