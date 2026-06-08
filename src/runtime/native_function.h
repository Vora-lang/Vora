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

    // VM-friendly call without Interpreter& dependency.
    Value callDirectly(const std::vector<Value>& arguments) const;

    const std::string& name() const;

    int arity() const;

    void setClassDef(std::shared_ptr<ObjectClass> cd) { classDef_ = std::move(cd); }

    std::shared_ptr<ObjectClass> getClassDef() const override { return classDef_; }

private:
    std::string name_;

    int arity_;

    NativeFn function_;

    std::shared_ptr<ObjectClass> classDef_;
};

}
