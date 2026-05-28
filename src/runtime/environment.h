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
class Environment {
public:

    explicit Environment(
        Environment* enclosing = nullptr
    );

    explicit Environment(
        std::shared_ptr<Environment> enclosing
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

    Environment* enclosingEnvironment() const;

    static std::shared_ptr<Environment> snapshot(
        const Environment& env
    );

private:

    Environment* enclosing;

    std::shared_ptr<Environment> enclosingOwned;

    std::unordered_map<
        std::string,
        Value
    > values;

    const Environment* parent() const;
};

}
