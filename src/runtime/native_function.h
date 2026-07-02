/**
 * @file native_function.h
 * @brief Pure native (C++) function callable from Vora bytecode.
 *
 * NativeFunction wraps a std::function callback so it can be stored in a
 * Vora Value and invoked through the unified Callable interface.  It has
 * no class-constructor or bound-method state — those roles have been
 * split into ClassConstructor and BoundMethod respectively.
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "callable.h"

namespace vora {

/**
 * @brief A pure native (C++) function that Vora code can call.
 *
 * Wraps a std::function<Value(const std::vector<Value>&)> together with a
 * name and declared arity.  Instances are created by builtin registries
 * (see builtins.h) and by collection method factories.  The VM dispatches
 * NativeFunction via Callable::call() — no special VM-level handling is
 * required.
 */
class NativeFunction : public Callable {
public:
    /**
     * @brief Signature of the underlying native callback.
     *
     * Receives a vector of positional argument Values and returns a single
     * result Value.  The VM guarantees argument count matching against
     * arity() before calling, so the callback need not re-validate.
     */
    using NativeFn = std::function<Value(const std::vector<Value>&)>;

    /**
     * @brief Construct a NativeFunction.
     * @param name     Human-readable name, used in error messages and stack traces.
     * @param arity    Expected number of arguments (negative for variadic).
     * @param function The C++ callback to invoke.
     */
    NativeFunction(std::string name, int arity, NativeFn function);

    /**
     * @brief Invoke the native callback.
     * @param arguments Positional argument list (size matches arity()).
     * @return The result value returned by the native callback.
     */
    Value call(const std::vector<Value>& arguments) override;

    /**
     * @brief Get the human-readable name of this native function.
     * @return Const reference to the name string.
     */
    const std::string& name() const;

    /**
     * @brief Get the declared arity.
     * @return Expected argument count (negative for variadic).
     */
    int arity() const;

    /** @brief Trace GC roots held by this native function. */
    void trace(std::vector<GcObject*>& wl) override;

    /** @brief Approximate GC-tracked memory size of this object. */
    size_t gcSize() const override;

private:
    std::string name_;
    int arity_;
    NativeFn function_;
};

} // namespace vora
