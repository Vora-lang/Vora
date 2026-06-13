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
    size_t frameBase;            // base of this frame's locals on the stack (index)
    const Chunk* callerChunk;    // chunk to restore on return
    std::shared_ptr<VoraFunction> function;  // executing function (for upvalue access)
};

class VM {
    static constexpr int MAX_LOCALS_PER_FRAME = 256;

    std::vector<Value> stack;
    size_t stackTopIndex = 0;

    // Global variable storage — integer-indexed for O(1) access without string hashing.
    // globalNames[slot] → name, globalValues[slot] → value (parallel arrays).
    // globalIndex maps name → slot for define-time lookup only.
    std::vector<std::string> globalNames;
    std::vector<Value> globalValues;
    std::vector<bool> globalDefined;
    std::unordered_map<std::string, int> globalIndex;
    const Chunk* currentChunk = nullptr;
    const uint8_t* ip = nullptr;

    // Call frame stack
    std::vector<CallFrame> frames;
    size_t frameBaseIndex = 0;   // current frame's base (stack for top-level)

    // Catch handler stack: { catchIp, catchFrameBase, chunk, frameCount }
    struct CatchHandler {
        const uint8_t* targetIp;
        size_t targetFrameBase;   // stack index where handler was registered
        const Chunk* chunk;        // which chunk the handler lives in
        size_t frameCount;         // frames.size() when handler was registered
    };
    std::vector<CatchHandler> catchHandlers;

    // Number of arguments actually passed to the current function call.
    // Used by OP_DEFAULT_PARAM to determine whether a default value should
    // be evaluated for a parameter slot.
    uint8_t currentArgCount = 0;

    // True when throwException() has just routed an exception through a catch
    // handler and the value is on the stack. OP_FINALLY_END checks this to
    // decide whether to re-throw.
    bool exceptionInFlight = false;

    // Open upvalues: stack slot index → heap-allocated value (for closures)
    std::unordered_map<size_t, std::shared_ptr<Value>> openUpvalues;
    std::shared_ptr<Value> captureUpvalue(size_t slotIndex);

public:
    VM();
    ~VM() = default;

    InterpretResult interpret(const Chunk& chunk);

    void defineNative(const std::string& name, int arity,
                      std::function<Value(const std::vector<Value>&)> fn);

    // Initialize global slots from compiler's interning table.
    void initGlobals(const std::vector<std::string>& names);

    // Copy global state into this VM. Used to give a temporary VM
    // (e.g. constructor execution) access to the same globals.
    void adoptGlobals(const std::vector<std::string>& names,
                      const std::vector<Value>& values,
                      const std::vector<bool>& defined,
                      const std::unordered_map<std::string, int>& index);

    // Native error reporting — set by builtins (e.g. assert) that need to
    // throw through the VM exception mechanism rather than calling exit().
    // callValue() checks this after every native call and routes through
    // throwException() so the error is catchable by try/catch.
    bool nativeError = false;
    Value nativeErrorValue;

    // Value operations are in value_ops.h (free functions).

    // Exception throw (public — callable from native function context).
    // Returns true if caught by a catch handler, false if uncaught.
    bool throwException(const Value& value);

private:
    InterpretResult run();

    void resetStack();

    void push(Value value);
    Value pop();
    Value& peek(int distance = 0);
    const Value& peek(int distance = 0) const;

    void runtimeError(const std::string& message);
    void runtimeError(const std::string& message, int line, int column);
    bool runtimeErrorOrThrow(const std::string& message);  // true if caught

    uint16_t readShort();
    uint8_t readByte();

    // Call frame management
    bool callValue(const Value& callee, uint8_t argCount);
    InterpretResult callVoraFunction(const std::shared_ptr<VoraFunction>& func,
                                      const std::vector<Value>& args);

    // Stack trace: walks the CallFrame chain to produce a traceback string.
    std::string captureStackTrace() const;

    // Pre-unwind snapshot: throwException() captures the stack trace before
    // popping frames. runtimeError() uses this if non-empty (exception path)
    // and captures fresh otherwise (direct error path).
    std::string lastErrorStackTrace;
};

} // namespace vora
