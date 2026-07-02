#pragma once

/**
 * @file callable.h
 * @brief Abstract base class for all runtime-callable objects in Vora.
 *
 * Defines the Callable interface, which provides a unified invocation
 * mechanism for Vora functions, native callbacks, class constructors,
 * and bound methods.  Callable is a garbage-collected object (derives
 * from GcObject) so that its lifetime is managed by the VM's tracing
 * collector.
 */

#include <vector>

#include "../gc/gc_object.h"
#include "../gc/gc_ptr.h"
#include "value.h"

namespace vora {

class VM;

/**
 * @brief Abstract base for every object that can be invoked with
 *        function-call syntax.
 *
 * Concrete subclasses override the pure-virtual single-argument call()
 * to supply their specific invocation logic.  The two-argument overload
 * additionally receives a VM reference, making it possible to read or
 * modify global state during the call (used by ClassConstructor).
 */
class Callable : public GcObject {
public:
    /// @brief  Invoke this callable with positional arguments (no VM access).
    /// @param  arguments  Positional argument list.
    /// @return The result value produced by the invocation.
    virtual Value call(const std::vector<Value>& arguments) = 0;

    // Call with access to the calling VM (enables reading/modifying globals).
    // Default implementation delegates to call(arguments).
    // Override in ClassConstructor and any future callable that needs VM context.
    /// @brief  Invoke this callable with VM context.
    /// @param  vm         The calling Virtual Machine instance.
    /// @param  arguments  Positional argument list.
    /// @return The result value produced by the invocation.
    virtual Value call(VM& vm, const std::vector<Value>& arguments) {
        (void)vm;
        return call(arguments);
    }

    // Returns the ClassDefinition for constructor callables (used by inheritance).
    /// @brief  Retrieve the ClassDefinition bound to this callable, if any.
    /// @return A GcPtr to the ClassDefinition for constructor callables,
    ///         or an empty (null) GcPtr for non-constructor callables.
    virtual GcPtr<ClassDefinition> getClassDef() const { return GcPtr<ClassDefinition>(); }
};

} // namespace vora
