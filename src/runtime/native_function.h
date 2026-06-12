#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "callable.h"

namespace vora {

class ObjectInstance;
struct FunctionPrototype;

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

    Value call(const std::vector<Value>& arguments) override;

    const std::string& name() const;

    int arity() const;

    void setClassDef(std::shared_ptr<ObjectClass> cd) { classDef_ = std::move(cd); }

    std::shared_ptr<ObjectClass> getClassDef() const override { return classDef_; }

    // Bound method support: marks this NativeFunction as a method bound
    // to a specific instance. callValue() handles these without a temp VM so
    // that globals and builtins remain accessible.
    void markAsBoundMethod(std::shared_ptr<ObjectInstance> instance,
                           const FunctionPrototype* methodProto);
    bool isBoundMethod() const { return isBoundMethod_; }
    const std::shared_ptr<ObjectInstance>& getBoundInstance() const { return boundInstance_; }
    const FunctionPrototype* getBoundMethodProto() const { return boundMethodProto_; }

private:
    std::string name_;

    int arity_;

    NativeFn function_;

    std::shared_ptr<ObjectClass> classDef_;

    // Bound method state (for VM method dispatch)
    bool isBoundMethod_ = false;
    std::shared_ptr<ObjectInstance> boundInstance_;
    const FunctionPrototype* boundMethodProto_ = nullptr;
};

}
