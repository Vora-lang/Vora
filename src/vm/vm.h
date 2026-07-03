/**
 * @file vm.h
 * @brief Stack-based bytecode virtual machine for the Vora language.
 *
 * @details
 * The VM executes compiled bytecode (Chunk) on a fixed-size value stack with
 * call frame management, exception handling (try/catch/finally), closure
 * upvalue capture, generator suspension, global variable interning, and
 * a debugger interface (DAP).
 *
 * Architecture overview:
 * - The VM is a register-less, pure stack machine. All computation happens
 *   by pushing and popping `Value` objects on the value stack.
 * - Global variables are stored in parallel arrays (names, values, defined
 *   flags) indexed by integer slot, giving O(1) read/write without string
 *   hashing at runtime. An auxiliary `globalIndex` map is used only at
 *   define-time (compiler/REPL).
 * - Call frames hold a return instruction pointer, a stack base index, a
 *   reference to the executing VoraFunction (for upvalue access), parameter
 *   masks for default-parameter evaluation, and a LIFO defer-closure stack.
 * - Exception handling uses a catch-handler stack. `OP_PUSH_CATCH` registers
 *   a handler (target IP, frame bookmarks); `throwException()` finds the
 *   innermost handler, sets `exceptionInFlight`, and jumps to the target.
 *   Finally blocks are replayed bytecode; `OP_FINALLY_END` checks
 *   `exceptionInFlight` to decide whether to re-throw.
 * - Closures capture open upvalues via `std::shared_ptr<Upvalue>` stored in
 *   an `openUpvalues` map keyed by stack slot index. When a local goes out
 *   of scope, the upvalue is closed (value copied out, stack pointer cleared).
 * - Generators suspend/resume by saving/restoring the IP, chunk pointer, and
 *   stack state through `pendingResume`/`pendingIp`/`pendingChunk`.
 * - The module system caches loaded modules by resolved absolute path,
 *   detects circular imports via an import stack, and supports relative
 *   imports from the current module's directory.
 * - A debugger interface (DAP) exposes breakpoints, stepping (in/over/out),
 *   stack inspection, local variable access, and expression evaluation.
 */

#pragma once

#include <csignal>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "chunk.h"
#include "../common/error_reporter.h"
#include "../runtime/value.h"

namespace vora {

class VoraFunction;  // forward declaration

/// @brief Result of a VM interpretation run.
enum class InterpretResult {
    OK,              ///< Execution completed successfully.
    COMPILE_ERROR,   ///< Compilation failed before execution.
    RUNTIME_ERROR,   ///< A runtime error occurred during execution.
    DEBUG_STOPPED,   ///< Debugger breakpoint or step triggered.
};

/// @brief Debugger step mode for single-stepping.
enum class StepMode {
    None,     ///< Not stepping — run freely.
    StepIn,   ///< Step into function calls.
    StepOver, ///< Step over function calls.
    StepOut,  ///< Step out of the current function.
};

/// @brief A call frame representing one function invocation.
///
/// @details
/// Each call frame records where to resume in the caller's bytecode after
/// the callee returns (`returnIp`), the base of this frame's locals on the
/// value stack (`frameBase`), and the chunk/function being executed.
///
/// The `paramProvidedMask` is a bitmask where bit i indicates that argument i
/// was actually passed by the caller. This is consumed by `OP_DEFAULT_PARAM`
/// to decide whether to evaluate a default-value expression for that slot.
///
/// The `deferStack` holds defer-closure values in LIFO order (the most
/// recently deferred closure is at the back). Defer closures are flushed
/// on normal function exit (`OP_DEFER_FLUSH`) and during exception unwind
/// (`throwException()`), in both cases last-in-first-out.
struct CallFrame {
    const uint8_t* returnIp;            ///< Where to resume in caller after return.
    size_t frameBase;                    ///< Base index of this frame's locals on the VM stack.
    const Chunk* callerChunk;            ///< Chunk to restore on return.
    GcPtr<VoraFunction> function;        ///< Executing function (for upvalue access).
    GcPtr<Generator> generator;           ///< Generator if this frame is an async function (null otherwise).
    uint64_t paramProvidedMask = 0;      ///< Bit i=1 if param slot i was provided by the caller.
    std::vector<Value> deferStack;       ///< Defer closures (LIFO: back = most recently deferred).
};

/// @brief Stack-based bytecode virtual machine.
///
/// @details
/// Executes compiled Vora bytecode on a value stack with:
/// - Global variable interning (O(1) slot-based access)
/// - Call frame stack with closure upvalue capture
/// - Exception handling (try/catch/finally with non-local exit routing)
/// - Generator suspension/resumption
/// - Module caching and circular import detection
/// - Debugger interface (DAP breakpoints, stepping, variable inspection)
/// - Defer execution (LIFO at function exit / exception unwind)
///
/// The VM owns all runtime state. Most fields are private; the public fields
/// are intentionally exposed for native function callbacks and the debugger
/// to inspect/modify execution state without adding getter/setter overhead
/// on the hot path.
class VM {
    static constexpr int MAX_LOCALS_PER_FRAME = 256;   ///< Maximum local variables per frame.
    static constexpr int MAX_CALL_FRAMES = 10000;      ///< Maximum call depth before overflow.

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
    /// @brief Mark a global slot as modified (for incremental merge).
    void markGlobalDirty(int slot);
    /// @brief Clear all dirty-global tracking state.
    void clearDirtyGlobals();

    /// @brief An entry on the catch-handler stack.
    ///
    /// @details
    /// When `OP_PUSH_CATCH` executes, a `CatchHandler` is pushed onto
    /// `catchHandlers`. The handler records the target instruction pointer
    /// (where to jump on exception), the frame base and frame count at
    /// registration time (for unwinding to the correct frame), the stack
    /// slot where the exception value should be placed, and which chunk
    /// the handler belongs to (for validity checks during unwinding).
    struct CatchHandler {
        const uint8_t* targetIp;      ///< IP of the catch-block entry (OP_POP_CATCH at try exit).
        size_t targetFrameBase;        ///< Stack base index when handler was registered (frame's own base).
        size_t targetSlot;             ///< Stack index where to push the exception value.
        const Chunk* chunk;            ///< Chunk the handler lives in (for cross-chunk validity).
        size_t frameCount;             ///< frames.size() when handler was registered.
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
    /// @brief Capture (or reuse) an open upvalue at a given stack slot.
    /// @param slotIndex The stack index of the local variable to capture.
    /// @return A shared pointer to the Upvalue (new or existing).
    std::shared_ptr<Upvalue> captureUpvalue(size_t slotIndex);

    // Interrupt flag — set by signal handler (SIGINT / Ctrl+C).
    // Checked at the top of each opcode dispatch in run().
    static volatile sig_atomic_t interruptFlag;

    // Stack error flag — set by push() when the stack overflows.
    // Checked at dispatch to bail out gracefully instead of continuing
    // with corrupted stack state.
    bool stackErrorFlag = false;

    // Defer execution depth limit — when >=0, run() returns OK when
    // frames.size() reaches this value. Used by flushDeferStack() to
    // execute a single defer closure to completion within throwException().
    int deferExecutionLimit = -1;

    // ── Debugger state ─────────────────────────────────────────────────
    std::unordered_set<size_t> breakpoints_;     ///< Set of bytecode offsets with active breakpoints.
    StepMode stepMode_ = StepMode::None;         ///< Current stepping mode.
    int stepOutDepth_ = 0;                       ///< Frame count to fall below for StepOut to complete.
    int stepOverDepth_ = 0;                      ///< Frame depth to match for StepOver to complete.
    bool debugPaused_ = false;                   ///< True when execution is paused by debugger.

public:
    VM();
    ~VM() = default;

    // --- Runtime execution state (public for native function access) ---
    const Chunk* currentChunk = nullptr;  ///< Currently executing chunk.
    const uint8_t* ip = nullptr;          ///< Instruction pointer into currentChunk's bytecode.
    size_t frameBaseIndex = 0;            ///< Current frame's stack base index.
    std::vector<CallFrame> frames;        ///< Call frame stack (top = back = current frame).

    // --- Generator support ---
    bool pendingResume = false;               ///< True when a generator has state to resume from.
    const uint8_t* pendingIp = nullptr;       ///< Saved IP for generator resume.
    const Chunk* pendingChunk = nullptr;      ///< Saved chunk for generator resume.
    GcPtr<Generator> pendingGenerator;        ///< Generator whose next() triggered the pending state.
    GcPtr<Generator> currentGenerator;        ///< Non-null while executing a generator body.

    /// @brief Interpret (compile + execute) a bytecode chunk.
    /// @param chunk The compiled bytecode chunk to execute.
    /// @return InterpretResult indicating success, compile error, runtime error, or debug stop.
    InterpretResult interpret(const Chunk& chunk);

    /// @brief Register a native (C++) function as a global variable.
    /// @param name The global variable name to bind the function to.
    /// @param arity The expected argument count (-1 for variadic, 0 for zero-arg, etc.).
    /// @param fn The C++ callback: receives a vector of argument values, returns a Value.
    void defineNative(const std::string& name, int arity,
                      std::function<Value(const std::vector<Value>&)> fn);

    // --- Embedding API: direct global variable access ---

    /// @brief Read a global variable.
    /// @param name The global variable name.
    /// @return The variable's Value, or nullptr (Value null) if not defined.
    Value getGlobal(const std::string& name) const;

    /// @brief Write a global variable. Creates the slot if it does not exist.
    /// @param name The global variable name.
    /// @param value The value to assign.
    void setGlobal(const std::string& name, const Value& value);

    /// @brief Check whether a global variable exists and is defined.
    /// @param name The global variable name.
    /// @return True if the variable has been defined (not just reserved).
    bool hasGlobal(const std::string& name) const;

    /// @brief Convenience alias for defineNative(name, -1, fn) — variadic by default.
    /// @param name The global variable name to bind the function to.
    /// @param fn The C++ callback.
    void registerNativeFunction(const std::string& name,
                                std::function<Value(const std::vector<Value>&)> fn);

    /// @brief Initialize global slots from the compiler's interning table.
    /// @details Called once before execution to reserve global variable slots
    /// identified during compilation. Each name gets a slot index; undefined
    /// variables get a slot but remain in "undefined" state until setGlobal.
    /// @param names The ordered list of global variable names from the compiler.
    void initGlobals(const std::vector<std::string>& names);

    /// @brief Read the current global name table.
    /// @details Used for seeding the compiler in REPL mode so that globals
    /// defined in earlier REPL lines are visible to subsequent lines.
    /// @return A const reference to the vector of global variable names.
    const std::vector<std::string>& getGlobalNames() const { return globalNames; }

    /// @brief Copy global state into this VM.
    /// @details Used to give a temporary VM (e.g. constructor execution)
    /// access to the same globals as the parent VM. This is a wholesale
    /// replacement — existing globals in this VM are discarded.
    /// @param names Global variable names (ordered).
    /// @param values Global variable values (parallel to names).
    /// @param defined Defined flags (parallel to names).
    /// @param index Name-to-slot mapping.
    void adoptGlobals(const std::vector<std::string>& names,
                      const std::vector<Value>& values,
                      const std::vector<bool>& defined,
                      const std::unordered_map<std::string, int>& index);

    /// @brief Copy globals from another VM (current snapshot, not stale).
    /// @param source The source VM to copy globals from.
    void copyGlobalsFrom(const VM& source);

    /// @brief Merge global mutations back to another VM.
    /// @details New definitions and value updates made in this VM are
    /// propagated to the target VM. Only dirty (modified) slots are
    /// transferred, avoiding O(n) iteration over all globals.
    /// @param target The target VM to merge globals into.
    void mergeGlobalsTo(VM& target) const;

    /// @brief Signal the VM to interrupt execution at the next safe point.
    /// @details Thread-safe: may be called from a signal handler.
    /// The VM checks `interruptFlag` at the top of each opcode dispatch
    /// in run() and handles it gracefully.
    static void requestInterrupt() { interruptFlag = 1; }

    // ── Debugger API (DAP) ───────────────────────────────────────────────

    /// @name Breakpoint management
    /// @{

    /// @brief Set a breakpoint at the given bytecode offset.
    /// @param bytecodeOffset The bytecode offset within the current chunk.
    void debugSetBreakpoint(size_t bytecodeOffset);
    /// @brief Remove a breakpoint at the given bytecode offset.
    /// @param bytecodeOffset The bytecode offset within the current chunk.
    void debugRemoveBreakpoint(size_t bytecodeOffset);
    /// @brief Clear all breakpoints.
    void debugClearBreakpoints();
    /// @brief Check whether a breakpoint exists at the given offset.
    /// @param bytecodeOffset The bytecode offset to query.
    /// @return True if a breakpoint is active at that offset.
    bool debugHasBreakpoint(size_t bytecodeOffset) const;

    /// @}

    /// @name Step control
    /// @{

    /// @brief Set the debugger step mode.
    /// @param mode The step mode: None, StepIn, StepOver, or StepOut.
    void debugSetStepMode(StepMode mode);
    /// @brief Get the current step mode.
    /// @return The current StepMode value.
    StepMode debugStepMode() const { return stepMode_; }
    /// @brief Check whether execution is currently paused.
    /// @return True if the VM is paused by the debugger.
    bool debugIsPaused() const { return debugPaused_; }
    /// @brief Resume execution from a paused state.
    void debugResume();
    /// @brief Pause execution (set the debug-paused flag).
    void debugPause();

    /// @}

    /// @name State inspection (for DAP stackTrace / scopes / variables)
    /// @{

    /// @brief Get the number of active call frames.
    /// @return The current call stack depth.
    int debugFrameCount() const;
    /// @brief Get the name of the function in a given frame.
    /// @param frameIndex 0-based frame index (0 = current/top frame).
    /// @return The function name string (may be empty for top-level code).
    std::string debugFrameName(int frameIndex) const;
    /// @brief Get the source line number for a given frame.
    /// @param frameIndex 0-based frame index.
    /// @return The 1-based source line number.
    int debugFrameLine(int frameIndex) const;
    /// @brief Get the bytecode offset of the instruction pointer in a given frame.
    /// @param frameIndex 0-based frame index.
    /// @return The bytecode offset within that frame's chunk.
    size_t debugFrameBytecodeOffset(int frameIndex) const;

    /// @brief Get local variable names for a given frame.
    /// @details Retrieved from the FunctionPrototype stored in the frame's
    /// VoraFunction constant pool entry.
    /// @param frameIndex 0-based frame index.
    /// @return A vector of local variable name strings (empty if none).
    std::vector<std::string> debugLocalNames(int frameIndex) const;
    /// @brief Read the value of a local variable in a given frame.
    /// @param frameIndex 0-based frame index.
    /// @param slot The local variable slot index within that frame.
    /// @return The Value stored in that local slot.
    Value debugLocalValue(int frameIndex, int slot) const;

    /// @brief Get the current instruction offset (in the active chunk).
    /// @return The bytecode offset of the current instruction pointer.
    size_t debugCurrentOffset() const;

    /// @}

    /// @brief Evaluate an expression string in the current debug frame context.
    /// @details Compiles and executes the expression within the scope of the
    /// current call frame, allowing the debugger to evaluate watch expressions
    /// and REPL-style queries while paused.
    /// @param expr The source string to evaluate (a Vora expression).
    /// @param[out] result Receives the result value on success.
    /// @return InterpretResult indicating success or the type of error.
    InterpretResult debugEvaluate(const std::string& expr, Value& result);

    /// @brief Run a constructor chunk with a given instance and arguments.
    /// @details Used by ClassConstructor to execute parent and own constructors
    /// in a temporary VM that shares globals with the calling VM. Resets
    /// internal stack state and runs to completion.
    /// @param proto The FunctionPrototype containing the constructor bytecode.
    /// @param instance The newly-created object instance (bound as `self`).
    /// @param args Constructor arguments.
    /// @return InterpretResult indicating success or error.
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
    bool nativeError = false;       ///< True when a native function has set an error.
    Value nativeErrorValue;         ///< The exception value set by a native function.

    // Value operations are in value_ops.h (free functions).

    /// @brief Throw (or re-throw) an exception through the VM's catch-handler chain.
    /// @details Searches the `catchHandlers` stack from innermost to outermost.
    /// For each handler, if the handler is in the current chunk, unwinds frames
    /// to the handler's frame count, flushes defer closures encountered during
    /// unwind, sets the instruction pointer to the handler's target, pushes the
    /// exception value, and sets `exceptionInFlight = true`.
    /// @param value The exception value to throw.
    /// @return True if a catch handler was found and the exception was caught;
    ///         false if uncaught (the exception propagates to the host).
    bool throwException(const Value& value);

private:
    /// @brief Main VM execution loop.
    /// @details Dispatches opcodes from `currentChunk` bytecode in a loop,
    /// checking interrupt and stack-error flags at the top of each iteration.
    /// @return InterpretResult indicating the outcome of the run.
    InterpretResult run();

    /// @brief Reset the value stack and frame-related state for a fresh execution.
    void resetStack();

    /// @brief Push a value onto the stack.
    /// @param value The value to push.
    void push(Value value);
    /// @brief Pop and return the top value from the stack.
    /// @return The value that was at the top of the stack.
    Value pop();
    /// @brief Peek at a value at a given distance from the stack top.
    /// @param distance 0 = top, 1 = one below top, etc.
    /// @return A mutable reference to the value.
    Value& peek(int distance = 0);
    /// @brief Peek at a value at a given distance from the stack top (const overload).
    /// @param distance 0 = top, 1 = one below top, etc.
    /// @return A const reference to the value.
    const Value& peek(int distance = 0) const;

    /// @brief Report a runtime error.
    /// @details Prints the error message along with the current source location
    /// and call stack trace. Resets the VM stack afterwards.
    /// @param message The error message.
    void runtimeError(const std::string& message);
    /// @brief Report a runtime error with explicit source location.
    /// @param message The error message.
    /// @param line The source line number.
    /// @param column The source column number.
    void runtimeError(const std::string& message, int line, int column);
    /// @brief Report a runtime error and attempt to throw it as an exception.
    /// @details If a catch handler is active, the error is routed through
    /// throwException() so the user's try/catch can handle it. Otherwise it
    /// falls through to runtimeError() which prints and resets.
    /// @param message The error message.
    /// @return True if the error was caught by a catch handler.
    bool runtimeErrorOrThrow(const std::string& message);

    /// @brief Read a 16-bit unsigned integer from the bytecode stream (little-endian).
    /// @details Advances the instruction pointer by 2 bytes.
    /// @return The decoded 16-bit value.
    uint16_t readShort();
    /// @brief Read a single byte from the bytecode stream.
    /// @details Advances the instruction pointer by 1 byte.
    /// @return The byte value.
    uint8_t readByte();

    // Call frame management
    /// @brief Call a value as a function.
    /// @details Dispatches via dynamic_cast to BoundMethod, ClassConstructor,
    /// NativeFunction, or VoraFunction. Handles argument count checking,
    /// frame setup, and error reporting.
    /// @param callee The callable value to invoke.
    /// @param argCount Number of arguments passed.
    /// @return True if the call was successful, false on error.
    bool callValue(const Value& callee, uint8_t argCount);
    /// @brief Call a compiled Vora function with explicit arguments.
    /// @param func The VoraFunction to call.
    /// @param args The argument values.
    /// @return InterpretResult indicating success or error.
    InterpretResult callVoraFunction(const GcPtr<VoraFunction>& func,
                                      const std::vector<Value>& args);
    /// @brief Execute a keyword-argument call (f(x=1, y=2)).
    /// @details Matches keyword argument names to parameter names and
    /// reorders arguments to match the function's parameter order.
    /// @param posCount Number of positional arguments.
    /// @param kwCount Number of keyword arguments.
    /// @param kwNameIndices Indices into the constant pool for keyword names.
    /// @return InterpretResult indicating success or error.
    InterpretResult executeCallKw(uint8_t posCount, uint8_t kwCount,
                                  const std::vector<size_t>& kwNameIndices);

    /// @brief Capture a stack trace string from the current call frame chain.
    /// @details Walks all call frames (current through root), extracting
    /// function names and source locations from each frame's VoraFunction
    /// and chunk line tables.
    /// @return A multi-line stack trace string.
    std::string captureStackTrace() const;

    /// @brief Run a mark-sweep garbage collection cycle.
    /// @details Gathers roots from the VM stack, globals, open upvalues,
    /// and call frames, then invokes the GC to mark reachable objects
    /// and sweep unreachable ones.
    void collectGarbage();

    /// @brief Execute all defer closures on the current frame's defer stack (LIFO).
    /// @details Pops and calls defer closures one by one, last-in-first-out.
    /// Shared by OP_DEFER_FLUSH (normal function exit) and throwException()
    /// (exception unwind path). Uses `deferExecutionLimit` to run one defer
    /// to completion at a time during unwind, so nested exceptions are handled
    /// correctly.
    void flushDeferStack();

    // Pre-unwind snapshot: throwException() captures the stack trace before
    // popping frames. runtimeError() uses this if non-empty (exception path)
    // and captures fresh otherwise (direct error path).
    std::string lastErrorStackTrace;

public:
    // =========================================================================
    // Module system
    // =========================================================================

    /// @brief Module cache: resolved absolute path → module Dict object.
    /// @details Each module is loaded and executed at most once per VM.
    /// Subsequent imports of the same resolved path return the cached Dict.
    std::unordered_map<std::string, Value> moduleCache;

    /// @brief Stack of module paths currently being loaded.
    /// @details Used for circular import detection. If a resolved path
    /// already appears in this stack, a circular dependency error is raised.
    /// Shared between parent and child VMs during nested imports so the
    /// child VM can see the parent's import context.
    std::vector<std::string> importStack;

    /// @brief Error message set by loadModule() on failure.
    /// @details The OP_IMPORT handler reads this to print a single clean
    /// error instead of a cascade of repeated messages at each nesting level
    /// when a deeply-nested import fails.
    std::string loadModuleError;

    /// @brief Directory for std/ library modules.
    /// @details Set by main() or the REPL host. Used as fallback search
    /// path when resolving `import "std/..."` paths.
    std::string stdDir;

    /// @brief Directory of the currently-executing file.
    /// @details Set before executing a file so that relative imports
    /// (e.g. `import "./utils"`) resolve relative to the importing file.
    std::string currentModuleDir;

    /// @brief Resolve a raw import path to an absolute file path.
    /// @details Searches in order: (1) relative to currentModuleDir,
    /// (2) relative to stdDir for "std/..." prefixed paths,
    /// (3) relative to the current working directory.
    /// @param rawPath The raw import path as written in source (e.g. "./math", "std/io").
    /// @return The resolved absolute file path with `.va` extension appended,
    ///         or an empty string if the module cannot be found.
    std::string resolveModulePath(const std::string& rawPath) const;

    /// @brief Load and execute a module file, returning a Dict of its exports.
    /// @details Compiles and runs the module in a child VM that shares
    /// globals with this VM. The module's top-level scope populates an
    /// exports Dict which is cached in `moduleCache` for subsequent imports.
    /// @param resolvedPath The absolute path to the module file (from resolveModulePath()).
    /// @return The module's export Dict as a Value, or nullptr (Value) on error.
    Value loadModule(const std::string& resolvedPath);
};

} // namespace vora
