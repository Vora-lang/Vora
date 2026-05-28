#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../ast/expr.h"
#include "../ast/program.h"
#include "../ast/stmt.h"

#include "../runtime/environment.h"
#include "../runtime/native_function.h"
#include "../runtime/runtime_config.h"
#include "../runtime/runtime_error.h"
#include "../runtime/value.h"

namespace vora {

class VoraFunction;

class Interpreter {
public:

    explicit Interpreter(
        RuntimeConfig config = {}
    );

    void interpret(
        const Program* program
    );

    Value callFunction(
        const VoraFunction& function,
        const std::vector<Value>& arguments
    );

private:

    RuntimeConfig config;

    std::shared_ptr<Environment> globals;

    Environment environment;

    std::deque<Environment> scopeStack;

    void pushScope();

    void pushScope(
        Environment* enclosing
    );

    void popScope();

    Value evaluate(
        const Expr* expr
    );

    void execute(
        const Stmt* stmt
    );

    void executeBlock(
        const std::vector<std::unique_ptr<Stmt>>& statements
    );

    Value invoke(
        const Value& callee,
        const std::vector<Value>& arguments,
        const Token& token
    );

    void defineNative(
        const std::string& name,
        int arity,
        NativeFunction::NativeFn function
    );

    static bool isTruthy(
        const Value& value
    );

    std::shared_ptr<Environment> captureClosure();

    void defineBinding(
        const std::string& name,
        const Value& value
    );

    std::string interpolateString(
        const std::string& str
    );
};

}
