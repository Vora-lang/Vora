#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "chunk.h"
#include "../runtime/value.h"

namespace vora {

enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR,
};

class VM {
    static constexpr int STACK_MAX = 1024;

    Value stack[STACK_MAX];
    Value* stackTop;

    std::unordered_map<std::string, Value> globals;
    const Chunk* currentChunk = nullptr;
    const uint8_t* ip = nullptr;

public:
    VM();
    ~VM() = default;

    InterpretResult interpret(const Chunk& chunk);

    void defineNative(const std::string& name, int arity,
                      std::function<Value(const std::vector<Value>&)> fn);

    // Value operations — shared by VM and potentially other backends.
    // Not static so they can call runtimeError().
    Value addValues(const Value& a, const Value& b);
    static bool isTruthy(const Value& value);
    static bool valuesEqual(const Value& a, const Value& b);
    int valuesCompare(const Value& a, const Value& b);

private:
    InterpretResult run();

    void resetStack();

    void push(Value value);
    Value pop();
    Value peek(int distance = 0);

    void runtimeError(const std::string& message);
    void runtimeError(const std::string& message, int line, int column);

    // Read a 16-bit little-endian short from the instruction pointer.
    uint16_t readShort();
};

} // namespace vora
