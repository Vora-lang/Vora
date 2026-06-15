#include "environment.h"
#include "runtime_error.h"

namespace vora {

Environment::Environment(
    std::shared_ptr<Environment> enclosing
)
    : enclosing_(std::move(enclosing)) {
}

void Environment::define(
    const std::string& name,
    const Value& value
) {
    values_[name] = value;
}

bool Environment::hasLocal(
    const std::string& name
) const {
    return values_.find(name) != values_.end();
}

Value Environment::get(
    const std::string& name,
    const Token& token
) const {
    if (hasLocal(name)) {
        return values_.at(name);
    }

    if (enclosing_) {
        return enclosing_->get(name, token);
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
        values_[name] = value;
        return;
    }

    if (enclosing_) {
        enclosing_->assign(name, value, token);
        return;
    }

    throw RuntimeError(
        "Undefined variable: " + name,
        token
    );
}

std::shared_ptr<Environment> Environment::enclosingEnvironment() const {
    return enclosing_;
}

std::shared_ptr<Environment> Environment::snapshot(
    const Environment& env
) {
    std::shared_ptr<Environment> parentSnapshot;

    if (env.enclosing_) {
        parentSnapshot = snapshot(*env.enclosing_);
    }

    auto copy = std::make_shared<Environment>(parentSnapshot);
    copy->values_ = env.values_;

    return copy;
}

}
