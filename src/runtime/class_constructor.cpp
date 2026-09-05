#include "class_constructor.h"
#include "vora_function.h"
#include "value.h"
#include "../vm/vm.h"
#include "../vm/compiler.h"  // for FunctionPrototype, ClassDefinition
#include "../gc/gc_heap.h"

namespace vora {

ClassConstructor::ClassConstructor(
    GcPtr<ClassDefinition> classDef,
    GcPtr<VoraFunction> ctorFn)
    : classDef_(std::move(classDef)),
      ctorFn_(std::move(ctorFn)) {
}

Value ClassConstructor::call(const std::vector<Value>& args) {
    // Fallback path for direct calls (not through callValue).
    // Create a minimal temp VM so constructors still run and the
    // instance is properly initialized. Global mutations are discarded
    // (no parent VM to merge into), which is the best we can do
    // without a calling VM reference.
    VM tempVm;
    return call(tempVm, args);
}

Value ClassConstructor::call(VM& vm, const std::vector<Value>& args) {
    auto instance = GcHeap::instance().alloc<ObjectInstance>();
    instance->className = classDef_->name;
    instance->classDefinition = classDef_;

    // Single temporary VM for all constructor runs.
    // Copy CURRENT globals from the calling VM (not a stale snapshot).
    VM tempVm;
    tempVm.copyGlobalsFrom(vm);

    // Run parent constructors in reverse MRO order (most-base first, excluding self).
    // MRO is [self, P1, P2, ..., base]. Reverse → [base, ..., P2, P1].
    for (size_t idx = classDef_->mro.size(); idx > 1; idx--) {
        if (auto parentClass = classDef_->mro[idx - 1]) {
            if (parentClass->ctorProto) {
                tempVm.runConstructor(*parentClass->ctorProto, instance, args);
            }
        }
    }

    // Run own constructor with all args
    if (ctorFn_->getPrototype()) {
        tempVm.runConstructor(*ctorFn_->getPrototype(), instance, args);
    }

    // Propagate any global mutations back to the calling VM.
    tempVm.mergeGlobalsTo(vm);

    return instance;
}

void ClassConstructor::trace(std::vector<GcObject*>& wl) {
    if (classDef_) wl.push_back(classDef_.get());
    if (ctorFn_) wl.push_back(ctorFn_.get());
}

size_t ClassConstructor::gcSize() const { return sizeof(ClassConstructor); }

} // namespace vora
