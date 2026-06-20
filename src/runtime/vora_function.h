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
        bool hasRest,
        const FunctionPrototype* prototype
    );

    // Not called directly — the VM dispatches VoraFunction
    // via callVoraFunction() which sets up call frames and upvalues.
    Value call(const std::vector<Value>& arguments) override;

    const std::string& name() const;

    int arity() const { return arity_; }
    int requiredArity() const { return requiredArity_; }
    bool hasRest() const { return hasRest_; }
    const FunctionPrototype* getPrototype() const { return prototype_; }
    bool isCompiled() const { return prototype_ != nullptr; }

    // Upvalues for VM closures. Each Upvalue provides pointer-indirection:
    // while the captured local is alive on the stack, location points to
    // the live stack slot. When the local goes out of scope, close() copies
    // the value to heap storage and redirects location there.
    std::vector<std::shared_ptr<Upvalue>> upvalues;

    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override { return sizeof(VoraFunction); }

private:
    std::string name_;
    int arity_ = 0;
    int requiredArity_ = 0;
    bool hasRest_ = false;
    const FunctionPrototype* prototype_ = nullptr;
};

} // namespace vora
