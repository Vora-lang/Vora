#include "vm.h"
#include "opcode.h"

#include "../runtime/native_function.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace vora {

// =========================================================================
// Construction
// =========================================================================

VM::VM() {
    resetStack();
}

void VM::resetStack() {
    stackTop = stack;
}

void VM::push(Value value) {
    *stackTop = value;
    stackTop++;
}

Value VM::pop() {
    stackTop--;
    return *stackTop;
}

Value VM::peek(int distance) {
    return *(stackTop - 1 - distance);
}

uint16_t VM::readShort() {
    uint16_t result = static_cast<uint16_t>(*ip) | (static_cast<uint16_t>(*(ip + 1)) << 8);
    ip += 2;
    return result;
}

void VM::runtimeError(const std::string& message) {
    // Find the line at the current instruction pointer
    size_t offset = ip - currentChunk->code.data();
    int line = 0;
    size_t pos = 0;
    const auto& rleLines = currentChunk->lines;
    for (size_t i = 0; i < rleLines.size(); i += 2) {
        int runLen = rleLines[i];
        if (offset < pos + static_cast<size_t>(runLen)) {
            line = rleLines[i + 1];
            break;
        }
        pos += runLen;
    }
    std::cerr << "VM RuntimeError [" << line << "]: " << message << std::endl;
}

void VM::runtimeError(const std::string& message, int line, int column) {
    std::cerr << "VM RuntimeError [" << line << ":" << column << "]: " << message << std::endl;
}

// =========================================================================
// Native function registration
// =========================================================================

void VM::defineNative(const std::string& name, int arity,
                      std::function<Value(const std::vector<Value>&)> fn) {
    globals[name] = std::make_shared<NativeFunction>(name, arity, std::move(fn));
}

// =========================================================================
// Value operations
// =========================================================================

bool VM::isTruthy(const Value& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) return false;
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value);
    if (std::holds_alternative<double>(value)) return std::get<double>(value) != 0.0;
    if (std::holds_alternative<std::string>(value)) return !std::get<std::string>(value).empty();
    return true; // arrays, callables, objects are always truthy
}

bool VM::valuesEqual(const Value& a, const Value& b) {
    if (a.index() != b.index()) return false;

    if (std::holds_alternative<std::nullptr_t>(a)) return true;
    if (std::holds_alternative<double>(a)) return std::get<double>(a) == std::get<double>(b);
    if (std::holds_alternative<bool>(a)) return std::get<bool>(a) == std::get<bool>(b);
    if (std::holds_alternative<std::string>(a)) return std::get<std::string>(a) == std::get<std::string>(b);

    // Reference types — pointer identity
    if (std::holds_alternative<std::shared_ptr<Array>>(a))
        return std::get<std::shared_ptr<Array>>(a) == std::get<std::shared_ptr<Array>>(b);
    if (std::holds_alternative<std::shared_ptr<Callable>>(a))
        return std::get<std::shared_ptr<Callable>>(a) == std::get<std::shared_ptr<Callable>>(b);
    if (std::holds_alternative<std::shared_ptr<ObjectInstance>>(a))
        return std::get<std::shared_ptr<ObjectInstance>>(a) == std::get<std::shared_ptr<ObjectInstance>>(b);

    return false;
}

int VM::valuesCompare(const Value& a, const Value& b) {
    // Returns -1 if a < b, 0 if equal, 1 if a > b
    // Used for OP_LESS etc.
    if (!std::holds_alternative<double>(a) || !std::holds_alternative<double>(b)) {
        return 0; // non-numeric comparison not supported; left to caller
    }
    double da = std::get<double>(a);
    double db = std::get<double>(b);
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

Value VM::addValues(const Value& a, const Value& b) {
    // Number + number
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        return std::get<double>(a) + std::get<double>(b);
    }

    // String + string (concatenation)
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return std::get<std::string>(a) + std::get<std::string>(b);
    }

    // String + any / any + string
    if (std::holds_alternative<std::string>(a)) {
        return std::get<std::string>(a) + valueToString(b);
    }
    if (std::holds_alternative<std::string>(b)) {
        return valueToString(a) + std::get<std::string>(b);
    }

    // Array + array (merge)
    if (std::holds_alternative<std::shared_ptr<Array>>(a) && std::holds_alternative<std::shared_ptr<Array>>(b)) {
        auto result = std::make_shared<Array>();
        const auto& leftArr = std::get<std::shared_ptr<Array>>(a)->elements;
        const auto& rightArr = std::get<std::shared_ptr<Array>>(b)->elements;
        result->elements.reserve(leftArr.size() + rightArr.size());
        result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
        result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
        return result;
    }

    // Array + value (append)
    if (std::holds_alternative<std::shared_ptr<Array>>(a)) {
        auto result = std::make_shared<Array>();
        const auto& leftArr = std::get<std::shared_ptr<Array>>(a)->elements;
        result->elements.reserve(leftArr.size() + 1);
        result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
        result->elements.push_back(b);
        return result;
    }

    // Value + array (prepend)
    if (std::holds_alternative<std::shared_ptr<Array>>(b)) {
        auto result = std::make_shared<Array>();
        const auto& rightArr = std::get<std::shared_ptr<Array>>(b)->elements;
        result->elements.reserve(1 + rightArr.size());
        result->elements.push_back(a);
        result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
        return result;
    }

    runtimeError("Invalid operands for +");
    return nullptr;
}

// =========================================================================
// Main entry
// =========================================================================

InterpretResult VM::interpret(const Chunk& chunk) {
    currentChunk = &chunk;
    ip = chunk.code.data();
    resetStack();
    return run();
}

// =========================================================================
// Execution loop
// =========================================================================

InterpretResult VM::run() {
#define READ_BYTE() (*ip++)
#define READ_CONSTANT() (currentChunk->constants[READ_BYTE()])
#define BINARY_OP(op, opName) \
    do { \
        Value b = pop(); \
        Value a = pop(); \
        if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) { \
            double da = std::get<double>(a); \
            double db = std::get<double>(b); \
            push((da op db)); \
        } else { \
            runtimeError("Invalid operands for " opName); \
            return InterpretResult::RUNTIME_ERROR; \
        } \
    } while (false)

    for (;;) {
        OpCode instruction = static_cast<OpCode>(READ_BYTE());

        switch (instruction) {
            // --- Constants ---
            case OpCode::OP_CONSTANT: {
                push(READ_CONSTANT());
                break;
            }
            case OpCode::OP_NULL:  push(nullptr); break;
            case OpCode::OP_TRUE:  push(true);  break;
            case OpCode::OP_FALSE: push(false); break;

            // --- Stack ---
            case OpCode::OP_POP: pop(); break;

            // --- Unary ---
            case OpCode::OP_NEGATE: {
                Value v = peek(0);
                if (std::holds_alternative<double>(v)) {
                    push(-std::get<double>(pop()));
                } else {
                    runtimeError("Negation requires a number");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_NOT: {
                push(!isTruthy(pop()));
                break;
            }

            // --- Arithmetic ---
            case OpCode::OP_ADD: {
                Value b = pop();
                Value a = pop();
                push(addValues(a, b));
                break;
            }
            case OpCode::OP_SUBTRACT: BINARY_OP(-, "subtraction"); break;
            case OpCode::OP_MULTIPLY: BINARY_OP(*, "multiplication"); break;
            case OpCode::OP_DIVIDE: {
                Value b = pop();
                Value a = pop();
                if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
                    double db = std::get<double>(b);
                    if (db == 0) {
                        runtimeError("Division by zero");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(std::get<double>(a) / db);
                } else {
                    runtimeError("Invalid operands for division");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_MODULO: {
                Value b = pop();
                Value a = pop();
                if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
                    double db = std::get<double>(b);
                    if (db == 0) {
                        runtimeError("Modulo by zero");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(std::fmod(std::get<double>(a), db));
                } else {
                    runtimeError("Invalid operands for modulo");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_POWER: {
                Value b = pop();
                Value a = pop();
                if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
                    push(std::pow(std::get<double>(a), std::get<double>(b)));
                } else {
                    runtimeError("Invalid operands for power (**)");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }

            // --- Comparison ---
            case OpCode::OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(valuesEqual(a, b));
                break;
            }
            case OpCode::OP_NOT_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(!valuesEqual(a, b));
                break;
            }
            case OpCode::OP_LESS: {
                Value b = pop();
                Value a = pop();
                if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
                    push(std::get<double>(a) < std::get<double>(b));
                } else {
                    push(false);
                }
                break;
            }
            case OpCode::OP_LESS_EQUAL: {
                Value b = pop();
                Value a = pop();
                if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
                    push(std::get<double>(a) <= std::get<double>(b));
                } else {
                    push(false);
                }
                break;
            }
            case OpCode::OP_GREATER: {
                Value b = pop();
                Value a = pop();
                if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
                    push(std::get<double>(a) > std::get<double>(b));
                } else {
                    push(false);
                }
                break;
            }
            case OpCode::OP_GREATER_EQUAL: {
                Value b = pop();
                Value a = pop();
                if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
                    push(std::get<double>(a) >= std::get<double>(b));
                } else {
                    push(false);
                }
                break;
            }

            // --- Globals ---
            case OpCode::OP_DEFINE_GLOBAL: {
                std::string name = std::get<std::string>(READ_CONSTANT());
                Value value = pop();
                globals[name] = value;
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                std::string name = std::get<std::string>(READ_CONSTANT());
                auto it = globals.find(name);
                if (it != globals.end()) {
                    push(it->second);
                } else {
                    runtimeError("Undefined variable '" + name + "'");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                std::string name = std::get<std::string>(READ_CONSTANT());
                Value value = peek(0);
                auto it = globals.find(name);
                if (it != globals.end()) {
                    it->second = value;
                } else {
                    runtimeError("Undefined variable '" + name + "'");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }

            // --- Control flow ---
            case OpCode::OP_JUMP: {
                uint16_t offset = readShort();
                ip += offset;
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = readShort();
                if (!isTruthy(peek(0))) {
                    ip += offset;
                }
                break;
            }
            case OpCode::OP_LOOP: {
                uint16_t offset = readShort();
                ip -= offset;
                break;
            }

            // --- Functions ---
            case OpCode::OP_CALL: {
                uint8_t argCount = READ_BYTE();
                // Collect arguments in reverse order (they're on the stack)
                std::vector<Value> arguments(argCount);
                for (int i = argCount - 1; i >= 0; i--) {
                    arguments[static_cast<size_t>(i)] = pop();
                }
                Value callee = pop();

                if (auto native = std::dynamic_pointer_cast<NativeFunction>(
                        std::get<std::shared_ptr<Callable>>(callee))) {
                    if (native->arity() >= 0 && argCount != static_cast<uint8_t>(native->arity())) {
                        runtimeError("Wrong arity: expected " +
                            std::to_string(native->arity()) + " but got " +
                            std::to_string(argCount));
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(native->callDirectly(arguments));
                } else {
                    runtimeError("Can only call native functions in VM Phase 1");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_RETURN: {
                // Exit the VM. In Phase 1, this just stops.
                return InterpretResult::OK;
            }

            // --- Arrays ---
            case OpCode::OP_ARRAY: {
                uint8_t count = READ_BYTE();
                auto arr = std::make_shared<Array>();
                arr->elements.reserve(count);
                // Elements are on the stack in push order, so pop in reverse
                size_t startIdx = stackTop - stack - count;
                for (uint8_t i = 0; i < count; i++) {
                    arr->elements.push_back(stack[startIdx + i]);
                }
                stackTop -= count;
                push(arr);
                break;
            }
            case OpCode::OP_INDEX: {
                Value indexVal = pop();
                Value target = pop();

                if (!std::holds_alternative<double>(indexVal)) {
                    runtimeError("Index must be a number");
                    return InterpretResult::RUNTIME_ERROR;
                }

                double rawIndex = std::get<double>(indexVal);
                if (rawIndex < 0 || std::floor(rawIndex) != rawIndex) {
                    runtimeError("Index must be a non-negative integer");
                    return InterpretResult::RUNTIME_ERROR;
                }

                size_t index = static_cast<size_t>(rawIndex);

                if (std::holds_alternative<std::string>(target)) {
                    const auto& str = std::get<std::string>(target);
                    if (index >= str.size()) {
                        runtimeError("Index out of bounds");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(std::string(1, str[index]));
                } else if (std::holds_alternative<std::shared_ptr<Array>>(target)) {
                    const auto& elements =
                        std::get<std::shared_ptr<Array>>(target)->elements;
                    if (index >= elements.size()) {
                        runtimeError("Index out of bounds");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(elements[index]);
                } else {
                    runtimeError("Indexing requires an array or string");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_SET_INDEX: {
                Value value = pop();
                Value indexVal = pop();
                Value target = pop();

                if (!std::holds_alternative<double>(indexVal)) {
                    runtimeError("Index must be a number");
                    return InterpretResult::RUNTIME_ERROR;
                }

                double rawIndex = std::get<double>(indexVal);
                if (rawIndex < 0 || std::floor(rawIndex) != rawIndex) {
                    runtimeError("Index must be a non-negative integer");
                    return InterpretResult::RUNTIME_ERROR;
                }

                size_t index = static_cast<size_t>(rawIndex);

                if (std::holds_alternative<std::shared_ptr<Array>>(target)) {
                    auto& elements =
                        std::get<std::shared_ptr<Array>>(target)->elements;
                    if (index >= elements.size()) {
                        runtimeError("Index out of bounds");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    elements[index] = value;
                    push(value);
                } else {
                    runtimeError("Index assignment requires an array");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }

            case OpCode::OP_GET_PROPERTY:
            case OpCode::OP_SET_PROPERTY:
                runtimeError("Object properties not supported in VM Phase 1");
                return InterpretResult::RUNTIME_ERROR;

            default:
                runtimeError("Unknown opcode");
                return InterpretResult::RUNTIME_ERROR;
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

} // namespace vora
