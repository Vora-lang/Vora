#include "class_constructor.h"
#include "vora_function.h"
#include "value.h"
#include "../vm/vm.h"
#include "../vm/compiler.h"  // for FunctionPrototype, ClassDefinition
#include "../gc/gc_heap.h"

namespace vora {

ClassConstructor::ClassConstructor(
    GcPtr<ClassDefinition> classDef,
    GcPtr<VoraFunction> ctorFn,
    std::vector<std::string> globalNames,
    std::vector<Value> globalValues,
    std::vector<bool> globalDefined,
    std::unordered_map<std::string, int> globalIndex)
    : classDef_(std::move(classDef)),
      ctorFn_(std::move(ctorFn)),
      globalNames_(std::move(globalNames)),
      globalValues_(std::move(globalValues)),
      globalDefined_(std::move(globalDefined)),
      globalIndex_(std::move(globalIndex)) {
}

Value ClassConstructor::call(const std::vector<Value>& args) {
    auto instance = GcHeap::instance().alloc<ObjectInstance>();
    instance->className = classDef_->name;
    instance->classDefinition = classDef_;

    // Single temporary VM reused for all constructor runs.
    VM tempVm;
    tempVm.adoptGlobals(globalNames_, globalValues_, globalDefined_, globalIndex_);

    // Run parent constructors in reverse MRO order (most-base first, excluding self).
    // MRO is [self, P1, P2, ..., base]. Reverse → [base, ..., P2, P1].
    for (size_t idx = classDef_->mro.size(); idx > 1; idx--) {
        if (auto parentClass = classDef_->mro[idx - 1]) {
            if (parentClass->ctorProto) {
                tempVm.runConstructor(parentClass->ctorProto->chunk, instance, args);
            }
        }
    }

    // Run own constructor with all args
    if (ctorFn_->getPrototype()) {
        tempVm.runConstructor(ctorFn_->getPrototype()->chunk, instance, args);
    }

    return instance;
}

void ClassConstructor::trace(std::vector<GcObject*>& wl) {
    if (classDef_) wl.push_back(classDef_.get());
    if (ctorFn_) wl.push_back(ctorFn_.get());
}

size_t ClassConstructor::gcSize() const { return sizeof(ClassConstructor); }

} // namespace vora
