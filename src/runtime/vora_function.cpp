#include "vora_function.h"

#include <cassert>
#include "../vm/compiler.h"  // for FunctionPrototype

namespace vora {

VoraFunction::VoraFunction(
    std::string name,
    int arity,
    int requiredArity,
    const FunctionPrototype* prototype
)
    : name_(std::move(name)),
      arity_(arity),
      requiredArity_(requiredArity),
      prototype_(prototype) {
}

Value VoraFunction::call(const std::vector<Value>& /*arguments*/) {
    // VoraFunction is never called through the Callable interface —
    // the VM dispatches it directly via callVoraFunction() which sets
    // up call frames and upvalues. If this stub is ever reached, it
    // indicates a bug in the VM dispatch.
    assert(false && "VoraFunction::call() should never be invoked directly");
    return nullptr;
}

const std::string& VoraFunction::name() const {
    return name_;
}

void VoraFunction::trace(std::vector<GcObject*>& wl) {
    // Trace the prototype so it doesn't get collected while
    // this VoraFunction is reachable (via generators, closures, etc.)
    if (prototype_) wl.push_back(const_cast<FunctionPrototype*>(prototype_));
}

}
