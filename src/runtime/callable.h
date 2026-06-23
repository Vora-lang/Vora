#pragma once

#include <vector>

#include "../gc/gc_object.h"
#include "../gc/gc_ptr.h"
#include "value.h"

namespace vora {

class VM;

class Callable : public GcObject {
public:
    virtual Value call(const std::vector<Value>& arguments) = 0;

    // Call with access to the calling VM (enables reading/modifying globals).
    // Default implementation delegates to call(arguments).
    // Override in ClassConstructor and any future callable that needs VM context.
    virtual Value call(VM& vm, const std::vector<Value>& arguments) {
        (void)vm;
        return call(arguments);
    }

    // Returns the ClassDefinition for constructor callables (used by inheritance).
    virtual GcPtr<ClassDefinition> getClassDef() const { return GcPtr<ClassDefinition>(); }
};

} // namespace vora
