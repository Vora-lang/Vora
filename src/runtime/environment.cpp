#include "environment.h"
#include "runtime_error.h"

#include <iostream>

namespace vora {

Environment::Environment(
    Environment* enclosing
)
    : enclosing(enclosing),
      enclosingOwned(nullptr) {
}

Environment::Environment(
    std::shared_ptr<Environment> enclosing
)
    : enclosing(nullptr),
      enclosingOwned(std::move(enclosing)) {
}

const Environment* Environment::parent() const {

    if (enclosingOwned) {
        return enclosingOwned.get();
    }

    return enclosing;
}

void Environment::define(
    const std::string& name,
    const Value& value
) {

    values[name] = value;
}

bool Environment::hasLocal(
    const std::string& name
) const {

    return values.find(name) != values.end();
}

Value Environment::get(
    const std::string& name,
    const Token& token
) const {

    if (hasLocal(name)) {
        return values.at(name);
    }

    if (const Environment* parentEnv = parent()) {
        return parentEnv->get(name, token);
    }

    throw RuntimeError(
        "Undefined variable: " + name,
        token
    );
}

void Environment::assign(
    const std::string& name,
    const Value& value,
    const Token& token
) {

    if (hasLocal(name)) {
        values[name] = value;
        return;
    }

    if (const Environment* parentEnv = parent()) {
        const_cast<Environment*>(parentEnv)->assign(
            name,
            value,
            token
        );
        return;
    }

    throw RuntimeError(
        "Undefined variable: " + name,
        token
    );
}

Environment* Environment::enclosingEnvironment() const {

    if (enclosingOwned) {
        return enclosingOwned.get();
    }

    return enclosing;
}

std::shared_ptr<Environment> Environment::snapshot(
    const Environment& env
) {

    std::shared_ptr<Environment> parentSnapshot;

    if (const Environment* parentEnv = env.parent()) {
        parentSnapshot = snapshot(*parentEnv);
    }

    auto copy = std::make_shared<Environment>(
        parentSnapshot
    );

    copy->values = env.values;

    return copy;
}

}
