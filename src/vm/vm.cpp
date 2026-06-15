#include "vm.h"
#include "opcode.h"
#include "value_ops.h"

#include "../gc/gc_heap.h"
#include "../runtime/bound_method.h"
#include "../runtime/builtins.h"
#include "../runtime/class_constructor.h"
#include "../runtime/native_function.h"
#include "../runtime/vora_function.h"
#include "../vm/compiler.h"  // for FunctionPrototype, ClassDefinition

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace vora {

// =========================================================================
// C3 Linearization (Python-style MRO)
// =========================================================================
// Computes Method Resolution Order for a class given its parent classes.
// L[C(B1, B2, ..., Bn)] = [C] + merge(L[B1], L[B2], ..., L[Bn], [B1, B2, ..., Bn])
// Returns: vector<GcPtr<ClassDefinition>> where [0] = self, then parents in MRO.
// Throws std::runtime_error on inconsistent MRO (diamond problem without resolution).

static std::vector<GcPtr<ClassDefinition>> computeC3MRO(
    const GcPtr<ClassDefinition>& self,
    const std::vector<GcPtr<ClassDefinition>>& parents)
{
    // Build list of linearizations for the merge: each parent's MRO + direct parent list
    std::vector<std::vector<GcPtr<ClassDefinition>>> lists;

    for (const auto& p : parents) {
        // Add the parent's own MRO (which is [parent, parent's parents...])
        std::vector<GcPtr<ClassDefinition>> parentMRO;
        for (const auto& sp : p->mro) {
            if (sp) {
                parentMRO.push_back(sp);
            }
        }
        lists.push_back(std::move(parentMRO));
    }

    // Add the direct parent list [B1, B2, ..., Bn] as the last list
    lists.push_back(parents);

    // Merge
    std::vector<GcPtr<ClassDefinition>> result;
    result.push_back(self);

    // Continue while any list is non-empty
    bool anyNonEmpty = true;
    while (anyNonEmpty) {
        anyNonEmpty = false;
        for (auto& lst : lists) {
            if (!lst.empty()) {
                anyNonEmpty = true;
                break;
            }
        }
        if (!anyNonEmpty) break;

        // Find a good head: the first head that doesn't appear in any tail
        GcPtr<ClassDefinition> goodHead;
        for (const auto& lst : lists) {
            if (lst.empty()) continue;
            const auto& candidate = lst[0];

            // Check if candidate appears in the tail of ANY list
            bool inTail = false;
            for (const auto& other : lists) {
                for (size_t j = 1; j < other.size(); j++) {
                    if (other[j] == candidate) {
                        inTail = true;
                        break;
                    }
                }
                if (inTail) break;
            }

            if (!inTail) {
                goodHead = candidate;
                break;
            }
        }

        if (!goodHead) {
            // Inconsistent MRO — should not happen in practice for simple hierarchies
            // Fall back to the original parent order (keep first inherited)
            break;
        }

        result.push_back(goodHead);

        // Remove goodHead from the head of all lists
        for (auto& lst : lists) {
            if (!lst.empty() && lst[0] == goodHead) {
                lst.erase(lst.begin());
            }
        }
    }

    return result;
}

// =========================================================================
// Construction
// =========================================================================

VM::VM() {
    stack.reserve(1024);
    resetStack();
    frameBaseIndex = 0;
}

void VM::resetStack() {
    stackTopIndex = 0;
}

void VM::push(Value value) {
    if (stackTopIndex < stack.size())
        stack[stackTopIndex] = std::move(value);
    else
        stack.push_back(std::move(value));
    stackTopIndex++;
}

Value VM::pop() {
    return std::move(stack[--stackTopIndex]);
}

Value& VM::peek(int distance) {
    return stack[stackTopIndex - 1 - distance];
}

const Value& VM::peek(int distance) const {
    return stack[stackTopIndex - 1 - distance];
}

uint16_t VM::readShort() {
    uint16_t result = static_cast<uint16_t>(*ip) | (static_cast<uint16_t>(*(ip + 1)) << 8);
    ip += 2;
    return result;
}

uint8_t VM::readByte() {
    return *ip++;
}

// =========================================================================
// RLE line/column decode helper (used by runtimeError and captureStackTrace)
// =========================================================================

namespace {

void decodeLineFromChunk(const Chunk& chunk, size_t offset, int& line, int& column) {
    line = 0;
    column = 0;
    size_t pos = 0;
    const auto& rleLines = chunk.lines;
    for (size_t i = 0; i < rleLines.size(); i += 2) {
        int runLen = rleLines[i];
        if (offset < pos + static_cast<size_t>(runLen)) {
            line = rleLines[i + 1];
            break;
        }
        pos += runLen;
    }
    pos = 0;
    const auto& rleCols = chunk.columns;
    for (size_t i = 0; i < rleCols.size(); i += 2) {
        int runLen = rleCols[i];
        if (offset < pos + static_cast<size_t>(runLen)) {
            column = rleCols[i + 1];
            break;
        }
        pos += runLen;
    }
}

} // anonymous namespace

void VM::runtimeError(const std::string& message) {
    size_t offset = ip - currentChunk->code.data();
    int line = 0, column = 0;
    decodeLineFromChunk(*currentChunk, offset, line, column);

    // Print source snippet with caret
    printSourceLine(std::cerr, currentChunk->source, line, column, 1,
                    message, "RuntimeError");

    if (!lastErrorStackTrace.empty()) {
        std::cerr << lastErrorStackTrace;
        lastErrorStackTrace.clear();
    } else {
        std::cerr << captureStackTrace();
    }
}

void VM::runtimeError(const std::string& message, int line, int column) {
    // Print source snippet with caret (uses currentChunk for source if available)
    if (currentChunk) {
        printSourceLine(std::cerr, currentChunk->source, line, column, 1,
                        message, "RuntimeError");
    } else {
        std::cerr << "VM RuntimeError [" << line << ":" << column << "]: "
                  << message << "\n";
    }

    if (!lastErrorStackTrace.empty()) {
        std::cerr << lastErrorStackTrace;
        lastErrorStackTrace.clear();
    } else {
        std::cerr << captureStackTrace();
    }
}

std::string VM::captureStackTrace() const {
    // Guard: no chunk loaded yet (shouldn't happen, but be safe)
    if (!currentChunk || currentChunk->code.empty()) return "";

    std::string result = "Stack trace (most recent call last):\n";

    // Walk frames from OLDEST to NEWEST — each frame's returnIp points
    // to the OP_CALL site in the caller's chunk (2 bytes: opcode + argCount).
    for (size_t i = 0; i < frames.size(); i++) {
        const auto& frame = frames[i];
        if (!frame.callerChunk || frame.callerChunk->code.empty()) continue;

        // returnIp points AFTER the OP_CALL instruction (2 bytes)
        const uint8_t* codeStart = frame.callerChunk->code.data();
        ptrdiff_t returnOffset = frame.returnIp - codeStart;
        size_t callOffset = (returnOffset >= 2)
            ? static_cast<size_t>(returnOffset - 2)
            : 0;

        int line = 0, column = 0;
        decodeLineFromChunk(*frame.callerChunk, callOffset, line, column);

        std::string callerName;
        if (i == 0) {
            callerName = "<script>";
        } else {
            const auto& callerFrame = frames[i - 1];
            if (callerFrame.function) {
                callerName = callerFrame.function->name();
            } else {
                callerName = "<native>";
            }
        }

        result += "  [" + std::to_string(line) + ":" + std::to_string(column)
               + "] in " + callerName + "\n";
    }

    // Error site: current instruction in the currently executing function
    size_t errorOffset = static_cast<size_t>(ip - currentChunk->code.data());
    int line = 0, column = 0;
    decodeLineFromChunk(*currentChunk, errorOffset, line, column);

    std::string funcName;
    if (!frames.empty() && frames.back().function) {
        funcName = frames.back().function->name();
    } else {
        funcName = "<script>";
    }
    result += "  [" + std::to_string(line) + ":" + std::to_string(column)
           + "] in " + funcName + "\n";

    return result;
}

bool VM::runtimeErrorOrThrow(const std::string& message) {
    if (throwException(GcHeap::instance().alloc<GcString>(message))) return true;  // caught by catch handler
    runtimeError(message);
    return false;  // not caught
}

std::shared_ptr<Value> VM::captureUpvalue(size_t slotIndex) {
    // Check if this slot already has an open upvalue
    auto it = openUpvalues.find(slotIndex);
    if (it != openUpvalues.end()) {
        return it->second;
    }
    // Create a new heap-allocated value copying the current stack value
    auto uv = std::make_shared<Value>(stack[slotIndex]);
    openUpvalues[slotIndex] = uv;
    return uv;
}

bool VM::throwException(const Value& value) {
    // Capture stack trace BEFORE unwinding — once frames are popped, the
    // call chain is lost. If this exception is uncaught, runtimeError()
    // will print the captured trace.
    lastErrorStackTrace = captureStackTrace();

    // Walk up through catch handlers. We peek the handler (don't pop it)
    // so that OP_POP_CATCH at the target can pop it. If we popped here,
    // nested try blocks would fail because OP_POP_CATCH at the inner
    // handler target would accidentally pop the outer handler.
    while (!catchHandlers.empty()) {
        auto handler = catchHandlers.back();  // peek only

        // Unwind stack to exception slot and push the thrown value.
        // The exception slot is after all locals that existed at handler
        // registration time. This ensures function parameters and other
        // locals below the slot are preserved across the catch.
        stackTopIndex = handler.targetSlot;
        push(value);

        // Jump to catch handler
        ip = handler.targetIp;
        currentChunk = handler.chunk;

        // Unwind call frames to the level where the handler was registered.
        while (frames.size() > handler.frameCount) {
            // Clear open upvalues for the frame being unwound
            size_t frameEnd = frames.back().frameBase + MAX_LOCALS_PER_FRAME;
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
        // Restore the CAUGHT frame's own base (from the handler), NOT the value
        // stored in CallFrame::frameBase (which holds the PARENT frame's base).
        frameBaseIndex = handler.targetFrameBase;

        exceptionInFlight = true;
        lastErrorStackTrace.clear();  // caught — trace no longer needed
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
        GcHeap::instance().alloc<NativeFunction>(name, arity, std::move(fn));
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
            if (std::holds_alternative<GcPtr<Array>>(arg)) {
                return static_cast<double>(
                    std::get<GcPtr<Array>>(arg)->elements.size());
            }
            if (std::holds_alternative<GcPtr<GcString>>(arg)) {
                return static_cast<double>(std::get<GcPtr<GcString>>(arg)->value.size());
            }
            if (std::holds_alternative<GcPtr<ObjectInstance>>(arg)) {
                return static_cast<double>(
                    std::get<GcPtr<ObjectInstance>>(arg)->properties.size());
            }
            if (std::holds_alternative<GcPtr<Dict>>(arg)) {
                return static_cast<double>(
                    std::get<GcPtr<Dict>>(arg)->pairs.size());
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
// Constructor execution (used by ClassConstructor)
// =========================================================================

InterpretResult VM::runConstructor(const Chunk& chunk,
                                    const Value& instance,
                                    const std::vector<Value>& args) {
    resetStack();
    frameBaseIndex = 0;
    frames.clear();
    catchHandlers.clear();
    exceptionInFlight = false;
    currentChunk = &chunk;
    ip = chunk.code.data();
    push(instance);  // 'this' at slot 0
    for (const auto& a : args) {
        push(a);
    }
    return run();
}

// =========================================================================
// Value operations
// =========================================================================
// Extracted to value_ops.cpp — see value_ops.h for isTruthy(), valuesEqual(),
// valuesCompare(), and addValues().

// =========================================================================
// Call frame management
// =========================================================================

bool VM::callValue(const Value& callee, uint8_t argCount) {
    if (std::holds_alternative<GcPtr<Callable>>(callee)) {
        auto callable = std::get<GcPtr<Callable>>(callee);

        // Bound method: dispatch through the current VM so globals
        // and builtins remain accessible (no temp VM).
        if (auto bound = dynamic_cast<BoundMethod*>(callable.get())) {
            auto instance = bound->getInstance();
            const auto* proto = bound->getMethodProto();
            if (!proto) {
                runtimeErrorOrThrow("Bound method has no prototype");
                return false;
            }

            // Pop args (in reverse), then pop the callee
            std::vector<Value> arguments(argCount);
            for (int i = argCount - 1; i >= 0; i--) {
                arguments[static_cast<size_t>(i)] = pop();
            }
            pop(); // callee (bound method)

            // Save frame base BEFORE pushing — it must point at 'this'
            size_t newFrameBaseIndex = stackTopIndex;

            // Push 'this' (bound instance) then method arguments as locals
            push(instance);
            for (const auto& a : arguments) {
                push(a);
            }

            // Set up call frame directly (inline callVoraFunction logic).
            frames.push_back({ip, frameBaseIndex, currentChunk, bound->getMethodFunc()});
            currentChunk = &proto->chunk;
            ip = currentChunk->code.data();
            frameBaseIndex = newFrameBaseIndex;  // points to instance at slot 0
            return true;
        }

        // Class constructor: delegate to ClassConstructor::call() which
        // creates an instance and runs the constructor chain in a temp VM.
        if (auto ctor = dynamic_cast<ClassConstructor*>(callable.get())) {
            // Collect arguments from stack (in reverse)
            std::vector<Value> arguments(argCount);
            for (int i = argCount - 1; i >= 0; i--) {
                arguments[static_cast<size_t>(i)] = pop();
            }
            pop(); // pop the callable itself

            Value result = ctor->call(arguments);
            push(result);
            return true;
        }

        // Native function (pure callback)
        if (auto native = dynamic_cast<NativeFunction*>(callable.get())) {
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

            Value result = native->call(arguments);

            // Check for native-side error (e.g. assert failure).
            if (nativeError) {
                nativeError = false;
                Value errorVal = std::move(nativeErrorValue);
                nativeErrorValue = nullptr;
                if (throwException(errorVal)) {
                    return true;  // caught, IP redirected
                }
                // Not caught — report as unhandled runtime error.
                runtimeError("Uncaught: " + valueToString(errorVal));
                return false;
            }

            push(result);
            return true;
        }

        // Vora function
        if (auto voraFn = dynamic_cast<VoraFunction*>(callable.get())) {
            // Collect arguments from stack (in reverse)
            std::vector<Value> arguments(argCount);
            for (int i = argCount - 1; i >= 0; i--) {
                arguments[static_cast<size_t>(i)] = pop();
            }
            pop(); // pop the callable itself

            return callVoraFunction(GcPtr<VoraFunction>(voraFn), arguments) == InterpretResult::OK;
        }
    }

    runtimeErrorOrThrow("Can only call functions");
    return false;
}

InterpretResult VM::callVoraFunction(const GcPtr<VoraFunction>& func,
                                      const std::vector<Value>& args) {
    // Check arity — range check (must be between requiredArity and arity)
    if (args.size() < static_cast<size_t>(func->requiredArity()) ||
        args.size() > static_cast<size_t>(func->arity())) {
        std::string expected;
        if (func->requiredArity() == func->arity()) {
            expected = std::to_string(func->arity());
        } else {
            expected = std::to_string(func->requiredArity()) + ".." +
                       std::to_string(func->arity());
        }
        runtimeErrorOrThrow("Wrong arity: expected " + expected +
            " but got " + std::to_string(args.size()));
        return InterpretResult::RUNTIME_ERROR;
    }

    // Get the function's compiled prototype
    const FunctionPrototype* proto = func->getPrototype();
    if (!proto) {
        runtimeErrorOrThrow("Function has no compiled body");
        return InterpretResult::RUNTIME_ERROR;
    }

    // Push return address, caller chunk, and function onto call frame stack
    frames.push_back({ip, frameBaseIndex, currentChunk, func});

    // Switch to function's chunk
    currentChunk = &proto->chunk;
    ip = currentChunk->code.data();
    frameBaseIndex = stackTopIndex;  // locals start after the current stack top

    // Store actual arg count for OP_DEFAULT_PARAM
    currentArgCount = static_cast<uint8_t>(args.size());

    // Bind parameters as locals at frameBase.
    // Pad with null up to total arity (default params will overwrite).
    for (size_t i = 0; i < static_cast<size_t>(func->arity()); i++) {
        if (i < args.size()) {
            push(args[i]);
        } else {
            push(nullptr);
        }
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
    frameBaseIndex = 0;
    frames.clear();
    catchHandlers.clear();
    exceptionInFlight = false;
    nativeError = false;
    nativeErrorValue = nullptr;
    lastErrorStackTrace.clear();
    return run();
}

// =========================================================================
// Garbage collection — gather roots from VM state and run mark-sweep
// =========================================================================

void VM::collectGarbage() {
    std::vector<GcObject*> roots;

    // 1. Stack values
    for (size_t i = 0; i < stackTopIndex; i++) {
        pushGcRefs(stack[i], roots);
    }

    // 2. Global variables
    for (auto& v : globalValues) {
        pushGcRefs(v, roots);
    }

    // 3. Open upvalues (shared_ptr<Value> heap-allocated Values)
    for (auto& [slot, uv] : openUpvalues) {
        pushGcRefs(*uv, roots);
    }

    // 4. Call frames — the executing functions
    for (auto& frame : frames) {
        if (frame.function) {
            roots.push_back(frame.function.get());
        }
    }

    GcHeap::instance().collect(roots);
}

// =========================================================================
// Execution loop
// =========================================================================

InterpretResult VM::run() {
    try {
#define READ_BYTE() (*ip++)
#define READ_SHORT() (static_cast<uint16_t>(READ_BYTE()) | (static_cast<uint16_t>(READ_BYTE()) << 8))
#define READ_CONSTANT() (currentChunk->constants[READ_BYTE()])
#define READ_LONG_CONSTANT() (currentChunk->constants[READ_SHORT()])
#define RUNTIME_ERROR_OR_THROW(msg) \
    do { \
        std::string _errMsg = (msg); \
        if (throwException(GcHeap::instance().alloc<GcString>(_errMsg))) { goto dispatch; } \
        runtimeError(_errMsg); \
        return InterpretResult::RUNTIME_ERROR; \
    } while (false)

    for (;;) {
        OpCode instruction = static_cast<OpCode>(READ_BYTE());

        switch (instruction) {
            // --- Constants ---
            case OpCode::OP_CONSTANT: {
                push(READ_CONSTANT());
                break;
            }
            case OpCode::OP_CONSTANT_LONG: {
                push(READ_LONG_CONSTANT());
                break;
            }
            case OpCode::OP_NULL:  push(nullptr); break;
            case OpCode::OP_TRUE:  push(true);  break;
            case OpCode::OP_FALSE: push(false); break;

            // --- Stack ---
            case OpCode::OP_POP: pop(); break;
            case OpCode::OP_POPN: {
                uint8_t count = READ_BYTE();
                if (stackTopIndex < count) {
                    RUNTIME_ERROR_OR_THROW("Stack underflow in OP_POPN");
                } else {
                    stackTopIndex -= count;
                }
                break;
            }

            // --- Locals ---
            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(stack[frameBaseIndex + slot]);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                stack[frameBaseIndex + slot] = peek(0);
                break;
            }

            case OpCode::OP_DUP: {
                push(peek(0));
                break;
            }

            // --- Unary ---
            case OpCode::OP_NEGATE: {
                if (std::holds_alternative<int64_t>(peek(0))) {
                    int64_t v = std::get<int64_t>(pop());
                    if (v == INT64_MIN) {
                        push(-static_cast<double>(v));
                    } else {
                        push(-v);
                    }
                } else if (std::holds_alternative<double>(peek(0))) {
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
                bool addError = false;
                Value result = addValues(a, b, addError);
                if (addError) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for +");
                }
                push(result);
                break;
            }
            case OpCode::OP_DIVIDE: {
                Value b = pop();
                Value a = pop();
                if (isNumeric(a) && isNumeric(b)) {
                    double db = toDouble(b);
                    if (db == 0) {
                        RUNTIME_ERROR_OR_THROW("Division by zero");
                    }
                    push(toDouble(a) / db);
                } else {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for division");
                }
                break;
            }
            case OpCode::OP_MODULO: {
                Value b = pop();
                Value a = pop();
                if (isNumeric(a) && isNumeric(b)) {
                    double db = toDouble(b);
                    if (db == 0) {
                        RUNTIME_ERROR_OR_THROW("Modulo by zero");
                    }
                    push(std::fmod(toDouble(a), db));
                } else {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for modulo");
                }
                break;
            }
            case OpCode::OP_POWER: {
                Value b = pop();
                Value a = pop();
                if (isNumeric(a) && isNumeric(b)) {
                    push(std::pow(toDouble(a), toDouble(b)));
                } else {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for power (**)");
                }
                break;
            }

            // --- Fast numeric arithmetic (with type checks) ---
            case OpCode::OP_SUB_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!isNumeric(aVal) || !isNumeric(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for -");
                }
                push(toDouble(aVal) - toDouble(bVal));
                break;
            }
            case OpCode::OP_MUL_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!isNumeric(aVal) || !isNumeric(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for *");
                }
                push(toDouble(aVal) * toDouble(bVal));
                break;
            }
            case OpCode::OP_DIV_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!isNumeric(aVal) || !isNumeric(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for /");
                }
                double b = toDouble(bVal);
                if (b == 0) {
                    RUNTIME_ERROR_OR_THROW("Division by zero");
                }
                push(toDouble(aVal) / b);
                break;
            }
            case OpCode::OP_MOD_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!isNumeric(aVal) || !isNumeric(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for %");
                }
                double b = toDouble(bVal);
                if (b == 0) {
                    RUNTIME_ERROR_OR_THROW("Modulo by zero");
                }
                push(std::fmod(toDouble(aVal), b));
                break;
            }
            case OpCode::OP_LESS_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!isNumeric(aVal) || !isNumeric(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for <");
                }
                push(toDouble(aVal) < toDouble(bVal));
                break;
            }
            case OpCode::OP_LESS_EQ_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!isNumeric(aVal) || !isNumeric(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for <=");
                }
                push(toDouble(aVal) <= toDouble(bVal));
                break;
            }
            case OpCode::OP_GREATER_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!isNumeric(aVal) || !isNumeric(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for >");
                }
                push(toDouble(aVal) > toDouble(bVal));
                break;
            }
            case OpCode::OP_GREATER_EQ_NN: {
                Value bVal = pop();
                Value aVal = pop();
                if (!isNumeric(aVal) || !isNumeric(bVal)) {
                    RUNTIME_ERROR_OR_THROW("Invalid operands for >=");
                }
                push(toDouble(aVal) >= toDouble(bVal));
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
                if (isNumeric(a) && isNumeric(b)) {
                    push(toDouble(a) < toDouble(b));
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
                // Safe point: collect garbage if threshold exceeded.
                if (GcHeap::instance().needsGC()) {
                    collectGarbage();
                }

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
                if (!std::holds_alternative<GcPtr<FunctionPrototype>>(protoVal)) {
                    RUNTIME_ERROR_OR_THROW("OP_CLOSURE: expected function prototype in constant pool");
                }
                auto protoPtr = std::get<GcPtr<FunctionPrototype>>(protoVal);
                auto function = GcHeap::instance().alloc<VoraFunction>(
                    protoPtr->name, protoPtr->arity,
                    protoPtr->requiredArity, protoPtr.get());

                // Read upvalue descriptors and capture upvalues
                uint8_t upvalueCount = READ_BYTE();
                for (uint8_t u = 0; u < upvalueCount; u++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        function->upvalues.push_back(
                            std::make_shared<Value>(stack[frameBaseIndex + index]));
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
                stackTopIndex = frameBaseIndex;

                // Clear any open upvalues for this frame (locals going out of scope)
                for (auto it = openUpvalues.begin(); it != openUpvalues.end(); ) {
                    if (it->first >= frameBaseIndex && it->first < frameBaseIndex + MAX_LOCALS_PER_FRAME) {
                        it = openUpvalues.erase(it);
                    } else {
                        ++it;
                    }
                }

                // Restore caller's frame
                CallFrame frame = frames.back();
                frames.pop_back();
                ip = frame.returnIp;
                frameBaseIndex = frame.frameBase;
                currentChunk = frame.callerChunk;

                // Clean up any catch handlers that belonged to the returning
                // function (their frameCount references the now-popped frame).
                while (!catchHandlers.empty() &&
                       catchHandlers.back().frameCount > frames.size()) {
                    catchHandlers.pop_back();
                }

                // Push return value
                push(returnValue);
                break;
            }

            // --- Default parameters ---
            case OpCode::OP_DEFAULT_PARAM: {
                uint8_t slot = READ_BYTE();
                uint16_t skipOffset = readShort();
                if (slot < currentArgCount) {
                    // Arg was provided by caller — skip the default eval code
                    ip += skipOffset;
                }
                // else: fall through to evaluate default expression
                break;
            }

            // --- Upvalues ---
            case OpCode::OP_GET_UPVALUE: {
                uint8_t idx = READ_BYTE();
                GcPtr<VoraFunction> fn;
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
                GcPtr<VoraFunction> fn;
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
                size_t slotIdx = frameBaseIndex + localSlot;
                auto it = openUpvalues.find(slotIdx);
                if (it != openUpvalues.end()) {
                    // Sync current stack value to the heap-allocated upvalue,
                    // then remove from open upvalues so subsequent captures on
                    // this slot create a new heap allocation.
                    *(it->second) = stack[slotIdx];
                    openUpvalues.erase(it);
                }
                break;
            }

            // --- Arrays ---
            case OpCode::OP_ARRAY: {
                uint8_t count = READ_BYTE();
                auto arr = GcHeap::instance().alloc<Array>();
                arr->elements.reserve(count);
                size_t startIdx = stackTopIndex - count;
                for (uint8_t i = 0; i < count; i++) {
                    arr->elements.push_back(stack[startIdx + i]);
                }
                stackTopIndex -= count;
                push(arr);
                break;
            }
            case OpCode::OP_DICT: {
                uint8_t pairCount = READ_BYTE();
                auto dict = GcHeap::instance().alloc<Dict>();
                size_t startIdx = stackTopIndex - pairCount * 2;
                for (uint8_t i = 0; i < pairCount; i++) {
                    std::string key = valueToString(stack[startIdx + i * 2]);
                    dict->pairs[key] = stack[startIdx + i * 2 + 1];
                }
                stackTopIndex -= pairCount * 2;
                push(dict);
                break;
            }
            case OpCode::OP_INDEX: {
                Value indexVal = pop();
                Value target = pop();

                // Dict: string key lookup, or numeric index for for-in iteration
                if (std::holds_alternative<GcPtr<Dict>>(target)) {
                    const auto& dpairs =
                        std::get<GcPtr<Dict>>(target)->pairs;
                    if (isNumeric(indexVal)) {
                        // Numeric index: return Nth key (for for-in iteration)
                        size_t idx = static_cast<size_t>(toDouble(indexVal));
                        if (idx >= dpairs.size()) {
                            RUNTIME_ERROR_OR_THROW("Index out of bounds");
                        }
                        auto it = dpairs.begin();
                        std::advance(it, idx);
                        push(GcHeap::instance().alloc<GcString>(it->first));
                    } else {
                        // String key: lookup by name
                        std::string key = valueToString(indexVal);
                        auto it = dpairs.find(key);
                        if (it == dpairs.end()) {
                            RUNTIME_ERROR_OR_THROW("Key not found: " + key);
                        }
                        push(it->second);
                    }
                    break;
                }

                if (!isNumeric(indexVal)) {
                    RUNTIME_ERROR_OR_THROW("Index must be a number");
                }

                double rawIndex = toDouble(indexVal);
                if (rawIndex < 0 || std::floor(rawIndex) != rawIndex) {
                    RUNTIME_ERROR_OR_THROW("Index must be a non-negative integer");
                }

                size_t index = static_cast<size_t>(rawIndex);

                if (std::holds_alternative<GcPtr<GcString>>(target)) {
                    const auto& str = std::get<GcPtr<GcString>>(target)->value;
                    if (index >= str.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    push(GcHeap::instance().alloc<GcString>(std::string(1, str[index])));
                } else if (std::holds_alternative<GcPtr<Array>>(target)) {
                    const auto& elements =
                        std::get<GcPtr<Array>>(target)->elements;
                    if (index >= elements.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    push(elements[index]);
                } else if (std::holds_alternative<GcPtr<ObjectInstance>>(target)) {
                    const auto& props =
                        std::get<GcPtr<ObjectInstance>>(target)->properties;
                    if (index >= props.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    auto it = props.begin();
                    std::advance(it, index);
                    push(GcHeap::instance().alloc<GcString>(it->first));
                } else {
                    RUNTIME_ERROR_OR_THROW("Indexing requires an array, string, or object");
                }
                break;
            }
            case OpCode::OP_SET_INDEX: {
                Value value = pop();
                Value indexVal = pop();
                Value target = pop();

                // Dict: string-keyed assignment (handled first since keys are non-numeric)
                if (std::holds_alternative<GcPtr<Dict>>(target)) {
                    std::string key = valueToString(indexVal);
                    std::get<GcPtr<Dict>>(target)->pairs[key] = value;
                    push(value);
                    break;
                }

                if (!isNumeric(indexVal)) {
                    RUNTIME_ERROR_OR_THROW("Index must be a number");
                }

                double rawIndex = toDouble(indexVal);
                if (rawIndex < 0 || std::floor(rawIndex) != rawIndex) {
                    RUNTIME_ERROR_OR_THROW("Index must be a non-negative integer");
                }

                size_t index = static_cast<size_t>(rawIndex);

                if (std::holds_alternative<GcPtr<Array>>(target)) {
                    auto& elements =
                        std::get<GcPtr<Array>>(target)->elements;
                    if (index >= elements.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    elements[index] = value;
                    push(value);
                } else if (std::holds_alternative<GcPtr<GcString>>(target)) {
                    RUNTIME_ERROR_OR_THROW("Cannot assign to string index (strings are immutable)");
                } else {
                    RUNTIME_ERROR_OR_THROW("Index assignment requires an array");
                }
                break;
            }

            // --- Objects ---
            case OpCode::OP_GET_PROPERTY: {
                uint8_t nameIndex = READ_BYTE();
                std::string propName = std::get<GcPtr<GcString>>(currentChunk->constants[nameIndex])->value;
                Value obj = pop();

                // Array built-in methods & properties
                if (std::holds_alternative<GcPtr<Array>>(obj)) {
                    auto arr = std::get<GcPtr<Array>>(obj);
                    if (propName == "length") {
                        push(static_cast<int64_t>(arr->elements.size()));
                        break;
                    }
                    auto method = getArrayMethod(propName, arr);
                    if (method) { push(method); break; }
                    RUNTIME_ERROR_OR_THROW("Unknown array method: " + propName);
                }

                // String built-in methods & properties
                if (std::holds_alternative<GcPtr<GcString>>(obj)) {
                    const auto& str = std::get<GcPtr<GcString>>(obj)->value;
                    if (propName == "length") {
                        push(static_cast<int64_t>(str.size()));
                        break;
                    }
                    auto method = getStringMethod(propName, str);
                    if (method) { push(method); break; }
                    RUNTIME_ERROR_OR_THROW("Unknown string method: " + propName);
                }

                // Dict built-in methods & properties
                if (std::holds_alternative<GcPtr<Dict>>(obj)) {
                    auto dict = std::get<GcPtr<Dict>>(obj);
                    if (propName == "length") {
                        push(static_cast<int64_t>(dict->pairs.size()));
                        break;
                    }
                    auto method = getDictMethod(propName, dict);
                    if (method) { push(method); break; }
                    RUNTIME_ERROR_OR_THROW("Unknown dict method: " + propName);
                }

                if (std::holds_alternative<GcPtr<ObjectInstance>>(obj)) {
                    auto instance = std::get<GcPtr<ObjectInstance>>(obj);
                    // Check instance properties
                    auto it = instance->properties.find(propName);
                    if (it != instance->properties.end()) {
                        push(it->second);
                        break;
                    }
                    // Check class methods via MRO (C3 linearization)
                    if (instance->classDefinition) {
                        for (const auto& wp : instance->classDefinition->mro) {
                            auto cls = wp;
                            if (!cls) continue;
                            auto methodIt = cls->methods.find(propName);
                            if (methodIt != cls->methods.end()) {
                                // Create bound method wrapping the instance and prototype.
                                auto methodFn = methodIt->second;
                                const FunctionPrototype* mp = methodFn->getPrototype();
                                if (!mp) {
                                    RUNTIME_ERROR_OR_THROW("Method has no compiled body");
                                }
                                auto bound = GcHeap::instance().alloc<BoundMethod>(
                                    instance, mp, methodFn);
                                push(bound);
                                goto method_found;
                            }
                        }
                        method_found:
                        break;
                    }
                    RUNTIME_ERROR_OR_THROW("Undefined property: " + propName);
                } else {
                    RUNTIME_ERROR_OR_THROW("Can only access properties on objects");
                }
                break;
            }
            case OpCode::OP_SET_PROPERTY: {
                uint8_t nameIndex = READ_BYTE();
                std::string propName = std::get<GcPtr<GcString>>(currentChunk->constants[nameIndex])->value;
                Value value = pop();
                Value obj = pop();

                if (std::holds_alternative<GcPtr<ObjectInstance>>(obj)) {
                    auto instance = std::get<GcPtr<ObjectInstance>>(obj);
                    instance->properties[propName] = value;
                    push(value);
                } else {
                    RUNTIME_ERROR_OR_THROW("Can only set properties on objects");
                }
                break;
            }
            case OpCode::OP_GET_SUPER: {
                uint8_t nameIndex = READ_BYTE();
                std::string propName = std::get<GcPtr<GcString>>(currentChunk->constants[nameIndex])->value;

                // Get 'this' from local slot 0 (must be in a method)
                Value thisVal = stack[frameBaseIndex];
                if (!std::holds_alternative<GcPtr<ObjectInstance>>(thisVal)) {
                    RUNTIME_ERROR_OR_THROW("'super' used outside of method");
                }
                auto instance = std::get<GcPtr<ObjectInstance>>(thisVal);

                if (!instance->classDefinition) {
                    RUNTIME_ERROR_OR_THROW("Object has no class definition");
                }

                const auto& mro = instance->classDefinition->mro;
                if (mro.size() < 2) {
                    RUNTIME_ERROR_OR_THROW("No parent class for super." + propName);
                }

                // Find which class in the MRO defines the currently executing method.
                // super.method() should search starting from the NEXT class after the
                // defining class.
                size_t startIdx = 1;  // default: skip self (the instance's own class)
                if (!frames.empty() && frames.back().function) {
                    const auto* currentProto = frames.back().function->getPrototype();
                    if (currentProto) {
                        for (size_t i = 0; i < mro.size(); i++) {
                            auto cls = mro[i];
                            if (!cls) continue;
                            for (const auto& [mname, mfn] : cls->methods) {
                                if (mfn->getPrototype() == currentProto) {
                                    startIdx = i + 1;  // start from next class after defining class
                                    goto super_start_found;
                                }
                            }
                        }
                    }
                }
                super_start_found:

                if (startIdx >= mro.size()) {
                    RUNTIME_ERROR_OR_THROW("No parent class for super." + propName);
                }

                bool found = false;
                for (size_t i = startIdx; i < mro.size(); i++) {
                    auto cls = mro[i];
                    if (!cls) continue;
                    auto methodIt = cls->methods.find(propName);
                    if (methodIt != cls->methods.end()) {
                        auto methodFn = methodIt->second;
                        const FunctionPrototype* mp = methodFn->getPrototype();
                        if (!mp) {
                            RUNTIME_ERROR_OR_THROW("Method has no compiled body");
                        }
                        auto bound = GcHeap::instance().alloc<BoundMethod>(
                            instance, mp, methodFn);
                        push(bound);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    RUNTIME_ERROR_OR_THROW("Undefined super property: " + propName);
                }
                break;
            }
            case OpCode::OP_CLASS: {
                uint8_t classIndex = READ_BYTE();
                Value classDefVal = currentChunk->constants[classIndex];

                if (!std::holds_alternative<GcPtr<ClassDefinition>>(classDefVal)) {
                    RUNTIME_ERROR_OR_THROW("OP_CLASS: expected class definition in constant pool");
                }
                auto cd = std::get<GcPtr<ClassDefinition>>(classDefVal);

                // Resolve parent classes from globals (integer-indexed lookup)
                std::vector<GcPtr<ClassDefinition>> resolvedParents;
                for (const auto& parentName : cd->parentNames) {
                    auto it = globalIndex.find(parentName);
                    if (it != globalIndex.end()) {
                        int pSlot = it->second;
                        if (static_cast<size_t>(pSlot) < globalValues.size() &&
                            globalDefined[static_cast<size_t>(pSlot)]) {
                            const auto& parentVal = globalValues[static_cast<size_t>(pSlot)];
                            if (auto callable = std::get_if<GcPtr<Callable>>(&parentVal)) {
                                if (*callable) {
                                    auto parentDef = (*callable)->getClassDef();
                                    if (parentDef) {
                                        resolvedParents.push_back(parentDef);
                                        cd->parentClasses.push_back(parentDef);
                                    }
                                }
                            }
                        }
                    }
                }

                // Store method VoraFunctions on the ClassDefinition
                for (const auto& methodProto : cd->methodProtos) {
                    auto methodFn = GcHeap::instance().alloc<VoraFunction>(
                        methodProto->name, methodProto->arity,
                        methodProto->requiredArity, methodProto.get());
                    cd->methods[methodProto->name] = methodFn;
                }

                // Compute C3 MRO and cache on ClassDefinition
                {
                    auto mroShared = computeC3MRO(cd, resolvedParents);
                    cd->mro = std::move(mroShared);
                }

                // Create constructor VoraFunction
                auto ctorFn = GcHeap::instance().alloc<VoraFunction>(
                    cd->ctorProto->name, cd->ctorProto->arity,
                    cd->ctorProto->requiredArity, cd->ctorProto.get());

                // Create ClassConstructor — the clean, dedicated constructor callable.
                auto ctorCallable = GcHeap::instance().alloc<ClassConstructor>(
                    cd, ctorFn,
                    globalNames, globalValues, globalDefined, globalIndex);

                push(ctorCallable);
                break;
            }

            // --- Exception handling ---
            case OpCode::OP_PUSH_CATCH: {
                // localCount encodes how many locals exist at handler registration.
                // throwException() pushes the exception value at frameBaseIndex + localCount,
                // which is the first free slot after all existing locals — this avoids
                // overwriting function parameters or other locals at lower slots.
                // We also store the frame's own base (frameBaseIndex) for correct
                // restoration after throwException unwinds the call stack.
                uint8_t localCount = readByte();
                uint16_t catchOffset = readShort();
                size_t caughtFrameBase = frameBaseIndex;
                size_t exceptionSlot = caughtFrameBase + localCount;
                catchHandlers.push_back({ip + catchOffset, caughtFrameBase, exceptionSlot, currentChunk, frames.size()});
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
    } catch (const std::runtime_error& e) {
        runtimeError(e.what());
        return InterpretResult::RUNTIME_ERROR;
    }
}

} // namespace vora
