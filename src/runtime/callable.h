#pragma once

#include <vector>

#include "../gc/gc_object.h"
#include "../gc/gc_ptr.h"
#include "value.h"

namespace vora {

class Callable : public GcObject {
public:
    virtual Value call(const std::vector<Value>& arguments) = 0;

    // Returns the ClassDefinition for constructor callables (used by inheritance).
    virtual GcPtr<ClassDefinition> getClassDef() const { return GcPtr<ClassDefinition>(); }
};

} // namespace vora
