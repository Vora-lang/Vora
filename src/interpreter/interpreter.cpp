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

Interpreter::Interpreter(
    RuntimeConfig config)
    : config(config),
      globals(std::make_shared<Environment>()),
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
    const std::vector<Value>& arguments,
    const Token& token
) {

    if (!std::holds_alternative<
            std::shared_ptr<Callable>
        >(callee)) {

        throw RuntimeError(
            "Can only call functions",
            token
        );
    }

    const auto& callable =
        std::get<std::shared_ptr<Callable>>(callee);

    if (auto native =
        std::dynamic_pointer_cast<NativeFunction>(callable)) {

        if (native->arity() >= 0
            && arguments.size() != static_cast<size_t>(
                native->arity()
            )) {

            throw RuntimeError(
                "Wrong arity: expected "
                    + std::to_string(native->arity())
                    + " arguments but got "
                    + std::to_string(arguments.size()),
                token
            );
        }
    }

    if (auto function =
        std::dynamic_pointer_cast<VoraFunction>(callable)) {

        if (arguments.size() != function->params().size()) {
            throw RuntimeError(
                "Wrong arity: expected "
                    + std::to_string(function->params().size())
                    + " arguments but got "
                    + std::to_string(arguments.size()),
                token
            );
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
            value,
            assign->nameToken
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
            arguments,
            call->paren
        );
    }

    if (auto variable =
        dynamic_cast<const VariableExpr*>(expr)) {

        return environment.get(
            variable->name,
            variable->nameToken
        );
    }

    if (auto array =
        dynamic_cast<const ArrayExpr*>(expr)) {

        auto result = std::make_shared<Array>();
        result->elements.reserve(array->elements.size());

        for (const auto& element : array->elements) {
            result->elements.push_back(
                evaluate(element.get())
            );
        }

        return result;
    }

    if (auto indexExpr =
        dynamic_cast<const IndexExpr*>(expr)) {

        Value array =
            evaluate(indexExpr->array.get());

        Value indexValue =
            evaluate(indexExpr->index.get());

        if (!std::holds_alternative<
                std::shared_ptr<Array>
            >(array)) {

            throw RuntimeError(
                "Indexing requires an array",
                indexExpr->bracket
            );
        }

        if (!std::holds_alternative<double>(indexValue)) {
            throw RuntimeError(
                "Array index must be a number",
                indexExpr->bracket
            );
        }

        double rawIndex =
            std::get<double>(indexValue);

        if (rawIndex < 0 ||
            std::floor(rawIndex) != rawIndex) {

            throw RuntimeError(
                "Array index must be a non-negative integer",
                indexExpr->bracket
            );
        }

        size_t index =
            static_cast<size_t>(rawIndex);

        const auto& elements =
            std::get<std::shared_ptr<Array>>(array)->elements;

        if (index >= elements.size()) {
            throw RuntimeError(
                "Index out of bounds",
                indexExpr->bracket
            );
        }

        return elements[index];
    }

    if (auto literal =
        dynamic_cast<const LiteralExpr*>(expr)) {

        if (std::holds_alternative<std::string>(literal->value)) {
            std::string str = std::get<std::string>(literal->value);
            return interpolateString(str);
        }

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

        throw RuntimeError(
            "Invalid unary operand",
            unary->op
        );
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

                    throw RuntimeError(
                        "Division by zero",
                        binary->op
                    );
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

        throw RuntimeError(
            "Invalid binary operands",
            binary->op
        );
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

    if (auto forStmt =
        dynamic_cast<const ForStmt*>(stmt)) {

        Value iterable = evaluate(forStmt->iterable.get());

        if (!std::holds_alternative<
                std::shared_ptr<Array>
            >(iterable)) {

            throw RuntimeError(
                "Can only iterate over arrays",
                forStmt->forToken
            );
        }

        const auto& array =
            std::get<std::shared_ptr<Array>>(iterable);

        for (const auto& element : array->elements) {
            pushScope();
            environment.define(
                forStmt->variable,
                element
            );

            execute(forStmt->body.get());

            popScope();
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
    
        Value result = evaluate(exprStmt->expression.get());

        if (config.repl()) {
            printValue(result);
            std::cout << std::endl;
        }
    
        return;
    }
}

std::string Interpreter::interpolateString(
    const std::string& str
) {
    std::string result;
    size_t i = 0;

    while (i < str.length()) {
        if (i + 1 < str.length() && str[i] == '$' && str[i + 1] == '{') {
            i += 2;
            std::string varName;

            while (i < str.length() && str[i] != '}') {
                varName += str[i];
                i++;
            }

            if (i < str.length() && str[i] == '}') {
                i++;
                try {
                    Value val = environment.get(varName, Token(TokenType::IDENTIFIER, varName, 0));
                    if (std::holds_alternative<double>(val)) {
                        double d = std::get<double>(val);
                        if (d == static_cast<int>(d)) {
                            result += std::to_string(static_cast<int>(d));
                        } else {
                            result += std::to_string(d);
                        }
                    } else if (std::holds_alternative<std::string>(val)) {
                        result += std::get<std::string>(val);
                    } else if (std::holds_alternative<bool>(val)) {
                        result += std::get<bool>(val) ? "true" : "false";
                    } else if (std::holds_alternative<std::nullptr_t>(val)) {
                        result += "null";
                    } else {
                        result += "[object]";
                    }
                } catch (const RuntimeError&) {
                    result += "${";
                    result += varName;
                    result += "}";
                }
            }
        } else {
            result += str[i];
            i++;
        }
    }

    return result;
}

}