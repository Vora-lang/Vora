#pragma once

#include <memory>
#include <string>
#include <vector>

#include "callable.h"

namespace vora {

struct FunctionPrototype;

class VoraFunction : public Callable {
public:
    VoraFunction(
        std::string name,
        int arity,
        int requiredArity,
        const FunctionPrototype* prototype
    );

    // Not called directly — the VM dispatches VoraFunction
    // via callVoraFunction() which sets up call frames and upvalues.
    Value call(const std::vector<Value>& arguments) override;

    const std::string& name() const;

    int arity() const { return arity_; }
    int requiredArity() const { return requiredArity_; }
    const FunctionPrototype* getPrototype() const { return prototype_; }
    bool isCompiled() const { return prototype_ != nullptr; }

    // Upvalues for VM closures (shared heap-allocated values).
    // shared_ptr<Value> is kept because Value itself is not GC-managed.
    std::vector<std::shared_ptr<Value>> upvalues;

    void trace(std::vector<GcObject*>& wl) override {
        // VoraFunction is referenced from ObjectClass::methods,
        // from VM frames, and from closures (upvalues).  It doesn't
        // directly own any GcObject references itself.
    }
    size_t gcSize() const override { return sizeof(VoraFunction); }

private:
    std::string name_;
    int arity_ = 0;
    int requiredArity_ = 0;
    const FunctionPrototype* prototype_ = nullptr;
};

} // namespace vora
