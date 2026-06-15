#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "../lexer/token.h"
#include "value.h"

namespace vora {

// Lexical scope chain: each frame has a parent (enclosing) environment.
// define() always binds in the current frame only.
// get() / assign() resolve locally first, then walk upward.
//
// Environments are owned exclusively by shared_ptr — the enclosing chain
// forms a DAG where each node holds a shared_ptr to its parent, and
// snapshot() creates an independent deep copy for closures.
class Environment {
public:

    explicit Environment(
        std::shared_ptr<Environment> enclosing = nullptr
    );

    void define(
        const std::string& name,
        const Value& value
    );

    Value get(
        const std::string& name,
        const Token& token
    ) const;

    void assign(
        const std::string& name,
        const Value& value,
        const Token& token
    );

    bool hasLocal(
        const std::string& name
    ) const;

    std::shared_ptr<Environment> enclosingEnvironment() const;

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
