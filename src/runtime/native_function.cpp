#include "native_function.h"

namespace vora {

NativeFunction::NativeFunction(
    std::string name,
    int arity,
    NativeFn function
)
    : name_(std::move(name)),
      arity_(arity),
      function_(std::move(function)) {
}

Value NativeFunction::call(const std::vector<Value>& arguments) {
    return function_(arguments);
}

const std::string& NativeFunction::name() const {
    return name_;
}

int NativeFunction::arity() const {
    return arity_;
}

void NativeFunction::trace(std::vector<GcObject*>& /*wl*/) {
    // NativeFunction no longer owns any GcObjects.
}

size_t NativeFunction::gcSize() const { return sizeof(NativeFunction); }

}
