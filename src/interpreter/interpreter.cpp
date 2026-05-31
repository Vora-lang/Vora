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

struct BreakSignal {};

struct ContinueSignal {};

struct ThrowSignal {
    Value value;
};

} // anonymous namespace

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
        -1,
        [](const std::vector<Value>& arguments) -> Value {

            if (!arguments.empty()) {
                std::cout << valueToString(arguments[0]);
            }

            std::string line;

            std::getline(std::cin, line);

            return line;
        }
    );

    defineNative(
        "int",
        1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];

            if (std::holds_alternative<double>(arg)) {
                double val = std::get<double>(arg);
                return std::trunc(val);
            }

            if (std::holds_alternative<std::string>(arg)) {
                try {
                    double val = std::stod(std::get<std::string>(arg));
                    return std::trunc(val);
                } catch (const std::exception&) {
                    throw RuntimeError(
                        "Cannot convert string to int",
                        Token(TokenType::IDENTIFIER, "int", 0)
                    );
                }
            }

            if (std::holds_alternative<bool>(arg)) {
                return std::get<bool>(arg) ? 1.0 : 0.0;
            }

            throw RuntimeError(
                "int() expects a number, string, or bool",
                Token(TokenType::IDENTIFIER, "int", 0)
            );
        }
    );

    defineNative(
        "float",
        1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];

            if (std::holds_alternative<double>(arg)) {
                return arg;  // already a float
            }

            if (std::holds_alternative<std::string>(arg)) {
                try {
                    return std::stod(std::get<std::string>(arg));
                } catch (const std::exception&) {
                    throw RuntimeError(
                        "Cannot convert string to float",
                        Token(TokenType::IDENTIFIER, "float", 0)
                    );
                }
            }

            if (std::holds_alternative<bool>(arg)) {
                return std::get<bool>(arg) ? 1.0 : 0.0;
            }

            throw RuntimeError(
                "float() expects a number, string, or bool",
                Token(TokenType::IDENTIFIER, "float", 0)
            );
        }
    );

    defineNative(
        "range",
        -1,
        [](const std::vector<Value>& arguments) -> Value {

            double start = 0;
            double end = 0;
            double step = 1;

            if (arguments.size() == 1) {
                end = std::get<double>(arguments[0]);
            } else if (arguments.size() == 2) {
                start = std::get<double>(arguments[0]);
                end = std::get<double>(arguments[1]);
            } else if (arguments.size() == 3) {
                start = std::get<double>(arguments[0]);
                end = std::get<double>(arguments[1]);
                step = std::get<double>(arguments[2]);
            } else {
                throw RuntimeError(
                    "range() expects 1, 2, or 3 arguments",
                    Token(TokenType::IDENTIFIER, "range", 0)
                );
            }

            auto arr = std::make_shared<Array>();
            if (step > 0) {
                for (double i = start; i < end; i += step) {
                    arr->elements.push_back(i);
                }
            } else if (step < 0) {
                for (double i = start; i > end; i += step) {
                    arr->elements.push_back(i);
                }
            }
            return arr;
        }
    );

    defineNative(
        "assert",
        -1,
        [](const std::vector<Value>& arguments) -> Value {
            if (arguments.empty()) {
                throw RuntimeError(
                    "assert() expects at least 1 argument",
                    Token(TokenType::IDENTIFIER, "assert", 0)
                );
            }

            bool condition = Interpreter::isTruthy(arguments[0]);
            if (!condition) {
                std::string message = "Assertion failed";
                if (arguments.size() >= 2 && std::holds_alternative<std::string>(arguments[1])) {
                    message = std::get<std::string>(arguments[1]);
                } else if (arguments.size() >= 2) {
                    message = valueToString(arguments[1]);
                }
                throw RuntimeError(
                    message,
                    Token(TokenType::IDENTIFIER, "assert", 0)
                );
            }

            return nullptr;
        }
    );

    defineNative(
        "bin",
        1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!std::holds_alternative<double>(arguments[0]))
                throw RuntimeError("bin() expects a number", Token(TokenType::IDENTIFIER, "bin", 0));
            double val = std::get<double>(arguments[0]);
            int64_t n = static_cast<int64_t>(std::trunc(val));
            if (n == 0) return std::string("0b0");
            bool neg = n < 0;
            uint64_t u = neg ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
            std::string bits;
            while (u > 0) {
                bits = (u & 1 ? '1' : '0') + bits;
                u >>= 1;
            }
            return (neg ? std::string("-0b") : std::string("0b")) + bits;
        }
    );

    defineNative(
        "oct",
        1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!std::holds_alternative<double>(arguments[0]))
                throw RuntimeError("oct() expects a number", Token(TokenType::IDENTIFIER, "oct", 0));
            double val = std::get<double>(arguments[0]);
            int64_t n = static_cast<int64_t>(std::trunc(val));
            if (n == 0) return std::string("0o0");
            bool neg = n < 0;
            uint64_t u = neg ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
            std::string digits;
            while (u > 0) {
                digits = static_cast<char>('0' + (u & 7)) + digits;
                u >>= 3;
            }
            return (neg ? std::string("-0o") : std::string("0o")) + digits;
        }
    );

    defineNative(
        "hex",
        1,
        [](const std::vector<Value>& arguments) -> Value {
            if (!std::holds_alternative<double>(arguments[0]))
                throw RuntimeError("hex() expects a number", Token(TokenType::IDENTIFIER, "hex", 0));
            double val = std::get<double>(arguments[0]);
            int64_t n = static_cast<int64_t>(std::trunc(val));
            if (n == 0) return std::string("0x0");
            bool neg = n < 0;
            uint64_t u = neg ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
            const char* hexChars = "0123456789abcdef";
            std::string digits;
            while (u > 0) {
                digits = hexChars[u & 0xF] + digits;
                u >>= 4;
            }
            return (neg ? std::string("-0x") : std::string("0x")) + digits;
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

void Interpreter::pushScopeWithEnclosing(Environment* enclosing) {
    pushScope(enclosing);
}

void Interpreter::executeBlockWithThis(
    const std::vector<std::unique_ptr<Stmt>>& statements,
    const Value& thisValue,
    const std::vector<std::string>& params,
    const std::vector<Value>& arguments
) {
    pushScope();

    try {
        for (size_t i = 0; i < params.size(); ++i) {
            environment.define(
                params[i],
                arguments[i]
            );
        }

        environment.define("this", thisValue);

        executeBlock(statements);

    } catch (const ReturnSignal&) {
        popScope();
        throw;
    } catch (...) {
        popScope();
        throw;
    }

    popScope();
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
    catch (...) {

        popScope();

        throw;
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

    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value) != 0.0;
    }

    if (std::holds_alternative<std::string>(value)) {
        return !std::get<std::string>(value).empty();
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

// =========================================================================
// Expression dispatch (double dispatch via expr->accept(*this))
// =========================================================================

Value Interpreter::evaluate(const Expr* expr) {
    return expr->accept(*this);
}

// =========================================================================
// Statement dispatch (double dispatch via stmt->accept(*this))
// =========================================================================

void Interpreter::execute(const Stmt* stmt) {
    stmt->accept(*this);
}

// =========================================================================
// ExprVisitor — visit methods
// =========================================================================

Value Interpreter::visitAssignmentExpr(const AssignmentExpr& expr) {

    Value value = evaluate(expr.value.get());

    environment.assign(
        expr.name,
        value,
        expr.nameToken
    );

    return value;
}

Value Interpreter::visitCallExpr(const CallExpr& expr) {

    Value callee = evaluate(expr.callee.get());

    std::vector<Value> arguments;

    arguments.reserve(expr.arguments.size());

    for (const auto& argument : expr.arguments) {
        arguments.push_back(
            evaluate(argument.get())
        );
    }

    return invoke(
        callee,
        arguments,
        expr.paren
    );
}

Value Interpreter::visitVariableExpr(const VariableExpr& expr) {

    return environment.get(
        expr.name,
        expr.nameToken
    );
}

Value Interpreter::visitArrayExpr(const ArrayExpr& expr) {

    auto result = std::make_shared<Array>();
    result->elements.reserve(expr.elements.size());

    for (const auto& element : expr.elements) {
        result->elements.push_back(
            evaluate(element.get())
        );
    }

    return result;
}

Value Interpreter::visitIndexExpr(const IndexExpr& expr) {

    Value target = evaluate(expr.array.get());
    Value indexValue = evaluate(expr.index.get());

    if (!std::holds_alternative<double>(indexValue)) {
        throw RuntimeError(
            "Index must be a number",
            expr.bracket
        );
    }

    double rawIndex = std::get<double>(indexValue);

    if (rawIndex < 0 || std::floor(rawIndex) != rawIndex) {
        throw RuntimeError(
            "Index must be a non-negative integer",
            expr.bracket
        );
    }

    size_t index = static_cast<size_t>(rawIndex);

    // --- string indexing ---
    if (std::holds_alternative<std::string>(target)) {
        const auto& str = std::get<std::string>(target);
        if (index >= str.size()) {
            throw RuntimeError("Index out of bounds", expr.bracket);
        }
        return std::string(1, str[index]);
    }

    // --- array indexing ---
    if (std::holds_alternative<std::shared_ptr<Array>>(target)) {
        const auto& elements =
            std::get<std::shared_ptr<Array>>(target)->elements;
        if (index >= elements.size()) {
            throw RuntimeError("Index out of bounds", expr.bracket);
        }
        return elements[index];
    }

    throw RuntimeError(
        "Indexing requires an array or string",
        expr.bracket
    );
}

Value Interpreter::visitLiteralExpr(const LiteralExpr& expr) {

    if (std::holds_alternative<std::string>(expr.value)) {
        std::string str = std::get<std::string>(expr.value);
        return interpolateString(str);
    }

    return expr.value;
}

Value Interpreter::visitGroupingExpr(const GroupingExpr& expr) {

    return evaluate(expr.expression.get());
}

Value Interpreter::visitUnaryExpr(const UnaryExpr& expr) {

    Value right = evaluate(expr.right.get());

    if (std::holds_alternative<double>(right)) {

        double value = std::get<double>(right);

        switch (expr.op.type) {

        case TokenType::MINUS:
            return -value;

        default:
            break;
        }
    }

    if (expr.op.type == TokenType::NOT) {
        return !isTruthy(right);
    }

    throw RuntimeError(
        "Invalid unary operand",
        expr.op
    );
}

Value Interpreter::visitBinaryExpr(const BinaryExpr& expr) {

    Value left = evaluate(expr.left.get());

    // --- logical short-circuit operators ---
    if (expr.op.type == TokenType::AND) {
        // a && b: if a is falsy, return a; else evaluate and return b
        if (!isTruthy(left)) return left;
        return evaluate(expr.right.get());
    }

    if (expr.op.type == TokenType::OR) {
        // a || b: if a is truthy, return a; else evaluate and return b
        if (isTruthy(left)) return left;
        return evaluate(expr.right.get());
    }

    Value right = evaluate(expr.right.get());

    // --- string concatenation ---
    if (expr.op.type == TokenType::PLUS) {

        // string + string
        if (std::holds_alternative<std::string>(left) &&
            std::holds_alternative<std::string>(right)) {
            return std::get<std::string>(left) + std::get<std::string>(right);
        }

        // array + array (merge)
        if (std::holds_alternative<std::shared_ptr<Array>>(left) &&
            std::holds_alternative<std::shared_ptr<Array>>(right)) {
            auto result = std::make_shared<Array>();
            const auto& leftArr = std::get<std::shared_ptr<Array>>(left)->elements;
            const auto& rightArr = std::get<std::shared_ptr<Array>>(right)->elements;
            result->elements.reserve(leftArr.size() + rightArr.size());
            result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
            result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
            return result;
        }

        // array + value (append) — must be before any+string to avoid
        // valueToString promotion
        if (std::holds_alternative<std::shared_ptr<Array>>(left)) {
            auto result = std::make_shared<Array>();
            const auto& leftArr = std::get<std::shared_ptr<Array>>(left)->elements;
            result->elements.reserve(leftArr.size() + 1);
            result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
            result->elements.push_back(right);
            return result;
        }

        // value + array (prepend)
        if (std::holds_alternative<std::shared_ptr<Array>>(right)) {
            auto result = std::make_shared<Array>();
            const auto& rightArr = std::get<std::shared_ptr<Array>>(right)->elements;
            result->elements.reserve(1 + rightArr.size());
            result->elements.push_back(left);
            result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
            return result;
        }

        // string + any
        if (std::holds_alternative<std::string>(left)) {
            return std::get<std::string>(left) + valueToString(right);
        }

        // any + string
        if (std::holds_alternative<std::string>(right)) {
            return valueToString(left) + std::get<std::string>(right);
        }
    }

    // --- equality (all types) ---
    if (expr.op.type == TokenType::EQUAL_EQUAL || expr.op.type == TokenType::NOT_EQUAL) {

        auto doCompare = [](const Value& a, const Value& b) -> bool {
            // Same type
            if (a.index() != b.index()) return false;

            if (std::holds_alternative<std::nullptr_t>(a)) return true;
            if (std::holds_alternative<double>(a)) return std::get<double>(a) == std::get<double>(b);
            if (std::holds_alternative<bool>(a)) return std::get<bool>(a) == std::get<bool>(b);
            if (std::holds_alternative<std::string>(a)) return std::get<std::string>(a) == std::get<std::string>(b);

            // Reference types — compare by pointer identity
            if (std::holds_alternative<std::shared_ptr<Array>>(a))
                return std::get<std::shared_ptr<Array>>(a) == std::get<std::shared_ptr<Array>>(b);
            if (std::holds_alternative<std::shared_ptr<Callable>>(a))
                return std::get<std::shared_ptr<Callable>>(a) == std::get<std::shared_ptr<Callable>>(b);
            if (std::holds_alternative<std::shared_ptr<ObjectInstance>>(a))
                return std::get<std::shared_ptr<ObjectInstance>>(a) == std::get<std::shared_ptr<ObjectInstance>>(b);

            return false;
        };

        bool equal = doCompare(left, right);
        return expr.op.type == TokenType::EQUAL_EQUAL ? equal : !equal;
    }

    // --- numeric operations ---
    if (
        std::holds_alternative<double>(left)
        &&
        std::holds_alternative<double>(right)
    ) {

        double l = std::get<double>(left);

        double r = std::get<double>(right);

        switch (expr.op.type) {

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
                    expr.op
                );
            }

            return l / r;

        case TokenType::POWER:
            return std::pow(l, r);

        case TokenType::MODULO:
            if (r == 0) {
                throw RuntimeError(
                    "Modulo by zero",
                    expr.op
                );
            }
            return std::fmod(l, r);

        case TokenType::LESS:
            return l < r;

        case TokenType::LESS_EQUAL:
            return l <= r;

        case TokenType::GREATER:
            return l > r;

        case TokenType::GREATER_EQUAL:
            return l >= r;

        default:
            break;
        }
    }

    throw RuntimeError(
        "Invalid binary operands",
        expr.op
    );
}

Value Interpreter::visitPropertyExpr(const PropertyExpr& expr) {

    Value obj = evaluate(expr.object.get());

    if (!std::holds_alternative<
            std::shared_ptr<ObjectInstance>
        >(obj)) {

        throw RuntimeError(
            "Can only access properties on objects",
            expr.dot
        );
    }

    const auto& instance =
        std::get<std::shared_ptr<ObjectInstance>>(obj);

    auto it = instance->properties.find(expr.property);

    if (it != instance->properties.end()) {
        return it->second;
    }

    if (instance->classDefinition) {
        // Walk the class hierarchy (current class → parent → ...)
        ObjectClass* cls = instance->classDefinition.get();
        while (cls) {
            auto methodIt = cls->methods.find(expr.property);
            if (methodIt != cls->methods.end()) {
                // Return a bound method that sets 'this' when called
                struct BoundMethodCallable : public Callable {
                    std::shared_ptr<VoraFunction> method;
                    std::shared_ptr<ObjectInstance> instance;

                    BoundMethodCallable(std::shared_ptr<VoraFunction> m, std::shared_ptr<ObjectInstance> inst)
                        : method(std::move(m)), instance(std::move(inst)) {}

                    Value call(Interpreter& interpreter, const std::vector<Value>& arguments) override {
                        interpreter.pushScope(method->closure().get());

                        for (size_t i = 0; i < method->params().size(); ++i) {
                            interpreter.getEnvironment().define(
                                method->params()[i],
                                arguments[i]
                            );
                        }

                        interpreter.getEnvironment().define("this", instance);

                        try {
                            interpreter.executeBlock(method->body()->statements);
                        } catch (const ReturnSignal& signal) {
                            interpreter.popScope();
                            return signal.value;
                        }

                        interpreter.popScope();
                        return nullptr;
                    }
                };

                return std::make_shared<BoundMethodCallable>(methodIt->second, instance);
            }
            cls = cls->parentClass.get();
        }
    }

    throw RuntimeError(
        "Undefined property: " + expr.property,
        expr.dot
    );
}

Value Interpreter::visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) {

    Value obj = evaluate(expr.object.get());
    Value value = evaluate(expr.value.get());

    if (!std::holds_alternative<
            std::shared_ptr<ObjectInstance>
        >(obj)) {

        throw RuntimeError(
            "Can only set properties on objects",
            expr.dot
        );
    }

    const auto& instance =
        std::get<std::shared_ptr<ObjectInstance>>(obj);

    instance->properties[expr.property] = value;

    return value;
}

Value Interpreter::visitThisExpr(const ThisExpr& expr) {

    try {
        return environment.get("this", expr.keyword);
    } catch (const RuntimeError&) {
        throw RuntimeError(
            "Cannot use 'this' outside of object method",
            expr.keyword
        );
    }
}

Value Interpreter::visitTernaryExpr(const TernaryExpr& expr) {
    Value condition = evaluate(expr.condition.get());

    if (isTruthy(condition)) {
        return evaluate(expr.thenBranch.get());
    } else {
        return evaluate(expr.elseBranch.get());
    }
}

Value Interpreter::visitIncDecExpr(const IncDecExpr& expr) {

    double delta = (expr.op.type == TokenType::PLUS_PLUS) ? 1.0 : -1.0;

    // --- Variable target ---
    if (auto var = dynamic_cast<const VariableExpr*>(expr.target.get())) {
        Value current = environment.get(var->name, var->nameToken);
        if (!std::holds_alternative<double>(current)) {
            throw RuntimeError("Can only increment/decrement numbers", expr.op);
        }
        double oldVal = std::get<double>(current);
        double newVal = oldVal + delta;
        environment.assign(var->name, newVal, var->nameToken);
        return expr.isPrefix ? newVal : oldVal;
    }

    // --- Property target ---
    if (auto prop = dynamic_cast<const PropertyExpr*>(expr.target.get())) {
        Value obj = evaluate(prop->object.get());
        if (!std::holds_alternative<std::shared_ptr<ObjectInstance>>(obj)) {
            throw RuntimeError("Can only increment/decrement properties on objects", expr.op);
        }
        auto instance = std::get<std::shared_ptr<ObjectInstance>>(obj);
        auto it = instance->properties.find(prop->property);
        if (it == instance->properties.end()) {
            throw RuntimeError("Undefined property: " + prop->property, expr.op);
        }
        if (!std::holds_alternative<double>(it->second)) {
            throw RuntimeError("Can only increment/decrement numbers", expr.op);
        }
        double oldVal = std::get<double>(it->second);
        double newVal = oldVal + delta;
        instance->properties[prop->property] = newVal;
        return expr.isPrefix ? newVal : oldVal;
    }

    throw RuntimeError("Invalid increment/decrement target", expr.op);
}

// =========================================================================
// StmtVisitor — visit methods
// =========================================================================

void Interpreter::visitBlockStmt(const BlockStmt& stmt) {

    executeBlock(stmt.statements);
}

void Interpreter::visitIfStmt(const IfStmt& stmt) {

    if (isTruthy(
            evaluate(stmt.condition.get())
        )) {

        execute(stmt.thenBranch.get());
    }
    else if (stmt.elseBranch) {

        execute(stmt.elseBranch.get());
    }
}

void Interpreter::visitWhileStmt(const WhileStmt& stmt) {

    while (isTruthy(
        evaluate(stmt.condition.get())
    )) {

        try {
            execute(stmt.body.get());
        } catch (const BreakSignal&) {
            break;
        } catch (const ContinueSignal&) {
            // continue to next iteration
        }
    }
}

void Interpreter::visitForStmt(const ForStmt& stmt) {

    Value iterable = evaluate(stmt.iterable.get());

    // --- string iteration ---
    if (std::holds_alternative<std::string>(iterable)) {
        const auto& str = std::get<std::string>(iterable);
        for (size_t i = 0; i < str.size(); ++i) {
            pushScope();
            std::string ch(1, str[i]);
            environment.define(stmt.variable, ch);
            try {
                execute(stmt.body.get());
            } catch (const BreakSignal&) {
                popScope();
                break;
            } catch (const ContinueSignal&) {
                popScope();
                continue;
            }
            popScope();
        }
        return;
    }

    // --- array iteration (includes range() results) ---
    if (std::holds_alternative<std::shared_ptr<Array>>(iterable)) {
        const auto& array =
            std::get<std::shared_ptr<Array>>(iterable);

        for (const auto& element : array->elements) {
            pushScope();
            environment.define(stmt.variable, element);
            try {
                execute(stmt.body.get());
            } catch (const BreakSignal&) {
                popScope();
                break;
            } catch (const ContinueSignal&) {
                popScope();
                continue;
            }
            popScope();
        }
        return;
    }

    throw RuntimeError(
        "Can only iterate over arrays, strings, or range()",
        stmt.forToken
    );
}

void Interpreter::visitReturnStmt(const ReturnStmt& stmt) {

    throw ReturnSignal{
        evaluate(stmt.value.get())
    };
}

void Interpreter::visitFuncStmt(const FuncStmt& stmt) {

    auto function = std::make_shared<VoraFunction>(
        stmt.name,
        stmt.params,
        stmt.body,
        captureClosure()
    );

    defineBinding(
        stmt.name,
        function
    );
}

void Interpreter::visitObjStmt(const ObjStmt& stmt) {

    auto objClass = std::make_shared<ObjectClass>();
    objClass->className = stmt.name;
    objClass->params = stmt.params;
    objClass->body = stmt.body;
    objClass->parentClassName = stmt.parentName;

    // Resolve parent class at runtime
    if (!stmt.parentName.empty()) {
        Token nameToken(TokenType::IDENTIFIER, stmt.name, 0);
        try {
            Value parentVal = environment.get(stmt.parentName, nameToken);
            if (auto* callablePtr = std::get_if<std::shared_ptr<Callable>>(&parentVal)) {
                objClass->parentClass = (*callablePtr)->getClassDef();
            }
            if (!objClass->parentClass) {
                throw RuntimeError(
                    "'" + stmt.parentName + "' is not a valid parent class",
                    nameToken
                );
            }
        } catch (const RuntimeError&) {
            throw RuntimeError(
                "Parent class not found: " + stmt.parentName,
                nameToken
            );
        }
    }

    for (const auto& methodStmt : stmt.methods) {
        if (auto funcStmt = dynamic_cast<const FuncStmt*>(methodStmt.get())) {
            objClass->methods[funcStmt->name] =
                std::make_shared<VoraFunction>(
                    funcStmt->name,
                    funcStmt->params,
                    funcStmt->body,
                    captureClosure()
                );
        }
    }

    auto constructor = std::make_shared<VoraFunction>(
        stmt.name,
        stmt.params,
        stmt.body,
        captureClosure()
    );

    struct ObjectConstructorCallable : public Callable {
        std::shared_ptr<ObjectClass> classDef;
        std::shared_ptr<VoraFunction> constructorFn;

        ObjectConstructorCallable(
            std::shared_ptr<ObjectClass> c,
            std::shared_ptr<VoraFunction> f
        ) : classDef(c), constructorFn(f) {}

        std::shared_ptr<ObjectClass> getClassDef() const override {
            return classDef;
        }

        Value call(
            Interpreter& interpreter,
            const std::vector<Value>& arguments
        ) override {
            auto instance = std::make_shared<ObjectInstance>();
            instance->className = classDef->className;
            instance->classDefinition = classDef;

            // Call parent constructors root-first (top of chain to direct parent)
            if (classDef->parentClass) {
                std::vector<ObjectClass*> parentChain;
                for (ObjectClass* p = classDef->parentClass.get(); p; p = p->parentClass.get()) {
                    parentChain.insert(parentChain.begin(), p);
                }
                for (auto* parent : parentChain) {
                    interpreter.executeBlockWithThis(
                        parent->body->statements,
                        instance,
                        parent->params,
                        arguments
                    );
                }
            }

            interpreter.executeBlockWithThis(
                constructorFn->body()->statements,
                instance,
                constructorFn->params(),
                arguments
            );

            return instance;
        }
    };

    auto constructorCallable =
        std::make_shared<ObjectConstructorCallable>(objClass, constructor);

    defineBinding(stmt.name, constructorCallable);
}

void Interpreter::visitBreakStmt(const BreakStmt& /*stmt*/) {
    throw BreakSignal{};
}

void Interpreter::visitContinueStmt(const ContinueStmt& /*stmt*/) {
    throw ContinueSignal{};
}

void Interpreter::visitThrowStmt(const ThrowStmt& stmt) {
    Value value = evaluate(stmt.value.get());
    throw ThrowSignal{std::move(value)};
}

void Interpreter::visitTryStmt(const TryStmt& stmt) {
    // Helper: execute finally if present
    auto runFinally = [&]() {
        if (stmt.finallyBlock) {
            execute(stmt.finallyBlock.get());
        }
    };

    try {
        execute(stmt.tryBlock.get());
    } catch (const ThrowSignal& thrown) {
        // User-thrown value — attempt to catch it
        if (stmt.catchBlock) {
            pushScope();
            environment.define(stmt.catchVar, thrown.value);
            try {
                execute(stmt.catchBlock.get());
            } catch (...) {
                popScope();
                runFinally();
                throw;
            }
            popScope();
        } else {
            runFinally();
            throw;
        }
    } catch (const RuntimeError& err) {
        // Built-in runtime error — attempt to catch it
        if (stmt.catchBlock) {
            pushScope();
            // Bind error message as a string to the catch variable
            environment.define(stmt.catchVar, std::string(err.what()));
            try {
                execute(stmt.catchBlock.get());
            } catch (...) {
                popScope();
                runFinally();
                throw;
            }
            popScope();
        } else {
            // No catch clause — run finally then re-throw
            runFinally();
            throw;
        }
    } catch (const ReturnSignal&) {
        runFinally();
        throw;
    } catch (const BreakSignal&) {
        runFinally();
        throw;
    } catch (const ContinueSignal&) {
        runFinally();
        throw;
    }

    // Normal completion — run finally
    runFinally();
}

void Interpreter::visitLetStmt(const LetStmt& stmt) {

    defineBinding(
        stmt.name,
        evaluate(stmt.initializer.get())
    );
}

void Interpreter::visitExprStmt(const ExprStmt& stmt) {

    Value result = evaluate(stmt.expression.get());

    if (config.repl()) {
        printValue(result);
        std::cout << std::endl;
    }
}

// =========================================================================
// Block execution (scope management)
// =========================================================================

void Interpreter::executeBlock(
    const std::vector<std::unique_ptr<Stmt>>& statements
) {

    pushScope();

    try {

        for (const auto& stmt : statements) {
            execute(stmt.get());
        }
    }
    catch (...) {

        popScope();

        throw;
    }

    popScope();
}

// =========================================================================
// String interpolation
// =========================================================================

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
                    Value val;
                    // support dotted names like "this.name"
                    if (varName.find('.') == std::string::npos) {
                        val = environment.get(varName, Token(TokenType::IDENTIFIER, varName, 0));
                    } else {
                        size_t start = 0;
                        bool found = true;
                        // get base identifier
                        size_t dot = varName.find('.', start);
                        std::string base = varName.substr(start, dot - start);
                        try {
                            val = environment.get(base, Token(TokenType::IDENTIFIER, base, 0));
                        } catch (const RuntimeError&) {
                            found = false;
                        }

                        while (found && dot != std::string::npos) {
                            start = dot + 1;
                            dot = varName.find('.', start);
                            std::string prop = varName.substr(start, (dot == std::string::npos) ? std::string::npos : dot - start);

                            if (!std::holds_alternative<std::shared_ptr<ObjectInstance>>(val)) {
                                found = false;
                                break;
                            }

                            auto instance = std::get<std::shared_ptr<ObjectInstance>>(val);

                            auto it = instance->properties.find(prop);
                            if (it != instance->properties.end()) {
                                val = it->second;
                                continue;
                            }

                            if (instance->classDefinition) {
                                // Walk the class hierarchy
                                ObjectClass* cls = instance->classDefinition.get();
                                bool methodFound = false;
                                while (cls) {
                                    auto methodIt = cls->methods.find(prop);
                                    if (methodIt != cls->methods.end()) {
                                        val = methodIt->second;
                                        methodFound = true;
                                        break;
                                    }
                                    cls = cls->parentClass.get();
                                }
                                if (methodFound) continue;
                            }

                            found = false;
                            break;
                        }

                        if (!found) {
                            throw RuntimeError("Undefined property: " + varName, Token(TokenType::IDENTIFIER, varName, 0));
                        }
                    }

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
