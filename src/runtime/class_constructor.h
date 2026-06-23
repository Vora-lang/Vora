#pragma once

#include "callable.h"
#include "../gc/gc_ptr.h"

#include <string>
#include <vector>

namespace vora {

class VoraFunction;
struct FunctionPrototype;
struct ClassDefinition;
class VM;

// ClassConstructor — a callable that creates instances of a class.
// Stores the class definition and the compiled constructor function.
//
// Created by OP_CLASS at runtime. Constructor execution uses a temporary
// VM that syncs globals from the calling VM at call time (no stale snapshot),
// and merges mutations back afterward.
class ClassConstructor : public Callable {
public:
    ClassConstructor(GcPtr<ClassDefinition> classDef,
                     GcPtr<VoraFunction> ctorFn);

    // Legacy path: creates a minimal temp VM to run constructors.
    // Prefer call(VM&, args) which has access to the real VM's globals.
    Value call(const std::vector<Value>& arguments) override;

    // Primary path used by callValue(): runs constructors in a temp VM
    // with globals synced from the calling VM, then merges mutations back.
    Value call(VM& vm, const std::vector<Value>& args) override;

    GcPtr<ClassDefinition> getClassDef() const override { return classDef_; }
    GcPtr<VoraFunction> getCtorFn() const { return ctorFn_; }

    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override;

private:
    GcPtr<ClassDefinition> classDef_;
    GcPtr<VoraFunction> ctorFn_;
};

} // namespace vora
