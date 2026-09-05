/**
 * @file vora_function.h
 * @brief Bytecode-compiled Vora function (closure-capable).
 *
 * VoraFunction is the runtime representation of a function defined in Vora
 * source code.  It holds a pointer to the immutable FunctionPrototype
 * (bytecode + metadata) and a vector of Upvalue shared-pointers that
 * implement closure variable capture.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "callable.h"

namespace vora {

struct FunctionPrototype;

/**
 * @brief A bytecode-compiled Vora function, potentially a closure.
 *
 * Each VoraFunction is backed by a FunctionPrototype that contains the
 * compiled bytecode chunk, constant pool, and debug info.  The prototype
 * is shared across all closures created from the same function definition;
 * only the upvalue vector differs per-instance.
 *
 * VoraFunction is NOT invoked via call() directly — the VM intercepts it
 * in callValue() and dispatches through callVoraFunction(), which sets up
 * a new call frame, binds upvalues, and begins executing bytecode.
 */
class VoraFunction : public Callable {
public:
    /**
     * @brief Construct a VoraFunction from a compiled prototype.
     * @param name           Human-readable function name (for stack traces).
     * @param arity          Total declared parameter count.
     * @param requiredArity  Minimum required argument count (before defaults/rest).
     * @param hasRest        True if the function accepts a rest parameter (...args).
     * @param prototype      Pointer to the immutable compiled prototype.
     */
    VoraFunction(
        std::string name,
        int arity,
        int requiredArity,
        bool hasRest,
        const FunctionPrototype* prototype
    );

    /**
     * @brief Direct invocation — not normally used.
     *
     * The VM does NOT go through this path.  callValue() intercepts
     * VoraFunction and calls callVoraFunction() instead, which sets up
     * call frames and upvalue bindings.  This override exists only to
     * satisfy the Callable interface.
     *
     * @param arguments Positional argument list.
     * @return Result value (default implementation throws).
     */
    Value call(const std::vector<Value>& arguments) override;

    /**
     * @brief Get the function name.
     * @return Const reference to the name string.
     */
    const std::string& name() const;

    /** @brief Total declared parameter count. */
    int arity() const { return arity_; }

    /** @brief Minimum required arguments before defaults/rest. */
    int requiredArity() const { return requiredArity_; }

    /** @brief Whether the function has a rest parameter. */
    bool hasRest() const { return hasRest_; }

    /** @brief Pointer to the immutable compiled prototype. */
    const FunctionPrototype* getPrototype() const { return prototype_; }

    /** @brief Whether this function has been compiled (prototype is non-null). */
    bool isCompiled() const { return prototype_ != nullptr; }

    /**
     * @brief Upvalue list for closure variable capture.
     *
     * Each Upvalue provides pointer indirection: while the captured local
     * variable is alive on the stack, location points to the live stack
     * slot.  When the local goes out of scope, close() copies the value to
     * heap storage and redirects location there, so the closure sees the
     * correct value regardless of stack lifetime.
     */
    std::vector<std::shared_ptr<Upvalue>> upvalues;

    /** @brief Trace GC roots: prototypes and upvalues. */
    void trace(std::vector<GcObject*>& wl) override;

    /** @brief Approximate GC-tracked memory size. */
    size_t gcSize() const override { return sizeof(VoraFunction); }

private:
    std::string name_;
    int arity_ = 0;
    int requiredArity_ = 0;
    bool hasRest_ = false;
    const FunctionPrototype* prototype_ = nullptr;
};

} // namespace vora
