#include "vm.h"
#include "opcode.h"

#include "../runtime/native_function.h"
#include "../runtime/vora_function.h"
#include "../vm/compiler.h"  // for FunctionPrototype, ClassData

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace vora {

// =========================================================================
// Construction
// =========================================================================

VM::VM() {
    resetStack();
    frameBase = stack;
}

void VM::resetStack() {
    stackTop = stack;
}

void VM::push(Value value) {
    if (stackTop >= stack + STACK_MAX) {
        std::cerr << "VM: Fatal stack overflow (STACK_MAX=" << STACK_MAX << ")" << std::endl;
        std::exit(1);
    }
    *stackTop = std::move(value);
    stackTop++;
}

Value VM::pop() {
    stackTop--;
    return std::move(*stackTop);
}

Value& VM::peek(int distance) {
    return *(stackTop - 1 - distance);
}

const Value& VM::peek(int distance) const {
    return *(stackTop - 1 - distance);
}

uint16_t VM::readShort() {
    uint16_t result = static_cast<uint16_t>(*ip) | (static_cast<uint16_t>(*(ip + 1)) << 8);
    ip += 2;
    return result;
}

uint8_t VM::readByte() {
    return *ip++;
}

void VM::runtimeError(const std::string& message) {
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

bool VM::runtimeErrorOrThrow(const std::string& message) {
    if (throwException(message)) return true;  // caught by catch handler
    runtimeError(message);
    return false;  // not caught
}

std::shared_ptr<Value> VM::captureUpvalue(Value* slot) {
    // Check if this slot already has an open upvalue
    auto it = openUpvalues.find(slot);
    if (it != openUpvalues.end()) {
        return it->second;
    }
    // Create a new heap-allocated value copying the current stack value
    auto uv = std::make_shared<Value>(*slot);
    openUpvalues[slot] = uv;
    return uv;
}

bool VM::throwException(const Value& value) {
    // Walk up through catch handlers. We peek the handler (don't pop it)
    // so that OP_POP_CATCH at the target can pop it. If we popped here,
    // nested try blocks would fail because OP_POP_CATCH at the inner
    // handler target would accidentally pop the outer handler.
    while (!catchHandlers.empty()) {
        auto handler = catchHandlers.back();  // peek only

        // Unwind stack to handler's frame base
        stackTop = handler.targetFrameBase;

        // Push the thrown value
        push(value);

        // Jump to catch handler
        ip = handler.targetIp;
        currentChunk = handler.chunk;

        // Unwind call frames to the level where the handler was registered.
        while (frames.size() > handler.frameCount) {
            // Clear open upvalues for the frame being unwound
            Value* frameEnd = frames.back().frameBase + 256;
            auto it = openUpvalues.begin();
            while (it != openUpvalues.end()) {
                if (it->first >= frames.back().frameBase && it->first < frameEnd) {
                    it = openUpvalues.erase(it);
                } else {
                    ++it;
                }
            }
            frames.pop_back();
        }
        if (!frames.empty()) {
            frameBase = frames.back().frameBase;
        } else {
            frameBase = stack;  // top-level
        }

        exceptionInFlight = true;
        return true;  // caught — handler will be popped by OP_POP_CATCH at target
    }

    return false;  // not caught
}

// =========================================================================
// Native function registration
// =========================================================================

void VM::defineNative(const std::string& name, int arity,
                      std::function<Value(const std::vector<Value>&)> fn) {
    int slot;
    auto it = globalIndex.find(name);
    if (it != globalIndex.end()) {
        slot = it->second;
    } else {
        slot = static_cast<int>(globalNames.size());
        globalNames.push_back(name);
        globalValues.push_back(nullptr);
        globalDefined.push_back(false);
        globalIndex[name] = slot;
    }
    globalValues[static_cast<size_t>(slot)] =
        std::make_shared<NativeFunction>(name, arity, std::move(fn));
    globalDefined[static_cast<size_t>(slot)] = true;
}

void VM::initGlobals(const std::vector<std::string>& names) {
    globalNames = names;
    globalValues.resize(names.size(), nullptr);
    globalDefined.resize(names.size(), false);
    globalIndex.clear();
    for (size_t i = 0; i < names.size(); i++) {
        globalIndex[names[i]] = static_cast<int>(i);
    }

    // Register internal builtin used by for-in loop desugaring.
    defineNative("_vora_len", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (std::holds_alternative<std::shared_ptr<Array>>(arg)) {
                return static_cast<double>(
                    std::get<std::shared_ptr<Array>>(arg)->elements.size());
            }
            if (std::holds_alternative<std::string>(arg)) {
                return static_cast<double>(std::get<std::string>(arg).size());
            }
            return 0.0;
        });
}

void VM::adoptGlobals(const std::vector<std::string>& names,
                       const std::vector<Value>& values,
                       const std::vector<bool>& defined,
                       const std::unordered_map<std::string, int>& index) {
    globalNames = names;
    globalValues = values;
    globalDefined = defined;
    globalIndex = index;
}

// =========================================================================
// Value operations
// =========================================================================

bool VM::isTruthy(const Value& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) return false;
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value);
    if (std::holds_alternative<double>(value)) return std::get<double>(value) != 0.0;
    if (std::holds_alternative<std::string>(value)) return !std::get<std::string>(value).empty();
    return true;
}

bool VM::valuesEqual(const Value& a, const Value& b) {
    if (a.index() != b.index()) return false;

    if (std::holds_alternative<std::nullptr_t>(a)) return true;
    if (std::holds_alternative<double>(a)) return std::get<double>(a) == std::get<double>(b);
    if (std::holds_alternative<bool>(a)) return std::get<bool>(a) == std::get<bool>(b);
    if (std::holds_alternative<std::string>(a)) return std::get<std::string>(a) == std::get<std::string>(b);

    if (std::holds_alternative<std::shared_ptr<Array>>(a))
        return std::get<std::shared_ptr<Array>>(a) == std::get<std::shared_ptr<Array>>(b);
    if (std::holds_alternative<std::shared_ptr<Callable>>(a))
        return std::get<std::shared_ptr<Callable>>(a) == std::get<std::shared_ptr<Callable>>(b);
    if (std::holds_alternative<std::shared_ptr<ObjectInstance>>(a))
        return std::get<std::shared_ptr<ObjectInstance>>(a) == std::get<std::shared_ptr<ObjectInstance>>(b);

    return false;
}

int VM::valuesCompare(const Value& a, const Value& b) {
    if (!std::holds_alternative<double>(a) || !std::holds_alternative<double>(b)) {
        return 0;
    }
    double da = std::get<double>(a);
    double db = std::get<double>(b);
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

Value VM::addValues(const Value& a, const Value& b) {
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        return std::get<double>(a) + std::get<double>(b);
    }

    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return std::get<std::string>(a) + std::get<std::string>(b);
    }

    if (std::holds_alternative<std::string>(a)) {
        return std::get<std::string>(a) + valueToString(b);
    }
    if (std::holds_alternative<std::string>(b)) {
        return valueToString(a) + std::get<std::string>(b);
    }

    if (std::holds_alternative<std::shared_ptr<Array>>(a) && std::holds_alternative<std::shared_ptr<Array>>(b)) {
        auto result = std::make_shared<Array>();
        const auto& leftArr = std::get<std::shared_ptr<Array>>(a)->elements;
        const auto& rightArr = std::get<std::shared_ptr<Array>>(b)->elements;
        result->elements.reserve(leftArr.size() + rightArr.size());
        result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
        result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
        return result;
    }

    if (std::holds_alternative<std::shared_ptr<Array>>(a)) {
        auto result = std::make_shared<Array>();
        const auto& leftArr = std::get<std::shared_ptr<Array>>(a)->elements;
        result->elements.reserve(leftArr.size() + 1);
        result->elements.insert(result->elements.end(), leftArr.begin(), leftArr.end());
        result->elements.push_back(b);
        return result;
    }

    if (std::holds_alternative<std::shared_ptr<Array>>(b)) {
        auto result = std::make_shared<Array>();
        const auto& rightArr = std::get<std::shared_ptr<Array>>(b)->elements;
        result->elements.reserve(1 + rightArr.size());
        result->elements.push_back(a);
        result->elements.insert(result->elements.end(), rightArr.begin(), rightArr.end());
        return result;
    }

    runtimeErrorOrThrow("Invalid operands for +");
    return nullptr;
}

// =========================================================================
// Call frame management
// =========================================================================

bool VM::callValue(const Value& callee, uint8_t argCount) {
    if (std::holds_alternative<std::shared_ptr<Callable>>(callee)) {
        auto callable = std::get<std::shared_ptr<Callable>>(callee);

        // Native function
        if (auto native = std::dynamic_pointer_cast<NativeFunction>(callable)) {
            // Bound method: dispatch through the current VM so globals
            // and builtins remain accessible (no temp VM).
            if (native->isBoundMethod()) {
                auto instance = native->getBoundInstance();
                const auto* proto = native->getBoundMethodProto();
                if (!proto) {
                    runtimeErrorOrThrow("Bound method has no prototype");
                    return false;
                }

                // Pop args (in reverse), then pop the callee
                std::vector<Value> arguments(argCount);
                for (int i = argCount - 1; i >= 0; i--) {
                    arguments[static_cast<size_t>(i)] = pop();
                }
                pop(); // callee (bound method wrapper)

                // Save frame base BEFORE pushing — it must point at 'this'
                Value* newFrameBase = stackTop;

                // Push 'this' (bound instance) then method arguments as locals
                push(instance);
                for (const auto& a : arguments) {
                    push(a);
                }

                // Set up call frame directly (inline callVoraFunction logic).
                // We skip the usual arity check because 'this' is not counted
                // in the method prototype's arity.
                frames.push_back({ip, frameBase, currentChunk, nullptr});
                currentChunk = &proto->chunk;
                ip = currentChunk->code.data();
                frameBase = newFrameBase;  // points to instance at slot 0
                return true;
            }

            if (native->arity() >= 0 && argCount != static_cast<uint8_t>(native->arity())) {
                runtimeErrorOrThrow("Wrong arity: expected " +
                    std::to_string(native->arity()) + " but got " +
                    std::to_string(argCount));
                return false;
            }

            // Collect arguments from stack (in reverse)
            std::vector<Value> arguments(argCount);
            for (int i = argCount - 1; i >= 0; i--) {
                arguments[static_cast<size_t>(i)] = pop();
            }
            pop(); // pop the callable itself

            push(native->callDirectly(arguments));
            return true;
        }

        // Vora function
        if (auto voraFn = std::dynamic_pointer_cast<VoraFunction>(callable)) {
            // Collect arguments from stack (in reverse)
            std::vector<Value> arguments(argCount);
            for (int i = argCount - 1; i >= 0; i--) {
                arguments[static_cast<size_t>(i)] = pop();
            }
            pop(); // pop the callable itself

            return callVoraFunction(voraFn, arguments) == InterpretResult::OK;
        }
    }

    runtimeErrorOrThrow("Can only call functions");
    return false;
}

InterpretResult VM::callVoraFunction(const std::shared_ptr<VoraFunction>& func,
                                      const std::vector<Value>& args) {
    // Check arity
    if (static_cast<size_t>(func->arity()) != args.size()) {
        runtimeErrorOrThrow("Wrong arity: expected " +
            std::to_string(func->arity()) + " but got " +
            std::to_string(args.size()));
        return InterpretResult::RUNTIME_ERROR;
    }

    // Get the function's compiled prototype
    const FunctionPrototype* proto = func->getPrototype();
    if (!proto) {
        runtimeErrorOrThrow("Function has no compiled body");
        return InterpretResult::RUNTIME_ERROR;
    }

    // Push return address, caller chunk, and function onto call frame stack
    frames.push_back({ip, frameBase, currentChunk, func});

    // Switch to function's chunk
    currentChunk = &proto->chunk;
    ip = currentChunk->code.data();
    frameBase = stackTop;  // locals start after the current stack top

    // Bind parameters as locals at frameBase
    for (size_t i = 0; i < args.size(); i++) {
        push(args[i]);
    }

    return InterpretResult::OK;
}

// =========================================================================
// Main entry
// =========================================================================

InterpretResult VM::interpret(const Chunk& chunk) {
    currentChunk = &chunk;
    ip = chunk.code.data();
    resetStack();
    frameBase = stack;
    frames.clear();
    catchHandlers.clear();
    return run();
}

// =========================================================================
// Execution loop
// =========================================================================

InterpretResult VM::run() {
#define READ_BYTE() (*ip++)
#define READ_CONSTANT() (currentChunk->constants[READ_BYTE()])
#define RUNTIME_ERROR_OR_THROW(msg) \
    do { \
        std::string _errMsg = (msg); \
        if (throwException(_errMsg)) { goto dispatch; } \
        runtimeError(_errMsg); \
        return InterpretResult::RUNTIME_ERROR; \
    } while (false)
#define BINARY_OP(op, opName) \
    do { \
        Value b = pop(); \
        Value a = pop(); \
        if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) { \
            double da = std::get<double>(a); \
            double db = std::get<double>(b); \
            push((da op db)); \
        } else { \
            RUNTIME_ERROR_OR_THROW("Invalid operands for " opName); \
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
            case OpCode::OP_POPN: {
                uint8_t count = READ_BYTE();
                stackTop -= count;
                break;
            }

            // --- Locals ---
            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frameBase[slot]);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frameBase[slot] = peek(0);
                break;
            }

            case OpCode::OP_DUP: {
                push(peek(0));
                break;
            }

            // --- Unary ---
            case OpCode::OP_NEGATE: {
                if (std::holds_alternative<double>(peek(0))) {
                    push(-std::get<double>(pop()));
                } else {
                    RUNTIME_ERROR_OR_THROW("Negation requires a number");
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
            case OpCode::OP_DIVIDE: {
                Value b = pop();
                Value a = pop();
                if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
                    double db = std::get<double>(b);
                    if (db == 0) {
                        RUNTIME_ERROR_OR_THROW("Division by zero");
                    }
                    push(std::get<double>(a) / db);
                } else {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for division");
                }
                break;
            }
            case OpCode::OP_MODULO: {
                Value b = pop();
                Value a = pop();
                if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
                    double db = std::get<double>(b);
                    if (db == 0) {
                        RUNTIME_ERROR_OR_THROW("Modulo by zero");
                    }
                    push(std::fmod(std::get<double>(a), db));
                } else {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for modulo");
                }
                break;
            }
            case OpCode::OP_POWER: {
                Value b = pop();
                Value a = pop();
                if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
                    push(std::pow(std::get<double>(a), std::get<double>(b)));
                } else {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for power (**)");
                }
                break;
            }

            // --- Fast numeric arithmetic (with type checks) ---
            case OpCode::OP_SUB_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!std::holds_alternative<double>(aVal) || !std::holds_alternative<double>(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for -");
                }
                push(std::get<double>(aVal) - std::get<double>(bVal));
                break;
            }
            case OpCode::OP_MUL_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!std::holds_alternative<double>(aVal) || !std::holds_alternative<double>(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for *");
                }
                push(std::get<double>(aVal) * std::get<double>(bVal));
                break;
            }
            case OpCode::OP_DIV_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!std::holds_alternative<double>(aVal) || !std::holds_alternative<double>(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for /");
                }
                double b = std::get<double>(bVal);
                if (b == 0) {
                    RUNTIME_ERROR_OR_THROW("Division by zero");
                }
                push(std::get<double>(aVal) / b);
                break;
            }
            case OpCode::OP_MOD_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!std::holds_alternative<double>(aVal) || !std::holds_alternative<double>(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for %");
                }
                double b = std::get<double>(bVal);
                if (b == 0) {
                    RUNTIME_ERROR_OR_THROW("Modulo by zero");
                }
                push(std::fmod(std::get<double>(aVal), b));
                break;
            }
            case OpCode::OP_LESS_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!std::holds_alternative<double>(aVal) || !std::holds_alternative<double>(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for <");
                }
                push(std::get<double>(aVal) < std::get<double>(bVal));
                break;
            }
            case OpCode::OP_LESS_EQ_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!std::holds_alternative<double>(aVal) || !std::holds_alternative<double>(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for <=");
                }
                push(std::get<double>(aVal) <= std::get<double>(bVal));
                break;
            }
            case OpCode::OP_GREATER_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!std::holds_alternative<double>(aVal) || !std::holds_alternative<double>(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for >");
                }
                push(std::get<double>(aVal) > std::get<double>(bVal));
                break;
            }
            case OpCode::OP_GREATER_EQ_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!std::holds_alternative<double>(aVal) || !std::holds_alternative<double>(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for >=");
                }
                push(std::get<double>(aVal) >= std::get<double>(bVal));
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

            // --- Globals (integer-indexed, no string hashing) ---
            case OpCode::OP_DEFINE_GLOBAL: {
                uint8_t slot = READ_BYTE();
                if (slot >= globalValues.size()) {
                    RUNTIME_ERROR_OR_THROW("OP_DEFINE_GLOBAL: slot out of range");
                }
                globalValues[slot] = pop();
                globalDefined[slot] = true;
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                uint8_t slot = READ_BYTE();
                if (slot >= globalValues.size() || !globalDefined[slot]) {
                    RUNTIME_ERROR_OR_THROW("Undefined variable '" +
                        (slot < globalNames.size() ? globalNames[slot] : std::to_string(slot)) + "'");
                }
                push(globalValues[slot]);
                break;
            }
            case OpCode::OP_GET_GLOBAL_SAFE: {
                uint8_t slot = READ_BYTE();
                uint8_t fallbackIndex = READ_BYTE();
                if (fallbackIndex >= currentChunk->constants.size()) {
                    RUNTIME_ERROR_OR_THROW("OP_GET_GLOBAL_SAFE: constant index out of bounds");
                }
                if (slot < globalValues.size() && globalDefined[slot]) {
                    push(globalValues[slot]);
                } else {
                    push(currentChunk->constants[fallbackIndex]);
                }
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                uint8_t slot = READ_BYTE();
                if (slot >= globalValues.size() || !globalDefined[slot]) {
                    RUNTIME_ERROR_OR_THROW("Undefined variable '" +
                        (slot < globalNames.size() ? globalNames[slot] : std::to_string(slot)) + "'");
                }
                globalValues[slot] = peek(0);
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
                Value callee = peek(argCount);
                if (!callValue(callee, argCount)) {
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_CLOSURE: {
                uint8_t constIndex = READ_BYTE();
                Value protoVal = currentChunk->constants[constIndex];
                if (!std::holds_alternative<std::shared_ptr<FunctionPrototype>>(protoVal)) {
                    RUNTIME_ERROR_OR_THROW("OP_CLOSURE: expected function prototype in constant pool");
                }
                auto protoPtr = std::get<std::shared_ptr<FunctionPrototype>>(protoVal);
                auto function = std::make_shared<VoraFunction>(
                    protoPtr->name, protoPtr->arity, protoPtr.get());

                // Read upvalue descriptors and capture upvalues
                uint8_t upvalueCount = READ_BYTE();
                for (uint8_t u = 0; u < upvalueCount; u++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        // Capture local: snapshot the current value.
                        // Each closure gets its own independent copy to match
                        // the interpreter's Environment::snapshot() semantics.
                        Value* slot = frameBase + index;
                        function->upvalues.push_back(
                            std::make_shared<Value>(*slot));
                    } else {
                        // Capture upvalue from enclosing function
                        if (!frames.empty()) {
                            auto& encFn = frames.back().function;
                            if (encFn && index < encFn->upvalues.size()) {
                                function->upvalues.push_back(encFn->upvalues[index]);
                            } else {
                                function->upvalues.push_back(
                                    std::make_shared<Value>(nullptr));
                            }
                        } else {
                            function->upvalues.push_back(
                                std::make_shared<Value>(nullptr));
                        }
                    }
                }

                push(function);
                break;
            }
            case OpCode::OP_RETURN: {
                // Return from current function or top-level
                if (frames.empty()) {
                    // Top-level return
                    return InterpretResult::OK;
                }

                // Get return value (top of stack)
                Value returnValue = pop();

                // Pop all locals by restoring stack to frame base
                stackTop = frameBase;

                // Clear any open upvalues for this frame (locals going out of scope)
                for (auto it = openUpvalues.begin(); it != openUpvalues.end(); ) {
                    if (it->first >= frameBase && it->first < frameBase + 256) {
                        it = openUpvalues.erase(it);
                    } else {
                        ++it;
                    }
                }

                // Restore caller's frame
                CallFrame frame = frames.back();
                frames.pop_back();
                ip = frame.returnIp;
                frameBase = frame.frameBase;
                currentChunk = frame.callerChunk;

                // Push return value
                push(returnValue);
                break;
            }

            // --- Upvalues ---
            case OpCode::OP_GET_UPVALUE: {
                uint8_t idx = READ_BYTE();
                std::shared_ptr<VoraFunction> fn;
                if (!frames.empty()) {
                    fn = frames.back().function;
                }
                if (fn && idx < fn->upvalues.size()) {
                    push(*(fn->upvalues[idx]));
                } else {
                    RUNTIME_ERROR_OR_THROW("Invalid upvalue index");
                }
                break;
            }
            case OpCode::OP_SET_UPVALUE: {
                uint8_t idx = READ_BYTE();
                std::shared_ptr<VoraFunction> fn;
                if (!frames.empty()) {
                    fn = frames.back().function;
                }
                if (fn && idx < fn->upvalues.size()) {
                    *(fn->upvalues[idx]) = peek(0);
                } else {
                    RUNTIME_ERROR_OR_THROW("Invalid upvalue index");
                }
                break;
            }
            case OpCode::OP_CLOSE_UPVALUE: {
                uint8_t localSlot = READ_BYTE();
                Value* slot = frameBase + localSlot;
                auto it = openUpvalues.find(slot);
                if (it != openUpvalues.end()) {
                    // Sync current stack value to the heap-allocated upvalue
                    *(it->second) = *slot;
                }
                break;
            }

            // --- Arrays ---
            case OpCode::OP_ARRAY: {
                uint8_t count = READ_BYTE();
                auto arr = std::make_shared<Array>();
                arr->elements.reserve(count);
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
                    RUNTIME_ERROR_OR_THROW("Index must be a number");
                }

                double rawIndex = std::get<double>(indexVal);
                if (rawIndex < 0 || std::floor(rawIndex) != rawIndex) {
                    RUNTIME_ERROR_OR_THROW("Index must be a non-negative integer");
                }

                size_t index = static_cast<size_t>(rawIndex);

                if (std::holds_alternative<std::string>(target)) {
                    const auto& str = std::get<std::string>(target);
                    if (index >= str.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    push(std::string(1, str[index]));
                } else if (std::holds_alternative<std::shared_ptr<Array>>(target)) {
                    const auto& elements =
                        std::get<std::shared_ptr<Array>>(target)->elements;
                    if (index >= elements.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    push(elements[index]);
                } else {
                    RUNTIME_ERROR_OR_THROW("Indexing requires an array or string");
                }
                break;
            }
            case OpCode::OP_SET_INDEX: {
                Value value = pop();
                Value indexVal = pop();
                Value target = pop();

                if (!std::holds_alternative<double>(indexVal)) {
                    RUNTIME_ERROR_OR_THROW("Index must be a number");
                }

                double rawIndex = std::get<double>(indexVal);
                if (rawIndex < 0 || std::floor(rawIndex) != rawIndex) {
                    RUNTIME_ERROR_OR_THROW("Index must be a non-negative integer");
                }

                size_t index = static_cast<size_t>(rawIndex);

                if (std::holds_alternative<std::shared_ptr<Array>>(target)) {
                    auto& elements =
                        std::get<std::shared_ptr<Array>>(target)->elements;
                    if (index >= elements.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    elements[index] = value;
                    push(value);
                } else if (std::holds_alternative<std::string>(target)) {
                    RUNTIME_ERROR_OR_THROW("Cannot assign to string index (strings are immutable)");
                } else {
                    RUNTIME_ERROR_OR_THROW("Index assignment requires an array");
                }
                break;
            }

            // --- Objects ---
            case OpCode::OP_GET_PROPERTY: {
                uint8_t nameIndex = READ_BYTE();
                std::string propName = std::get<std::string>(currentChunk->constants[nameIndex]);
                Value obj = pop();

                if (std::holds_alternative<std::shared_ptr<ObjectInstance>>(obj)) {
                    auto instance = std::get<std::shared_ptr<ObjectInstance>>(obj);
                    // Check instance properties
                    auto it = instance->properties.find(propName);
                    if (it != instance->properties.end()) {
                        push(it->second);
                        break;
                    }
                    // Check class methods
                    if (instance->classDefinition) {
                        ObjectClass* cls = instance->classDefinition.get();
                        while (cls) {
                            auto methodIt = cls->methods.find(propName);
                            if (methodIt != cls->methods.end()) {
                                // Create bound method: a NativeFunction carrying the
                                // instance + prototype. callValue() detects it and
                                // dispatches through the current VM so globals and
                                // builtins remain accessible.
                                auto methodFn = methodIt->second;
                                const FunctionPrototype* mp = methodFn->getPrototype();
                                if (!mp) {
                                    RUNTIME_ERROR_OR_THROW("Method has no compiled body");
                                }
                                auto bound = std::make_shared<NativeFunction>(
                                    propName, methodFn->arity(),
                                    /*no-op lambda — callValue intercepts before calling this*/
                                    [](const std::vector<Value>&) -> Value { return nullptr; });
                                bound->markAsBoundMethod(instance, mp);
                                push(bound);
                                // Found method — break out of while + switch
                                goto method_found;
                            }
                            cls = cls->parentClass.get();
                        }
                        method_found:
                        if (cls) break;  // found method
                    }
                    RUNTIME_ERROR_OR_THROW("Undefined property: " + propName);
                } else {
                    RUNTIME_ERROR_OR_THROW("Can only access properties on objects");
                }
                break;
            }
            case OpCode::OP_SET_PROPERTY: {
                uint8_t nameIndex = READ_BYTE();
                std::string propName = std::get<std::string>(currentChunk->constants[nameIndex]);
                Value value = pop();
                Value obj = pop();

                if (std::holds_alternative<std::shared_ptr<ObjectInstance>>(obj)) {
                    auto instance = std::get<std::shared_ptr<ObjectInstance>>(obj);
                    instance->properties[propName] = value;
                    push(value);
                } else {
                    RUNTIME_ERROR_OR_THROW("Can only set properties on objects");
                }
                break;
            }
            case OpCode::OP_CLASS: {
                uint8_t classIndex = READ_BYTE();
                Value classDataVal = currentChunk->constants[classIndex];

                if (!std::holds_alternative<std::shared_ptr<ClassData>>(classDataVal)) {
                    RUNTIME_ERROR_OR_THROW("OP_CLASS: expected class data in constant pool");
                }
                auto cdPtr = std::get<std::shared_ptr<ClassData>>(classDataVal);
                const auto& cd = *cdPtr;

                // Build ObjectClass
                auto objClass = std::make_shared<ObjectClass>();
                objClass->className = cd.name;
                objClass->params = cd.params;
                objClass->ctorProto = cd.ctor;

                // Resolve parent class from globals (integer-indexed lookup)
                if (!cd.parentName.empty()) {
                    auto it = globalIndex.find(cd.parentName);
                    if (it != globalIndex.end()) {
                        int pSlot = it->second;
                        if (static_cast<size_t>(pSlot) < globalValues.size() &&
                            globalDefined[static_cast<size_t>(pSlot)]) {
                            const auto& parentVal = globalValues[static_cast<size_t>(pSlot)];
                            if (auto callable = std::get_if<std::shared_ptr<Callable>>(&parentVal)) {
                                if (*callable) {
                                    objClass->parentClass = (*callable)->getClassDef();
                                }
                            }
                        }
                    }
                }

                // Store method VoraFunctions
                for (const auto& methodProto : cd.methods) {
                    auto methodFn = std::make_shared<VoraFunction>(
                        methodProto->name, methodProto->arity, methodProto.get());
                    objClass->methods[methodProto->name] = methodFn;
                }

                // Create constructor VoraFunction
                auto ctorFn = std::make_shared<VoraFunction>(
                    cd.ctor->name, cd.ctor->arity, cd.ctor.get());

                // The constructor callable that creates instances.
                // Parent constructors are called root-first, then the child's
                // constructor. Each parent gets args[0..parentParamCount].
                auto ctorCallable = std::make_shared<NativeFunction>(
                    cd.name, static_cast<int>(cd.params.size()),
                    [objClass, ctorFn, globalsNames = globalNames, globalsValues = globalValues, globalsDefined = globalDefined, globalsIndex = globalIndex](const std::vector<Value>& args) -> Value {
                        auto instance = std::make_shared<ObjectInstance>();
                        instance->className = objClass->className;
                        instance->classDefinition = objClass;

                        // Helper: run a constructor prototype on the instance
                        auto runCtor = [&instance, &globalsNames, &globalsValues, &globalsDefined, &globalsIndex](const FunctionPrototype* cp,
                                                   const std::vector<Value>& ctorArgs) {
                            if (!cp) return;
                            VM tempVm;
                            tempVm.adoptGlobals(globalsNames, globalsValues, globalsDefined, globalsIndex);
                            tempVm.currentChunk = &cp->chunk;
                            tempVm.ip = cp->chunk.code.data();
                            tempVm.frameBase = tempVm.stackTop;
                            tempVm.push(instance);  // 'this' at slot 0
                            for (const auto& a : ctorArgs) {
                                tempVm.push(a);
                            }
                            tempVm.run();
                        };

                        // Run parent constructors root-first.
                        // Each parent gets ALL arguments (it uses the params it needs).
                        if (objClass->parentClass) {
                            std::vector<ObjectClass*> chain;
                            for (ObjectClass* p = objClass->parentClass.get(); p; p = p->parentClass.get()) {
                                chain.insert(chain.begin(), p);
                            }
                            for (auto* parent : chain) {
                                if (parent->ctorProto) {
                                    runCtor(parent->ctorProto.get(), args);
                                }
                            }
                        }

                        // Run own constructor with all args
                        if (ctorFn->getPrototype()) {
                            runCtor(ctorFn->getPrototype(), args);
                        }

                        return instance;
                    });

                ctorCallable->setClassDef(objClass);
                push(ctorCallable);
                break;
            }

            // --- Exception handling ---
            case OpCode::OP_PUSH_CATCH: {
                uint16_t catchOffset = readShort();
                catchHandlers.push_back({ip + catchOffset, frameBase, currentChunk, frames.size()});
                break;
            }
            case OpCode::OP_POP_CATCH: {
                if (!catchHandlers.empty()) {
                    catchHandlers.pop_back();
                }
                break;
            }
            case OpCode::OP_CLEAR_EXCEPTION: {
                exceptionInFlight = false;
                break;
            }
            case OpCode::OP_THROW: {
                if (!throwException(peek(0))) {
                    runtimeError("Uncaught exception: " + valueToString(peek(0)));
                    return InterpretResult::RUNTIME_ERROR;
                }
                goto dispatch;
            }
            case OpCode::OP_FINALLY_END: {
                // If we just ran a finally block after an exception was
                // routed through a catch handler, re-throw the exception
                // so enclosing handlers can catch it.
                if (exceptionInFlight) {
                    exceptionInFlight = false;
                    Value ex = pop();  // exception value pushed by throwException
                    if (!throwException(ex)) {
                        runtimeError("Uncaught exception: " + valueToString(ex));
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    // throwException set exceptionInFlight again if re-caught
                    goto dispatch;
                }
                break;
            }

            default:
                RUNTIME_ERROR_OR_THROW("Unknown opcode");
        }
        dispatch:;
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

} // namespace vora
