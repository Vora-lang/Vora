#include "vora_function.h"

#include "../vm/compiler.h"  // for FunctionPrototype
#include "runtime_error.h"

namespace vora {

VoraFunction::VoraFunction(
    std::string name,
    int arity,
    int requiredArity,
    bool hasRest,
    const FunctionPrototype* prototype
)
    : name_(std::move(name)),
      arity_(arity),
      requiredArity_(requiredArity),
      hasRest_(hasRest),
      prototype_(prototype) {
}

Value VoraFunction::call(const std::vector<Value>& /*arguments*/) {
    // VoraFunction is never called through the Callable interface —
    // the VM dispatches it directly via callVoraFunction() which sets
    // up call frames and upvalues. If this stub is ever reached, it
    // indicates a bug in the VM dispatch.
    throw RuntimeError("Internal error: VoraFunction::call() should never be invoked directly",
                       Token());
}

const std::string& VoraFunction::name() const {
    return name_;
}

void VoraFunction::trace(std::vector<GcObject*>& wl) {
    // Trace the prototype so it doesn't get collected while
    // this VoraFunction is reachable (via generators, closures, etc.)
    // const_cast is safe here: GC tracing only reads the object to find
    // further GcObject references — it never mutates the traced object.
    if (prototype_) wl.push_back(const_cast<FunctionPrototype*>(prototype_));
}

}
