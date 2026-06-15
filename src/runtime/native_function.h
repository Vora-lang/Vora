#pragma once

#include <functional>
#include <string>
#include <vector>

#include "callable.h"

namespace vora {

// NativeFunction — a pure native (C++) function callable from Vora.
// No class constructor or bound method state — those have been split
// into ClassConstructor and BoundMethod respectively.
class NativeFunction : public Callable {
public:
    using NativeFn = std::function<Value(const std::vector<Value>&)>;

    NativeFunction(std::string name, int arity, NativeFn function);

    Value call(const std::vector<Value>& arguments) override;

    const std::string& name() const;
    int arity() const;

    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override;

private:
    std::string name_;
    int arity_;
    NativeFn function_;
};

} // namespace vora
