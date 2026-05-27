#pragma once

#include <functional>
#include <string>
#include <vector>

#include "callable.h"

namespace vora {

class Interpreter;

class NativeFunction : public Callable {
public:
    using NativeFn = std::function<
        Value(const std::vector<Value>&)
    >;

    NativeFunction(
        std::string name,
        int arity,
        NativeFn function
    );

    Value call(
        Interpreter& interpreter,
        const std::vector<Value>& arguments
    ) override;

    const std::string& name() const;

    int arity() const;

private:
    std::string name_;

    int arity_;

    NativeFn function_;
};

}
