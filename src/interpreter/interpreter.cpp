#include "interpreter.h"

#include "../runtime/callable.h"
#include "../runtime/vora_function.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace vora {

namespace {

struct ReturnSignal {
    Value value;
};

}

Interpreter::Interpreter()
    : globals(std::make_shared<Environment>()),
      environment(globals.get()) {

    defineNative(
        "print",
        -1,
        [](const std::vector<Value>& arguments) -> Value {

            for (size_t i = 0; i < arguments.size(); ++i) {

                if (i > 0) {
                    std::cout << ' ';
                }

                printValue(arguments[i]);
            }

            std::cout << std::endl;

            return nullptr;
        }
    );

    defineNative(
        "clock",
        0,
        [](const std::vector<Value>&) -> Value {

            using namespace std::chrono;

            auto now = system_clock::now().time_since_epoch();

            auto millis =
                duration_cast<milliseconds>(now).count();

            return static_cast<double>(millis) / 1000.0;
        }
    );

    defineNative(
        "input",
        0,
        [](const std::vector<Value>&) -> Value {

            std::string line;

            std::getline(std::cin, line);

            return line;
        }
    );
}

void Interpreter::defineNative(
    const std::string& name,
    int arity,
    NativeFunction::NativeFn function
) {

    globals->define(
        name,
        std::make_shared<NativeFunction>(
            name,
            arity,
            std::move(function)
        )
    );
}

void Interpreter::defineBinding(
    const std::string& name,
    const Value& value
) {

    if (scopeStack.empty()) {
        globals->define(name, value);
    } else {
        environment.define(name, value);
    }
}

Value Interpreter::invoke(
    const Value& callee,
    const std::vector<Value>& arguments
) {

    if (!std::holds_alternative<
            std::shared_ptr<Callable>
        >(callee)) {

        std::cerr
            << "Can only call functions"
            << std::endl;

        return nullptr;
    }

    const auto& callable =
        std::get<std::shared_ptr<Callable>>(callee);

    if (auto native =
        std::dynamic_pointer_cast<NativeFunction>(callable)) {

        if (native->arity() >= 0
            && arguments.size() != static_cast<size_t>(
                native->arity()
            )) {

            std::cerr
                << "Expected "
                << native->arity()
                << " arguments but got "
                << arguments.size()
                << std::endl;

            return nullptr;
        }
    }

    return callable->call(
        *this,
        arguments
    );
}

void Interpreter::pushScope() {

    scopeStack.push_back(
        std::move(environment)
    );

    environment = Environment(
        &scopeStack.back()
    );
}

void Interpreter::pushScope(
    Environment* enclosing
) {

    scopeStack.push_back(
        std::move(environment)
    );

    environment = Environment(
        enclosing
    );
}

void Interpreter::popScope() {

    environment = std::move(
        scopeStack.back()
    );

    scopeStack.pop_back();
}

std::shared_ptr<Environment> Interpreter::captureClosure() {

    if (scopeStack.empty()) {
        return globals;
    }

    return Environment::snapshot(
        environment
    );
}

Value Interpreter::callFunction(
    const VoraFunction& function,
    const std::vector<Value>& arguments
) {

    if (arguments.size() != function.params().size()) {

        std::cerr
            << "Expected "
            << function.params().size()
            << " arguments but got "
            << arguments.size()
            << std::endl;

        return nullptr;
    }

    pushScope(
        function.closure().get()
    );

    for (size_t i = 0; i < function.params().size(); ++i) {

        environment.define(
            function.params()[i],
            arguments[i]
        );
    }

    try {

        executeBlock(
            function.body()->statements
        );
    }
    catch (const ReturnSignal& signal) {

        popScope();

        return signal.value;
    }

    popScope();

    return nullptr;
}

bool Interpreter::isTruthy(
    const Value& value
) {

    if (std::holds_alternative<std::nullptr_t>(value)) {
        return false;
    }

    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value);
    }

    return true;
}

void Interpreter::interpret(
    const Program* program
) {

    try {

        for (const auto& stmt :
            program->statements) {

            execute(stmt.get());
        }
    }
    catch (const ReturnSignal&) {

        std::cerr
            << "Cannot return from top-level code"
            << std::endl;
    }
}
Value Interpreter::evaluate(
    const Expr* expr
) {

    if (auto assign =
            dynamic_cast<
                const AssignmentExpr*
            >(expr)) {

        Value value =
            evaluate(
                assign->value.get()
            );

        environment.assign(
            assign->name,
            value
        );

        return value;
    }

    if (auto call =
        dynamic_cast<const CallExpr*>(expr)) {

        Value callee =
            evaluate(call->callee.get());

        std::vector<Value> arguments;

        arguments.reserve(
            call->arguments.size()
        );

        for (const auto& argument : call->arguments) {

            arguments.push_back(
                evaluate(argument.get())
            );
        }

        return invoke(
            callee,
            arguments
        );
    }

    if (auto variable =
        dynamic_cast<const VariableExpr*>(expr)) {

        return environment.get(
            variable->name
        );
    }

    if (auto literal =
        dynamic_cast<const LiteralExpr*>(expr)) {

        return literal->value;
    }

    if (auto grouping =
        dynamic_cast<const GroupingExpr*>(expr)) {

        return evaluate(
            grouping->expression.get()
        );
    }

    if (auto unary =
        dynamic_cast<const UnaryExpr*>(expr)) {

        Value right =
            evaluate(
                unary->right.get()
            );

        if (std::holds_alternative<double>(right)) {

            double value =
                std::get<double>(right);

            switch (unary->op.type) {

            case TokenType::MINUS:
                return -value;

            default:
                break;
            }
        }

        if (unary->op.type == TokenType::NOT) {

            return !isTruthy(right);
        }

        std::cerr
            << "Invalid unary operand"
            << std::endl;

        return nullptr;
    }

    if (auto binary =
        dynamic_cast<const BinaryExpr*>(expr)) {

        Value left =
            evaluate(
                binary->left.get()
            );

        Value right =
            evaluate(
                binary->right.get()
            );

        if (
            std::holds_alternative<double>(left)
            &&
            std::holds_alternative<double>(right)
        ) {

            double l =
                std::get<double>(left);

            double r =
                std::get<double>(right);

            switch (binary->op.type) {

            case TokenType::PLUS:
                return l + r;

            case TokenType::MINUS:
                return l - r;

            case TokenType::MULTIPLY:
                return l * r;

            case TokenType::DIVIDE:

                if (r == 0) {

                    std::cerr
                        << "Runtime Error: Division by zero"
                        << std::endl;

                    return nullptr;
                }

                return l / r;

            case TokenType::POWER:
                return std::pow(l, r);

            case TokenType::LESS:
                return l < r;

            case TokenType::LESS_EQUAL:
                return l <= r;

            case TokenType::GREATER:
                return l > r;

            case TokenType::GREATER_EQUAL:
                return l >= r;

            case TokenType::EQUAL_EQUAL:
                return l == r;

            case TokenType::NOT_EQUAL:
                return l != r;

            default:
                break;
            }
        }

        std::cerr
            << "Invalid binary operands"
            << std::endl;

        return nullptr;
    }

    std::cerr
        << "Unknown expression"
        << std::endl;

    return nullptr;
}

void Interpreter::executeBlock(
    const std::vector<std::unique_ptr<Stmt>>& statements
) {

    pushScope();

    try {

        for (const auto& stmt : statements) {
            execute(stmt.get());
        }
    }
    catch (const ReturnSignal& signal) {

        popScope();

        throw signal;
    }

    popScope();
}

void Interpreter::execute(
    const Stmt* stmt
) {

    if (auto block =
        dynamic_cast<const BlockStmt*>(stmt)) {

        executeBlock(block->statements);
        return;
    }

    if (auto ifStmt =
        dynamic_cast<const IfStmt*>(stmt)) {

        if (isTruthy(
                evaluate(ifStmt->condition.get())
            )) {

            execute(ifStmt->thenBranch.get());
        }
        else if (ifStmt->elseBranch) {

            execute(ifStmt->elseBranch.get());
        }

        return;
    }

    if (auto whileStmt =
        dynamic_cast<const WhileStmt*>(stmt)) {

        while (isTruthy(
            evaluate(whileStmt->condition.get())
        )) {

            execute(whileStmt->body.get());
        }

        return;
    }

    if (auto returnStmt =
        dynamic_cast<const ReturnStmt*>(stmt)) {

        throw ReturnSignal{
            evaluate(returnStmt->value.get())
        };
    }

    if (auto func =
        dynamic_cast<const FuncStmt*>(stmt)) {

        auto function = std::make_shared<VoraFunction>(
            func->name,
            func->params,
            func->body,
            captureClosure()
        );

        defineBinding(
            func->name,
            function
        );

        return;
    }

    if (auto letStmt =
        dynamic_cast<const LetStmt*>(stmt)) {

        defineBinding(
            letStmt->name,
            evaluate(letStmt->initializer.get())
        );

        return;
    }

    if (auto exprStmt =
        dynamic_cast<const ExprStmt*>(stmt)) {
    
        evaluate(exprStmt->expression.get());
    
        return;
    }
}

}