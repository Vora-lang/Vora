#pragma once

#include <csignal>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "chunk.h"
#include "../common/error_reporter.h"
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
    GcPtr<VoraFunction> function;  // executing function (for upvalue access)
    uint64_t paramProvidedMask = 0;  // bit i=1 if param slot i has caller-provided value
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
    // Track which global slots were modified since last clearDirtyGlobals().
    // Used by mergeGlobalsTo() to iterate only dirty slots instead of O(n).
    std::vector<int> dirtyGlobalSlots_;
    std::vector<bool> globalDirtyFlags_;   // parallel to globalValues, grows on demand
    void markGlobalDirty(int slot);
    void clearDirtyGlobals();
    // Catch handler stack: { targetIp, targetFrameBase, targetSlot, chunk, frameCount }
    struct CatchHandler {
        const uint8_t* targetIp;
        size_t targetFrameBase;   // stack index at handler registration (frame's own base)
        size_t targetSlot;         // stack index where to push the exception value
        const Chunk* chunk;        // which chunk the handler lives in
        size_t frameCount;         // frames.size() when handler was registered
    };
    std::vector<CatchHandler> catchHandlers;

    // Per-call spread element counters. Each OP_PUSH_SPREAD pushes 0 onto
    // this stack; OP_SPREAD increments the back entry; OP_CALL_N pops it.
    // A stack (not a single int) is needed to correctly handle nested calls
    // like f(...g(...a)) where inner and outer spreads overlap.
    std::vector<int> spreadCountStack_;

    // Number of arguments actually passed to the current function call.
    // Used by OP_DEFAULT_PARAM to determine whether a default value should
    // be evaluated for a parameter slot.
    uint8_t currentArgCount = 0;

    // True when throwException() has just routed an exception through a catch
    // handler and the value is on the stack. OP_FINALLY_END checks this to
    // decide whether to re-throw.
    bool exceptionInFlight = false;

    // Open upvalues: stack slot index → Upvalue with stack pointer indirection
    std::unordered_map<size_t, std::shared_ptr<Upvalue>> openUpvalues;
    std::shared_ptr<Upvalue> captureUpvalue(size_t slotIndex);

    // Interrupt flag — set by signal handler (SIGINT / Ctrl+C).
    // Checked at the top of each opcode dispatch in run().
    static volatile sig_atomic_t interruptFlag;

public:
    VM();
    ~VM() = default;

    // --- Runtime execution state (public for native function access) ---
    const Chunk* currentChunk = nullptr;  // currently executing chunk
    const uint8_t* ip = nullptr;          // instruction pointer
    size_t frameBaseIndex = 0;            // current frame's stack base index
    std::vector<CallFrame> frames;        // call frame stack

    // --- Generator support ---
    bool pendingResume = false;
    const uint8_t* pendingIp = nullptr;
    const Chunk* pendingChunk = nullptr;
    GcPtr<Generator> pendingGenerator;
    GcPtr<Generator> currentGenerator;  // non-null while executing a generator

    InterpretResult interpret(const Chunk& chunk);

    void defineNative(const std::string& name, int arity,
                      std::function<Value(const std::vector<Value>&)> fn);

    // Initialize global slots from compiler's interning table.
    void initGlobals(const std::vector<std::string>& names);

    // Read the current global name table (for seeding compiler in REPL).
    const std::vector<std::string>& getGlobalNames() const { return globalNames; }

    // Copy global state into this VM. Used to give a temporary VM
    // (e.g. constructor execution) access to the same globals.
    void adoptGlobals(const std::vector<std::string>& names,
                      const std::vector<Value>& values,
                      const std::vector<bool>& defined,
                      const std::unordered_map<std::string, int>& index);

    // Copy globals from another VM (current snapshot, not stale).
    void copyGlobalsFrom(const VM& source);

    // Merge global mutations back to another VM. New definitions and
    // value updates in this VM are propagated to the target.
    void mergeGlobalsTo(VM& target) const;

    // Signal the VM to interrupt execution at the next safe point.
    // Thread-safe: may be called from a signal handler.
    static void requestInterrupt() { interruptFlag = 1; }

    // Run a constructor chunk with a given instance and arguments.
    // Used by ClassConstructor to execute parent and own constructors
    // in a temporary VM. Resets internal state and runs to completion.
    InterpretResult runConstructor(const FunctionPrototype& proto,
                                   const Value& instance,
                                   const std::vector<Value>& args);

    // Error reporter — set by the host to redirect runtime errors.
    // When nullptr (default), runtime errors are printed to stderr directly.
    // Set to a custom ErrorReporter for LSP or other programmatic error handling.
    ErrorReporter* errorReporter = nullptr;

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
    InterpretResult callVoraFunction(const GcPtr<VoraFunction>& func,
                                      const std::vector<Value>& args);

    // Stack trace: walks the CallFrame chain to produce a traceback string.
    std::string captureStackTrace() const;

    // Run a mark-sweep garbage collection cycle.  Gathers roots from the VM
    // stack, globals, open upvalues, and call frames, then invokes the GC.
    void collectGarbage();

    // Pre-unwind snapshot: throwException() captures the stack trace before
    // popping frames. runtimeError() uses this if non-empty (exception path)
    // and captures fresh otherwise (direct error path).
    std::string lastErrorStackTrace;

public:
    // =========================================================================
    // Module system
    // =========================================================================

    // Module cache: resolved absolute path → module Dict object.
    // Each module is loaded and executed at most once.
    std::unordered_map<std::string, Value> moduleCache;

    // Stack of module paths currently being loaded (for circular import detection).
    // Shared between parent and child VMs during nested imports.
    std::vector<std::string> importStack;

    // Error message set by loadModule() on failure. The OP_IMPORT handler
    // reads this to print a single clean error instead of a cascade of
    // repeated messages at each nesting level.
    std::string loadModuleError;

    // Directory for std/ library modules (set by main / REPL).
    std::string stdDir;

    // Directory of the currently-executing file (for relative imports).
    std::string currentModuleDir;

    // Resolve a raw import path to an absolute file path.
    // Returns empty string if the module cannot be found.
    std::string resolveModulePath(const std::string& rawPath) const;

    // Load and execute a module file, returning a Dict of its exports.
    // Returns nullptr (Value) on error.
    Value loadModule(const std::string& resolvedPath);
};

} // namespace vora
