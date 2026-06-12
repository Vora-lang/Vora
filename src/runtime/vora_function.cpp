#include "vora_function.h"

#include "../vm/compiler.h"  // for FunctionPrototype

namespace vora {

VoraFunction::VoraFunction(
    std::string name,
    int arity,
    const FunctionPrototype* prototype
)
    : name_(std::move(name)),
      arity_(arity),
      prototype_(prototype) {
}

Value VoraFunction::call(const std::vector<Value>& /*arguments*/) {
    // VoraFunction is never called through the Callable interface —
    // the VM dispatches it directly via callVoraFunction() which sets
    // up call frames and upvalues. This stub returns null.
    return nullptr;
}

const std::string& VoraFunction::name() const {
    return name_;
}

}
