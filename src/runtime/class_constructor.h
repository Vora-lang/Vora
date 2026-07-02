/**
 * @file class_constructor.h
 * @brief Callable that creates instances of a Vora class.
 *
 * ClassConstructor wraps a ClassDefinition and its compiled constructor
 * VoraFunction.  When invoked, it allocates a new ObjectInstance, runs
 * the constructor (possibly through the MRO chain for inheritance), and
 * returns the constructed instance.
 */

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

/**
 * @brief A callable that creates instances of a Vora class.
 *
 * Created by the OP_CLASS instruction at runtime.  It stores the class
 * definition (with MRO, method table, parent chain) and the compiled
 * constructor function.
 *
 * Constructor execution uses a temporary VM that syncs globals from the
 * calling VM at call time (no stale snapshot) and merges mutations back
 * afterward.  This design allows constructors to read and modify global
 * state while keeping their local scope isolated.
 */
class ClassConstructor : public Callable {
public:
    /**
     * @brief Construct a ClassConstructor.
     * @param classDef The class definition (MRO, methods, parent info).
     * @param ctorFn   The compiled constructor VoraFunction.
     */
    ClassConstructor(GcPtr<ClassDefinition> classDef,
                     GcPtr<VoraFunction> ctorFn);

    /**
     * @brief Legacy invocation path — creates a minimal temp VM.
     *
     * Prefer call(VM&, args) which has access to the real calling VM's
     * global state.  This overload exists for compatibility with code
     * that invokes Callable through the single-argument interface.
     *
     * @param arguments Positional argument list.
     * @return The newly constructed ObjectInstance.
     */
    Value call(const std::vector<Value>& arguments) override;

    /**
     * @brief Primary invocation path used by callValue().
     *
     * Runs the constructor in a temporary VM with globals synced from the
     * calling VM, then merges global mutations back.  This ensures the
     * constructor sees up-to-date globals and its side effects are visible
     * to the caller.
     *
     * @param vm   The calling VM instance (provides global state).
     * @param args Positional argument list.
     * @return The newly constructed ObjectInstance.
     */
    Value call(VM& vm, const std::vector<Value>& args) override;

    /** @brief Get the ClassDefinition for this constructor. */
    GcPtr<ClassDefinition> getClassDef() const override { return classDef_; }

    /** @brief Get the compiled constructor function. */
    GcPtr<VoraFunction> getCtorFn() const { return ctorFn_; }

    /** @brief Trace GC roots: classDef and ctorFn. */
    void trace(std::vector<GcObject*>& wl) override;

    /** @brief Approximate GC-tracked memory size. */
    size_t gcSize() const override;

private:
    GcPtr<ClassDefinition> classDef_;
    GcPtr<VoraFunction> ctorFn_;
};

} // namespace vora
