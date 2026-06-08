#include "vora_function.h"

#include "../interpreter/interpreter.h"
#include "../vm/compiler.h"  // for FunctionPrototype
#include "environment.h"

namespace vora {

VoraFunction::VoraFunction(
    std::string name,
    std::vector<std::string> params,
    std::shared_ptr<BlockStmt> body,
    std::shared_ptr<Environment> closure
)
    : name_(std::move(name)),
      params_(std::move(params)),
      arity_(static_cast<int>(params_.size())),
      body_(std::move(body)),
      closure_(std::move(closure)) {
}

VoraFunction::VoraFunction(
    std::string name,
    int arity,
    const FunctionPrototype* prototype
)
    : name_(std::move(name)),
      arity_(arity),
      prototype_(prototype) {
}

Value VoraFunction::call(
    Interpreter& interpreter,
    const std::vector<Value>& arguments
) {

    return interpreter.callFunction(
        *this,
        arguments
    );
}

const std::string& VoraFunction::name() const {
    return name_;
}

const std::vector<std::string>& VoraFunction::params() const {
    return params_;
}

const std::shared_ptr<BlockStmt>& VoraFunction::body() const {
    return body_;
}

const std::shared_ptr<Environment>& VoraFunction::closure() const {
    return closure_;
}

}
