#include "bound_method.h"
#include "vora_function.h"

#include "runtime_error.h"

namespace vora {

BoundMethod::BoundMethod(GcPtr<ObjectInstance> instance,
                         const FunctionPrototype* methodProto,
                         GcPtr<VoraFunction> methodFunc)
    : instance_(std::move(instance)),
      methodProto_(methodProto),
      methodFunc_(std::move(methodFunc)) {
}

Value BoundMethod::call(const std::vector<Value>& /*arguments*/) {
    // BoundMethod is never called through the Callable interface —
    // the VM intercepts it in callValue() and dispatches directly.
    throw RuntimeError("Internal error: BoundMethod::call() should never be invoked directly",
                       Token());
}

void BoundMethod::trace(std::vector<GcObject*>& wl) {
    if (instance_) wl.push_back(instance_.get());
    if (methodFunc_) wl.push_back(methodFunc_.get());
}

size_t BoundMethod::gcSize() const { return sizeof(BoundMethod); }

} // namespace vora
