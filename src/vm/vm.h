#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "chunk.h"
#include "../runtime/value.h"

namespace vora {

class VoraFunction;  // forward declaration

enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR,
};

// A call frame for function execution
struct CallFrame {
    const uint8_t* returnIp;    // where to resume in caller
    Value* frameBase;            // base of this frame's locals on the stack
    const Chunk* callerChunk;    // chunk to restore on return
    std::shared_ptr<VoraFunction> function;  // executing function (for upvalue access)
};

class VM {
    static constexpr int STACK_MAX = 1024;

    Value stack[STACK_MAX];
    Value* stackTop;

    std::unordered_map<std::string, Value> globals;
    const Chunk* currentChunk = nullptr;
    const uint8_t* ip = nullptr;

    // Call frame stack
    std::vector<CallFrame> frames;
    Value* frameBase = nullptr;  // current frame's base (stack for top-level)

    // Catch handler stack: { catchIp, catchFrameBase }
    struct CatchHandler {
        const uint8_t* targetIp;
        Value* targetFrameBase;
    };
    std::vector<CatchHandler> catchHandlers;

    // Open upvalues: stack slot address → heap-allocated value (for closures)
    std::unordered_map<Value*, std::shared_ptr<Value>> openUpvalues;
    std::shared_ptr<Value> captureUpvalue(Value* slot);

public:
    VM();
    ~VM() = default;

    InterpretResult interpret(const Chunk& chunk);

    void defineNative(const std::string& name, int arity,
                      std::function<Value(const std::vector<Value>&)> fn);

    // Value operations
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
    bool runtimeErrorOrThrow(const std::string& message);  // true if caught
    bool throwException(const Value& value);  // returns true if caught

    uint16_t readShort();
    uint8_t readByte();

    // Call frame management
    bool callValue(const Value& callee, uint8_t argCount);
    InterpretResult callVoraFunction(const std::shared_ptr<VoraFunction>& func,
                                      const std::vector<Value>& args);
};

} // namespace vora
