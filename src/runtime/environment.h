/**
 * @file environment.h
 * @brief Lexical scope chain for the Vora runtime.
 *
 * Each Environment represents one scope frame.  Frames are linked via
 * shared_ptr to form a singly-linked parent chain, which variable lookup
 * walks from innermost to outermost (global).  This design supports
 * closures: snapshot() creates an independent deep copy of the chain so
 * that a function can capture its defining scope.
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "../lexer/token.h"
#include "value.h"

namespace vora {

/**
 * @brief A single frame in the lexical scope chain.
 *
 * define() always binds a name in the current frame only, never shadows a
 * parent.  get() resolves locally first, then walks the enclosing chain
 * upward.  assign() does the same walk: if the name exists in any
 * ancestor it is mutated there; otherwise it is defined locally (global
 * fallback semantics).
 *
 * Ownership is exclusively via std::shared_ptr.  The enclosing pointer
 * forms a DAG — each node holds a shared_ptr to its parent.  Closures
 * obtain a deep copy of the current chain through the static snapshot()
 * factory.
 */
class Environment {
public:

    /**
     * @brief Construct a scope frame, optionally nested inside an enclosing scope.
     * @param enclosing The parent scope (nullptr for the global scope).
     */
    explicit Environment(
        std::shared_ptr<Environment> enclosing = nullptr
    );

    /**
     * @brief Bind a name to a value in this frame only.
     * @param name  Variable name to define.
     * @param value Initial value.
     */
    void define(
        const std::string& name,
        const Value& value
    );

    /**
     * @brief Resolve a name by walking the scope chain upward.
     * @param name  Variable name to look up.
     * @param token Token for error reporting (line/column) on failure.
     * @return The value bound to the name.
     * @throws RuntimeError if the name is not found in any frame.
     */
    Value get(
        const std::string& name,
        const Token& token
    ) const;

    /**
     * @brief Assign to an existing variable, walking upward if needed.
     *
     * If the name exists in this frame or any ancestor, the value at the
     * nearest occurrence is updated.  Otherwise the name is defined
     * locally (global-level assignment creates a new global).
     *
     * @param name  Variable name to assign.
     * @param value New value.
     * @param token Token for error reporting on failure.
     * @throws RuntimeError if the name is not found and cannot be created.
     */
    void assign(
        const std::string& name,
        const Value& value,
        const Token& token
    );

    /**
     * @brief Check whether a name is defined in this frame (not walking upward).
     * @param name Variable name.
     * @return true if the name exists in this exact frame.
     */
    bool hasLocal(
        const std::string& name
    ) const;

    /**
     * @brief Get the enclosing (parent) environment.
     * @return Shared pointer to the parent scope, or nullptr if this is the global scope.
     */
    std::shared_ptr<Environment> enclosingEnvironment() const;

    /**
     * @brief Deep-copy an environment chain for closure capture.
     *
     * Creates an independent copy of the entire chain rooted at @p env.
     * The returned environment shares no mutable state with the original,
     * so captured variables persist after the original scope is destroyed.
     *
     * @param env The environment (chain root) to snapshot.
     * @return A deep-copied chain that can outlive the original scope.
     */
    static std::shared_ptr<Environment> snapshot(
        const Environment& env
    );

private:
    std::shared_ptr<Environment> enclosing_;

    std::unordered_map<
        std::string,
        Value
    > values_;
};

}
