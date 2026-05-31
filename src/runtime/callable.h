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

    // Returns the ObjectClass for constructor callables (used by inheritance).
    // Returns nullptr for non-constructor callables.
    virtual std::shared_ptr<ObjectClass> getClassDef() const { return nullptr; }
};

using CallablePtr = std::shared_ptr<Callable>;

}
