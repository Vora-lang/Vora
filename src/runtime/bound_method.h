#pragma once

#include "callable.h"
#include "../gc/gc_ptr.h"

#include <vector>

namespace vora {

class NativeFunction;
class ObjectInstance;
struct FunctionPrototype;   // defined in compiler.h
class VoraFunction;

// BoundMethod — a method bound to a specific object instance.
// Created by OP_GET_PROPERTY / OP_GET_SUPER when looking up a method
// on an ObjectInstance. The VM's callValue() intercepts BoundMethod
// before calling call() — it sets up the call frame with `this` at
// slot 0 and dispatches to the underlying VoraFunction.
class BoundMethod : public Callable {
public:
    BoundMethod(GcPtr<ObjectInstance> instance,
                const FunctionPrototype* methodProto,
                GcPtr<VoraFunction> methodFunc);

    // Never invoked directly — VM intercepts in callValue().
    Value call(const std::vector<Value>& arguments) override;

    GcPtr<ObjectInstance> getInstance() const { return instance_; }
    const FunctionPrototype* getMethodProto() const { return methodProto_; }
    GcPtr<VoraFunction> getMethodFunc() const { return methodFunc_; }

    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override;

private:
    GcPtr<ObjectInstance> instance_;
    const FunctionPrototype* methodProto_;
    GcPtr<VoraFunction> methodFunc_;
};

} // namespace vora
