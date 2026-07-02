/**
 * @file bound_method.h
 * @brief Method bound to a specific object instance.
 *
 * BoundMethod is created by OP_GET_PROPERTY / OP_GET_SUPER when a method
 * is looked up on an ObjectInstance.  It pairs the method's compiled
 * VoraFunction with the target instance so that `this` can be injected at
 * call time.
 */

#pragma once

#include "callable.h"
#include "../gc/gc_ptr.h"

#include <vector>

namespace vora {

class NativeFunction;
class ObjectInstance;
struct FunctionPrototype;   // defined in compiler.h
class VoraFunction;

/**
 * @brief A method bound to a specific object instance.
 *
 * Created by OP_GET_PROPERTY and OP_GET_SUPER when the VM looks up a
 * method on an ObjectInstance.  The VM's callValue() intercepts
 * BoundMethod before calling call() — it extracts the bound instance,
 * sets it as `this` at stack slot 0, and dispatches to the underlying
 * VoraFunction.
 *
 * call() is never invoked directly; callValue() handles BoundMethod
 * specially.
 */
class BoundMethod : public Callable {
public:
    /**
     * @brief Bind a method to an object instance.
     * @param instance    The object that `this` will refer to.
     * @param methodProto The immutable method prototype (bytecode metadata).
     * @param methodFunc  The compiled method VoraFunction.
     */
    BoundMethod(GcPtr<ObjectInstance> instance,
                const FunctionPrototype* methodProto,
                GcPtr<VoraFunction> methodFunc);

    /**
     * @brief Never invoked directly — VM intercepts in callValue().
     * @param arguments (unused).
     * @return Never returns normally (throws or asserts).
     */
    Value call(const std::vector<Value>& arguments) override;

    /** @brief Get the bound object instance (receiver / `this`). */
    GcPtr<ObjectInstance> getInstance() const { return instance_; }

    /** @brief Get the immutable method prototype. */
    const FunctionPrototype* getMethodProto() const { return methodProto_; }

    /** @brief Get the compiled method function. */
    GcPtr<VoraFunction> getMethodFunc() const { return methodFunc_; }

    /** @brief Trace GC roots: instance and methodFunc. */
    void trace(std::vector<GcObject*>& wl) override;

    /** @brief Approximate GC-tracked memory size. */
    size_t gcSize() const override;

private:
    GcPtr<ObjectInstance> instance_;
    const FunctionPrototype* methodProto_;
    GcPtr<VoraFunction> methodFunc_;
};

} // namespace vora
