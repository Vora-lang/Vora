#pragma once

#include "callable.h"
#include "../gc/gc_ptr.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace vora {

class VoraFunction;
struct FunctionPrototype;
struct ClassDefinition;

// ClassConstructor — a callable that creates instances of a class.
// Stores the class definition, the compiled constructor function, and
// a snapshot of global state (for running parent constructors in a
// temporary VM).
//
// Created by OP_CLASS at runtime.
class ClassConstructor : public Callable {
public:
    ClassConstructor(GcPtr<ClassDefinition> classDef,
                     GcPtr<VoraFunction> ctorFn,
                     std::vector<std::string> globalNames,
                     std::vector<Value> globalValues,
                     std::vector<bool> globalDefined,
                     std::unordered_map<std::string, int> globalIndex);

    Value call(const std::vector<Value>& arguments) override;

    GcPtr<ClassDefinition> getClassDef() const override { return classDef_; }

    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override;

private:
    GcPtr<ClassDefinition> classDef_;
    GcPtr<VoraFunction> ctorFn_;

    // Globals snapshot — needed by the temporary VM that runs parent
    // and own constructors. This ensures the constructor can access
    // global variables even when called from a different context.
    std::vector<std::string> globalNames_;
    std::vector<Value> globalValues_;
    std::vector<bool> globalDefined_;
    std::unordered_map<std::string, int> globalIndex_;
};

} // namespace vora
