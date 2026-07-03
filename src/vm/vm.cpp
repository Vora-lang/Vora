#include "vm.h"
#include "opcode.h"
#include "value_ops.h"

#include "../gc/gc_heap.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../runtime/bound_method.h"
#include "../runtime/builtins.h"
#include "../runtime/class_constructor.h"
#include "../runtime/native_function.h"
#include "../runtime/vora_function.h"
#include "../vm/compiler.h"  // for FunctionPrototype, ClassDefinition

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace vora {

volatile sig_atomic_t VM::interruptFlag = 0;

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
    // Reserve enough capacity that open-upvalue pointers (raw Value*
    // into the stack vector) are never invalidated by reallocation.
    stack.reserve(65536);
    resetStack();
    frameBaseIndex = 0;
}

void VM::resetStack() {
    stackTopIndex = 0;
}

void VM::push(Value value) {
    if (stackTopIndex >= stack.capacity()) {
        runtimeError("Stack overflow");
        stackErrorFlag = true;  // halt dispatch loop at next iteration
        return;
    }
    if (stackTopIndex < stack.size())
        stack[stackTopIndex] = std::move(value);
    else
        stack.push_back(std::move(value));
    stackTopIndex++;
}

Value VM::pop() {
    if (stackTopIndex == 0) {
        runtimeError("Stack underflow");
        return Value(nullptr);
    }
    return std::move(stack[--stackTopIndex]);
}

Value& VM::peek(int distance) {
    if (distance < 0 || static_cast<size_t>(distance) >= stackTopIndex) {
        runtimeError("Stack underflow in peek()");
        // Return reference to a static sentinel; the VM will stop on the
        // next opcode boundary via the error flag set by runtimeError.
        static Value sentinel;
        return sentinel;
    }
    return stack[stackTopIndex - 1 - distance];
}

const Value& VM::peek(int distance) const {
    if (distance < 0 || static_cast<size_t>(distance) >= stackTopIndex) {
        // Const version: can't call runtimeError, so return sentinel.
        // This path is only used by valueToString / debug helpers.
        static const Value sentinel;
        return sentinel;
    }
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
    // Fallback: if offset is beyond the RLE range (shouldn't happen in correct
    // bytecode, but can occur with a corrupted ip), use the last known line.
    if (line == 0 && !rleLines.empty()) {
        line = rleLines.back();
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
    if (column == 0 && !rleCols.empty()) {
        column = rleCols.back();
    }
}

} // anonymous namespace

void VM::runtimeError(const std::string& message) {
    if (!currentChunk || currentChunk->code.empty()) {
        std::cerr << "RuntimeError: " << message << "\n";
        if (!lastErrorStackTrace.empty()) {
            std::cerr << lastErrorStackTrace;
            lastErrorStackTrace.clear();
        }
        return;
    }

    size_t offset = ip - currentChunk->code.data();
    int line = 0, column = 0;
    decodeLineFromChunk(*currentChunk, offset, line, column);

    // Report via ErrorReporter if available, otherwise print to stderr.
    if (errorReporter) {
        errorReporter->error(line, column, 1, message);
    } else {
        printSourceLine(std::cerr, currentChunk->source, line, column, 1,
                        message, "RuntimeError");
    }

    if (!lastErrorStackTrace.empty()) {
        std::cerr << lastErrorStackTrace;
        lastErrorStackTrace.clear();
    } else {
        std::cerr << captureStackTrace();
    }
}

void VM::runtimeError(const std::string& message, int line, int column) {
    // Report via ErrorReporter if available, otherwise print to stderr.
    if (errorReporter) {
        errorReporter->error(line, column, 1, message);
    } else if (currentChunk) {
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

std::shared_ptr<Upvalue> VM::captureUpvalue(size_t slotIndex) {
    // Check if this slot already has an open upvalue
    auto it = openUpvalues.find(slotIndex);
    if (it != openUpvalues.end()) {
        return it->second;
    }
    // Create a new Upvalue pointing to the live stack slot via
    // stable index (stackPtr + slotIndex). The stack vector itself
    // is a member of VM and has a stable address — only its internal
    // buffer moves on reallocation, which the index handles correctly.
    auto uv = std::make_shared<Upvalue>();
    uv->stackPtr = &stack;
    uv->slotIndex = slotIndex;
    openUpvalues[slotIndex] = uv;
    return uv;
}

void VM::flushDeferStack() {
    // Capture the frame index — NOT a reference, since callVoraFunction()
    // may reallocate the frames vector, invalidating references.
    size_t frameIndex = frames.size() - 1;
    while (!frames[frameIndex].deferStack.empty()) {
        Value closure = std::move(frames[frameIndex].deferStack.back());
        frames[frameIndex].deferStack.pop_back();
        if (!closure.isCallable()) continue;
        auto callable = closure.asCallable();
        if (auto* vfn = dynamic_cast<VoraFunction*>(callable.get())) {
            size_t savedFramesSize = frames.size();
            bool savedExceptionInFlight = exceptionInFlight;
            exceptionInFlight = false;
            auto result = callVoraFunction(GcPtr<VoraFunction>(vfn), {});
            if (result == InterpretResult::OK) {
                deferExecutionLimit = static_cast<int>(savedFramesSize);
                run();
                deferExecutionLimit = -1;
            }
            exceptionInFlight = savedExceptionInFlight;
            // Pop the defer closure's return value (discarded)
            if (stackTopIndex > 0) pop();
        }
    }
}

bool VM::throwException(const Value& value) {
    // Capture stack trace BEFORE unwinding — once frames are popped, the
    // call chain is lost. If this exception is uncaught, runtimeError()
    // will print the captured trace.
    lastErrorStackTrace = captureStackTrace();

    // Auto-inject stack trace into structured thrown values (dicts/objects)
    // so catch blocks can access e.stack for diagnostics.
    if (!lastErrorStackTrace.empty()) {
        if (value.isDict()) {
            auto dictPtr = value.asDict();
            if (dictPtr) {
                dictPtr->pairs["stack"] =
                    GcHeap::instance().alloc<GcString>(lastErrorStackTrace);
            }
        } else if (value.isObjectInstance()) {
            auto objPtr = value.asObjectInstance();
            if (objPtr) {
                objPtr->properties["stack"] =
                    GcHeap::instance().alloc<GcString>(lastErrorStackTrace);
            }
        }
    }

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
            // Execute defer closures for this frame BEFORE popping it.
            // Safe: defer upvalues reference live stack slots (frame not yet popped).
            flushDeferStack();

            // Close + clear open upvalues for the frame being unwound
            size_t frameEnd = frames.back().frameBase + MAX_LOCALS_PER_FRAME;
            auto it = openUpvalues.begin();
            while (it != openUpvalues.end()) {
                if (it->first >= frames.back().frameBase && it->first < frameEnd) {
                    it->second->close();
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

Value VM::getGlobal(const std::string& name) const {
    auto it = globalIndex.find(name);
    if (it == globalIndex.end()) return nullptr;
    int slot = it->second;
    if (!globalDefined[static_cast<size_t>(slot)]) return nullptr;
    return globalValues[static_cast<size_t>(slot)];
}

void VM::setGlobal(const std::string& name, const Value& value) {
    auto it = globalIndex.find(name);
    int slot;
    if (it != globalIndex.end()) {
        slot = it->second;
    } else {
        slot = static_cast<int>(globalNames.size());
        globalNames.push_back(name);
        globalValues.push_back(nullptr);
        globalDefined.push_back(false);
        globalIndex[name] = slot;
    }
    globalValues[static_cast<size_t>(slot)] = value;
    globalDefined[static_cast<size_t>(slot)] = true;
    markGlobalDirty(slot);
}

bool VM::hasGlobal(const std::string& name) const {
    auto it = globalIndex.find(name);
    if (it == globalIndex.end()) return false;
    return globalDefined[static_cast<size_t>(it->second)];
}

void VM::registerNativeFunction(const std::string& name,
                                std::function<Value(const std::vector<Value>&)> fn) {
    defineNative(name, -1, std::move(fn));
}

// ── Debugger API ────────────────────────────────────────────────────────────

void VM::debugSetBreakpoint(size_t offset) { breakpoints_.insert(offset); }
void VM::debugRemoveBreakpoint(size_t offset) { breakpoints_.erase(offset); }
void VM::debugClearBreakpoints() { breakpoints_.clear(); }
bool VM::debugHasBreakpoint(size_t offset) const { return breakpoints_.count(offset) > 0; }

void VM::debugSetStepMode(StepMode mode) {
    stepMode_ = mode;
    if (mode == StepMode::StepOver) {
        stepOverDepth_ = static_cast<int>(frames.size());
    } else if (mode == StepMode::StepOut) {
        stepOutDepth_ = static_cast<int>(frames.size());
    }
}
void VM::debugResume() { debugPaused_ = false; }
void VM::debugPause() { interruptFlag = 1; }

int VM::debugFrameCount() const { return static_cast<int>(frames.size()) + 1; } // +1 for top-level

std::string VM::debugFrameName(int frameIndex) const {
    if (frameIndex < 0 || frameIndex >= debugFrameCount()) return "<unknown>";
    if (frameIndex == 0) return "<script>";
    int idx = frameIndex - 1;
    if (idx >= static_cast<int>(frames.size())) return "<unknown>";
    auto& fn = frames[idx].function;
    return fn ? fn->name() : "<native>";
}

int VM::debugFrameLine(int frameIndex) const {
    if (frameIndex < 0 || frameIndex >= debugFrameCount()) return 1;
    // Decode line from current chunk's RLE table
    size_t offset;
    const Chunk* chunk;
    if (frameIndex == 0) {
        offset = ip ? (ip - currentChunk->code.data()) : 0;
        chunk = currentChunk;
    } else {
        int idx = frameIndex - 1;
        if (idx >= static_cast<int>(frames.size())) return 1;
        auto& frame = frames[idx];
        offset = frame.returnIp ? (frame.returnIp - frame.callerChunk->code.data()) : 0;
        chunk = frame.callerChunk;
    }
    if (!chunk || chunk->lines.empty() || offset >= chunk->code.size()) return 1;
    // RLE decode: find line at bytecode offset
    size_t pos = 0;
    for (size_t i = 0; i < chunk->lines.size(); i += 2) {
        int runLen = chunk->lines[i];
        if (offset < pos + static_cast<size_t>(runLen)) return chunk->lines[i + 1];
        pos += runLen;
    }
    return 1;
}

size_t VM::debugFrameBytecodeOffset(int frameIndex) const {
    if (frameIndex == 0) return ip ? (ip - currentChunk->code.data()) : 0;
    int idx = frameIndex - 1;
    if (idx >= static_cast<int>(frames.size())) return 0;
    auto& frame = frames[idx];
    return frame.returnIp ? (frame.returnIp - frame.callerChunk->code.data()) : 0;
}

std::vector<std::string> VM::debugLocalNames(int frameIndex) const {
    if (frameIndex == 0) return {}; // top-level: no locals (use globals)
    int idx = frameIndex - 1;
    if (idx >= static_cast<int>(frames.size())) return {};
    auto& fn = frames[idx].function;
    if (!fn) return {};
    auto* proto = fn->getPrototype();
    if (!proto) return {};
    return proto->localNames;
}

Value VM::debugLocalValue(int frameIndex, int slot) const {
    if (frameIndex == 0 || frameIndex - 1 >= static_cast<int>(frames.size()))
        return nullptr;
    auto& frame = frames[frameIndex - 1];
    size_t localIdx = frame.frameBase + static_cast<size_t>(slot);
    if (localIdx >= stackTopIndex) return nullptr;
    return stack[localIdx];
}

size_t VM::debugCurrentOffset() const {
    return ip ? (ip - currentChunk->code.data()) : 0;
}

InterpretResult VM::debugEvaluate(const std::string& expr, Value& result) {
    // Compile and execute a simple expression in a temporary VM
    // seeded with the current global state.
    std::string source = "return (" + expr + ");";
    StderrErrorReporter reporter(source);
    Lexer lexer(source, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    if (parser.hasError()) return InterpretResult::COMPILE_ERROR;

    Compiler compiler(reporter);
    compiler.seedGlobals(globalNames);
    auto chunk = compiler.compile(prog.get());
    if (compiler.hadError) return InterpretResult::COMPILE_ERROR;

    // Create temp VM with current global state
    VM tempVM;
    tempVM.adoptGlobals(globalNames, globalValues, globalDefined, globalIndex);
    tempVM.initGlobals(compiler.getGlobalNames());
    InterpretResult res = tempVM.interpret(chunk);
    if (res == InterpretResult::OK && tempVM.stackTopIndex > 0) {
        result = tempVM.pop();
    }
    return res;
}

void VM::initGlobals(const std::vector<std::string>& names) {
    // Merge: only add globals that don't already exist, preserving
    // existing values. This is critical for REPL where multiple lines
    // share the same VM — otherwise every new line wipes all globals
    // defined by previous lines.
    for (const auto& name : names) {
        if (globalIndex.find(name) == globalIndex.end()) {
            size_t slot = globalNames.size();
            globalNames.push_back(name);
            globalValues.push_back(nullptr);
            globalDefined.push_back(false);
            globalIndex[name] = static_cast<int>(slot);
        }
    }

    // Register internal builtin used by for-in loop desugaring.
    defineNative("_vora_len", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (arg.isArray()) {
                return static_cast<double>(
                    arg.asArray()->elements.size());
            }
            if (arg.isGcString()) {
                return static_cast<double>(arg.asGcString()->value.size());
            }
            if (arg.isObjectInstance()) {
                return static_cast<double>(
                    arg.asObjectInstance()->properties.size());
            }
            if (arg.isDict()) {
                return static_cast<double>(
                    arg.asDict()->pairs.size());
            }
            if (arg.isSet()) {
                return static_cast<double>(
                    arg.asSet()->elements.size());
            }
            if (arg.isMap()) {
                return static_cast<double>(
                    arg.asMap()->pairs.size());
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

void VM::copyGlobalsFrom(const VM& source) {
    globalNames = source.globalNames;
    globalValues = source.globalValues;
    globalDefined = source.globalDefined;
    globalIndex = source.globalIndex;
    clearDirtyGlobals();
}

void VM::clearDirtyGlobals() {
    dirtyGlobalSlots_.clear();
}

void VM::markGlobalDirty(int slot) {
    // Grow flags vector if needed (should be rare — only when defining new globals)
    if (static_cast<size_t>(slot) >= globalDirtyFlags_.size()) {
        globalDirtyFlags_.resize(slot + 1, false);
    }
    if (!globalDirtyFlags_[slot]) {
        globalDirtyFlags_[slot] = true;
        dirtyGlobalSlots_.push_back(slot);
    }
}

void VM::mergeGlobalsTo(VM& target) const {
    for (int slot : dirtyGlobalSlots_) {
        if (static_cast<size_t>(slot) >= globalNames.size()) continue;
        if (!globalDefined[slot]) continue;

        const auto& name = globalNames[slot];
        auto it = target.globalIndex.find(name);
        if (it == target.globalIndex.end()) {
            // New global: add to target
            int newSlot = static_cast<int>(target.globalValues.size());
            target.globalNames.push_back(name);
            target.globalValues.push_back(globalValues[slot]);
            target.globalDefined.push_back(true);
            target.globalIndex[name] = newSlot;
        } else {
            // Existing: update value
            target.globalValues[it->second] = globalValues[slot];
            target.globalDefined[it->second] = true;
        }
    }
}

// =========================================================================
// Constructor execution (used by ClassConstructor)
// =========================================================================

InterpretResult VM::runConstructor(const FunctionPrototype& proto,
                                    const Value& instance,
                                    const std::vector<Value>& args) {
    resetStack();
    frameBaseIndex = 0;
    frames.clear();
    catchHandlers.clear();
    exceptionInFlight = false;
    currentChunk = &proto.chunk;
    ip = proto.chunk.code.data();
    push(instance);  // 'this' at slot 0
    // Push fixed params (up to arity), pad with null
    for (size_t i = 0; i < static_cast<size_t>(proto.arity); i++) {
        if (i < args.size()) push(args[i]);
        else push(nullptr);
    }
    // Push rest parameter if constructor has one
    if (proto.hasRest) {
        auto restArray = GcHeap::instance().alloc<Array>();
        for (size_t i = proto.arity; i < args.size(); i++) {
            restArray->elements.push_back(args[i]);
        }
        push(Value(GcPtr<Array>(restArray)));
    }
    currentArgCount = static_cast<uint8_t>(args.size());
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
    if (frames.size() >= MAX_CALL_FRAMES) {
        runtimeErrorOrThrow("Stack overflow: call depth limit reached");
        return false;
    }

    if (callee.isCallable()) {
        auto callable = callee.asCallable();

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

            // Push 'this' (bound instance)
            push(instance);

            // Push fixed method arguments (up to arity), pad with null
            for (size_t i = 0; i < static_cast<size_t>(proto->arity); i++) {
                if (i < arguments.size()) {
                    push(arguments[i]);
                } else {
                    push(nullptr);
                }
            }

            // Push rest parameter if method has one
            if (proto->hasRest) {
                auto restArray = GcHeap::instance().alloc<Array>();
                for (size_t i = proto->arity; i < arguments.size(); i++) {
                    restArray->elements.push_back(arguments[i]);
                }
                push(Value(GcPtr<Array>(restArray)));
            }

            // Set up call frame directly (inline callVoraFunction logic).
            frames.push_back({ip, frameBaseIndex, currentChunk, bound->getMethodFunc()});
            currentChunk = &proto->chunk;
            ip = currentChunk->code.data();
            frameBaseIndex = newFrameBaseIndex;  // points to instance at slot 0
            currentArgCount = argCount;  // for OP_DEFAULT_PARAM in methods

            // Set paramProvidedMask for the new frame
            uint64_t providedMask = 0;
            size_t pcount = std::min(static_cast<size_t>(argCount),
                                     static_cast<size_t>(proto->arity));
            for (size_t i = 0; i < pcount; i++) {
                providedMask |= (1ULL << i);
            }
            frames.back().paramProvidedMask = providedMask;
            return true;
        }

        // Class constructor: uses call(VM&, args) to sync globals from
        // the calling VM, then merge mutations back.
        if (auto ctor = dynamic_cast<ClassConstructor*>(callable.get())) {
            std::vector<Value> arguments(argCount);
            for (int i = argCount - 1; i >= 0; i--) {
                arguments[static_cast<size_t>(i)] = pop();
            }
            pop(); // pop the callable itself

            // Uses Callable::call(VM&, args) — ClassConstructor override
            // syncs globals, runs constructors, and merges back.
            Value result = callable->call(*this, arguments);
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
    // When hasRest is true, there is no upper bound — excess args
    // are collected into the rest array.
    if (func->hasRest()) {
        if (args.size() < static_cast<size_t>(func->requiredArity())) {
            runtimeErrorOrThrow("Wrong arity: expected at least " +
                std::to_string(func->requiredArity()) + " but got " +
                std::to_string(args.size()));
            return InterpretResult::RUNTIME_ERROR;
        }
    } else {
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
    }

    // Get the function's compiled prototype
    const FunctionPrototype* proto = func->getPrototype();
    if (!proto) {
        runtimeErrorOrThrow("Function has no compiled body");
        return InterpretResult::RUNTIME_ERROR;
    }

    // Generator function: create Generator lazily, don't execute body
    if (proto->isGenerator && !proto->isAsync) {
        auto gen = GcHeap::instance().alloc<Generator>();
        gen->function = func;
        gen->args = args;
        gen->done = false;
        gen->firstResume = true;
        push(gen);
        return InterpretResult::OK;
    }

    // Async function: create Generator+Task, then execute body eagerly
    if (proto->isAsync) {
        auto gen = GcHeap::instance().alloc<Generator>();
        gen->function = func;
        gen->args = args;
        gen->done = false;
        gen->firstResume = false;  // not first resume — we're executing now
        auto task = GcHeap::instance().alloc<Task>();
        task->generator = gen;
        task->state = Task::Pending;
        gen->task = task;

        // Save caller context for OP_RETURN to restore
        gen->callerIp = ip;
        gen->callerChunk = currentChunk;
        gen->callerFrameBase = frameBaseIndex;
        gen->callerFramesSize = frames.size();

        // Push Task on stack as the function's return value
        push(task);

        // Eagerly execute the body: push frame and start
        frames.push_back({ip, frameBaseIndex, currentChunk, func, gen});
        currentChunk = &proto->chunk;
        ip = currentChunk->code.data();
        frameBaseIndex = stackTopIndex;

        // Push arguments as locals
        for (size_t i = 0; i < static_cast<size_t>(func->arity()); i++) {
            if (i < args.size()) push(args[i]);
            else push(nullptr);
        }
        if (func->hasRest()) {
            auto restArray = GcHeap::instance().alloc<Array>();
            for (size_t i = func->arity(); i < args.size(); i++) {
                restArray->elements.push_back(args[i]);
            }
            push(Value(GcPtr<Array>(restArray)));
        }

        currentGenerator = gen;
        return InterpretResult::OK;
    }

    // Push return address, caller chunk, and function onto call frame stack
    frames.push_back({ip, frameBaseIndex, currentChunk, func});

    // Switch to function's chunk
    currentChunk = &proto->chunk;
    ip = currentChunk->code.data();
    frameBaseIndex = stackTopIndex;  // locals start after the current stack top

    // Store actual arg count for OP_DEFAULT_PARAM (legacy, kept for compatibility)
    currentArgCount = static_cast<uint8_t>(args.size());

    // Set paramProvidedMask on the new frame: first N slots are provided
    // where N = min(args.size(), func->arity()).
    uint64_t providedMask = 0;
    size_t providedCount = std::min(args.size(), static_cast<size_t>(func->arity()));
    for (size_t i = 0; i < providedCount; i++) {
        providedMask |= (1ULL << i);
    }
    frames.back().paramProvidedMask = providedMask;

    // Bind fixed parameters as locals at frameBase.
    // Pad with null up to total arity (default params will overwrite).
    for (size_t i = 0; i < static_cast<size_t>(func->arity()); i++) {
        if (i < args.size()) {
            push(args[i]);
        } else {
            push(nullptr);
        }
    }

    // Push rest parameter — collect excess args into an array
    if (func->hasRest()) {
        auto restArray = GcHeap::instance().alloc<Array>();
        for (size_t i = func->arity(); i < args.size(); i++) {
            restArray->elements.push_back(args[i]);
        }
        push(Value(GcPtr<Array>(restArray)));
    }

    return InterpretResult::OK;
}

InterpretResult VM::executeCallKw(uint8_t posCount, uint8_t kwCount,
                                   const std::vector<size_t>& kwNameIndices) {
    // Common logic for OP_CALL_KW and OP_CALL_KW_WIDE.
    // Caller reads name indices from bytecode (8-bit or 16-bit) and passes them
    // as size_t indices. On caught errors (exceptionInFlight set), the caller
    // must `goto dispatch` to jump to the catch handler.
    if (GcHeap::instance().needsGC()) {
        collectGarbage();
    }

    uint8_t totalArgs = posCount + kwCount;
    Value callee = peek(totalArgs);

    if (!callee.isCallable()) {
        if (runtimeErrorOrThrow("Can only call functions")) return InterpretResult::OK;
        return InterpretResult::RUNTIME_ERROR;
    }

    // Pop named arg values (reverse), positional args, then callee
    std::vector<Value> kwValues(kwCount);
    for (int i = kwCount - 1; i >= 0; i--) {
        kwValues[i] = pop();
    }
    std::vector<Value> posValues(posCount);
    for (int i = posCount - 1; i >= 0; i--) {
        posValues[i] = pop();
    }
    pop(); // callee

    auto callable = callee.asCallable();

    // Resolve FunctionPrototype to get parameter names
    FunctionPrototype const* proto = nullptr;
    GcPtr<VoraFunction> voraFn;

    if (auto vf = dynamic_cast<VoraFunction*>(callable.get())) {
        voraFn = GcPtr<VoraFunction>(vf);
        proto = vf->getPrototype();
    } else if (auto bm = dynamic_cast<BoundMethod*>(callable.get())) {
        voraFn = bm->getMethodFunc();
        proto = bm->getMethodProto();
    }

    if (!proto || proto->paramNames.empty()) {
        runtimeErrorOrThrow(
            "Named arguments require a Vora function with parameter names");
        return InterpretResult::RUNTIME_ERROR;
    }

    // Build name→slot map
    std::unordered_map<std::string, int> nameToSlot;
    for (size_t i = 0; i < proto->paramNames.size(); i++) {
        nameToSlot[proto->paramNames[i]] = static_cast<int>(i);
    }

    // Build reordered args: positional first, then named overlay
    std::vector<Value> args(proto->arity, nullptr);
    uint64_t providedMask = 0;

    // Fill positional args, skipping slots that will be filled by named args
    size_t posIdx = 0;
    for (int slot = 0; slot < proto->arity && posIdx < posCount; slot++) {
        bool filledByNamed = false;
        for (uint8_t k = 0; k < kwCount; k++) {
            size_t nameIdx = kwNameIndices[k];
            if (nameIdx < currentChunk->constants.size()) {
                const Value& nameVal = currentChunk->constants[nameIdx];
                if (nameVal.isGcString()) {
                    const std::string& kwName =
                        nameVal.asGcString()->value;
                    auto it = nameToSlot.find(kwName);
                    if (it != nameToSlot.end() && it->second == slot) {
                        filledByNamed = true;
                        break;
                    }
                }
            }
        }
        if (!filledByNamed) {
            args[slot] = posValues[posIdx++];
            providedMask |= (1ULL << slot);
        }
    }

    // Fill named args
    for (uint8_t k = 0; k < kwCount; k++) {
        size_t nameIdx = kwNameIndices[k];
        if (nameIdx >= currentChunk->constants.size() ||
            !currentChunk->constants[nameIdx].isGcString()) {
            runtimeErrorOrThrow("Invalid keyword argument name");
            return InterpretResult::RUNTIME_ERROR;
        }
        const std::string& kwName =
            currentChunk->constants[nameIdx].asGcString()->value;
        auto it = nameToSlot.find(kwName);
        if (it == nameToSlot.end()) {
            runtimeErrorOrThrow("Unknown parameter name: " + kwName);
            return InterpretResult::RUNTIME_ERROR;
        }
        int slot = it->second;
        if (providedMask & (1ULL << slot)) {
            runtimeErrorOrThrow(
                "Multiple values for parameter: " + kwName);
            return InterpretResult::RUNTIME_ERROR;
        }
        args[slot] = kwValues[k];
        providedMask |= (1ULL << slot);
    }

    // Count provided args for arity check
    int providedCount = 0;
    for (size_t i = 0; i < args.size(); i++) {
        if (providedMask & (1ULL << i)) providedCount++;
    }

    // Arity check
    if (proto->hasRest) {
        if (providedCount < proto->requiredArity) {
            runtimeErrorOrThrow("Wrong arity: expected at least " +
                std::to_string(proto->requiredArity) + " but got " +
                std::to_string(providedCount));
            return InterpretResult::RUNTIME_ERROR;
        }
    } else {
        if (providedCount < proto->requiredArity ||
            providedCount > proto->arity) {
            std::string expected;
            if (proto->requiredArity == proto->arity) {
                expected = std::to_string(proto->arity);
            } else {
                expected = std::to_string(proto->requiredArity) +
                           ".." + std::to_string(proto->arity);
            }
            runtimeErrorOrThrow("Wrong arity: expected " + expected +
                " but got " + std::to_string(providedCount));
            return InterpretResult::RUNTIME_ERROR;
        }
    }

    // Handle BoundMethod: push instance as 'this' slot 0
    if (auto bm = dynamic_cast<BoundMethod*>(callable.get())) {
        auto instance = bm->getInstance();
        size_t newFrameBaseIndex = stackTopIndex;
        push(instance);
        frames.push_back({nullptr, 0, nullptr, voraFn});
        auto result = callVoraFunction(voraFn, args);
        if (result != InterpretResult::OK) {
            return result;
        }
        if (!frames.empty()) {
            frames.back().frameBase = newFrameBaseIndex;
            frames.back().paramProvidedMask = providedMask;
        }
        return InterpretResult::OK;
    }

    // Direct VoraFunction call
    auto result = callVoraFunction(voraFn, args);
    if (result != InterpretResult::OK) {
        return result;
    }
    if (!frames.empty()) {
        frames.back().paramProvidedMask = providedMask;
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
    pendingResume = false;
    pendingGenerator = nullptr;
    currentGenerator = nullptr;
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

    // 3. Open upvalues — scan the current Value through Upvalue indirection
    for (auto& [slot, uv] : openUpvalues) {
        pushGcRefs(uv->get(), roots);
    }

    // 4. Call frames — the executing functions, defer stacks, and generators
    for (auto& frame : frames) {
        if (frame.function) {
            roots.push_back(frame.function.get());
        }
        if (frame.generator) {
            roots.push_back(frame.generator.get());
        }
        for (auto& v : frame.deferStack) {
            pushGcRefs(v, roots);
        }
    }

    // 5. Generator state — pending resume and currently executing
    if (pendingGenerator) {
        roots.push_back(pendingGenerator.get());
    }
    if (currentGenerator) {
        roots.push_back(currentGenerator.get());
    }

    // 6. Module cache — module Dict objects must survive
    for (auto& [path, modValue] : moduleCache) {
        pushGcRefs(modValue, roots);
    }

    // 7. Native error value — may hold a GcString set by native functions
    if (nativeError) {
        pushGcRefs(nativeErrorValue, roots);
    }

    GcHeap::instance().collectIfNeeded(roots);
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

    // Main bytecode dispatch loop.
    // Uses `goto dispatch` (computed-goto style) instead of `continue` to
    // jump back to the top of the switch. This is a deliberate performance
    // optimization: it avoids re-evaluating the outer `for (;;)` condition
    // and lets the compiler generate a direct jump table. The `for (;;)` is
    // only entered once; all subsequent iterations go through `goto dispatch`.
    for (;;) {
        // Check for interrupt request (e.g. Ctrl+C in REPL).
        // The flag is set by a signal handler and is safe to read
        // from any thread / signal context.
        if (interruptFlag) {
            interruptFlag = 0;
            std::cerr << "\nInterrupted" << std::endl;
            return InterpretResult::RUNTIME_ERROR;
        }
        if (stackErrorFlag) {
            stackErrorFlag = false;
            return InterpretResult::RUNTIME_ERROR;
        }

        // --- Defer execution limit (used by flushDeferStack) ---
        if (deferExecutionLimit >= 0 &&
            frames.size() <= static_cast<size_t>(deferExecutionLimit)) {
            return InterpretResult::OK;
        }

        // --- Generator pending resume ---
        // next(gen) sets this to redirect execution from the caller's
        // bytecode to the generator's saved instruction pointer.
        if (pendingResume) {
            // Pop the dummy nullptr that callValue pushed as next()'s return
            pop();

            // Push a frame for the generator
            frames.push_back({ip, frameBaseIndex, currentChunk,
                              pendingGenerator->function});

            // Redirect execution to the generator's chunk
            ip = pendingIp;
            currentChunk = pendingChunk;

            if (pendingGenerator->firstResume) {
                // First execution: set up fresh frame with captured arguments
                frameBaseIndex = stackTopIndex;
                size_t arity = static_cast<size_t>(pendingGenerator->function->arity());
                for (size_t i = 0; i < arity; i++) {
                    if (i < pendingGenerator->args.size())
                        push(pendingGenerator->args[i]);
                    else
                        push(nullptr);
                }
                pendingGenerator->firstResume = false;
            } else {
                // Resume: restore saved stack values at current stack top.
                // We cannot use the absolute savedFrameBase from yield time
                // because the caller's stack layout may differ between calls.
                frameBaseIndex = stackTopIndex;
                for (const auto& v : pendingGenerator->savedStack) {
                    push(v);
                }
            }

            currentGenerator = pendingGenerator;
            pendingGenerator = nullptr;
            pendingResume = false;
            // Fall through: next loop iteration will fetch instruction
            // from the generator's bytecode
        }

        // ── Debugger: breakpoint / step check ───────────────────────────
        if ((!breakpoints_.empty() || stepMode_ != StepMode::None) && currentChunk) {
            size_t offset = ip ? (ip - currentChunk->code.data()) : 0;
            bool shouldStop = false;

            if (breakpoints_.count(offset)) {
                shouldStop = true;
            }

            switch (stepMode_) {
                case StepMode::StepIn:
                    shouldStop = true;
                    break;
                case StepMode::StepOver:
                    if (frames.size() <= static_cast<size_t>(stepOverDepth_))
                        shouldStop = true;
                    break;
                case StepMode::StepOut:
                    if (frames.size() < static_cast<size_t>(stepOutDepth_))
                        shouldStop = true;
                    break;
                case StepMode::None:
                    break;
            }

            if (shouldStop) {
                stepMode_ = StepMode::None;
                debugPaused_ = true;
                // Rewind ip since we haven't read the opcode yet
                return InterpretResult::DEBUG_STOPPED;
            }
        }

        OpCode instruction = static_cast<OpCode>(READ_BYTE());

        // Shared variables for property-access opcodes (used by both fused
        // superinstructions and regular GET_PROPERTY/GET_PROPERTY_SAFE/etc.)
        uint16_t nameIndex;
        Value obj;

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
            case OpCode::OP_PRINT_POP: {
                // REPL mode: print top of stack (if non-null), then pop.
                // Used by expression statements to echo results like Python.
                Value v = pop();
                if (!v.isNull()) {
                    printValue(v);
                    std::cout << std::endl;
                }
                break;
            }
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
                size_t idx = frameBaseIndex + slot;
                if (idx >= stack.size()) {
                    RUNTIME_ERROR_OR_THROW("Invalid local variable index " + std::to_string(slot));
                    return InterpretResult::RUNTIME_ERROR;
                }
                push(stack[idx]);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                size_t idx = frameBaseIndex + slot;
                if (idx >= stack.size()) {
                    RUNTIME_ERROR_OR_THROW("Invalid local variable index " + std::to_string(slot));
                    return InterpretResult::RUNTIME_ERROR;
                }
                stack[idx] = peek(0);
                break;
            }

            case OpCode::OP_DUP: {
                push(peek(0));
                break;
            }

            // --- Unary ---
            case OpCode::OP_NEGATE: {
                if (peek(0).isInt()) {
                    int64_t v = pop().asInt();
                    if (v == INT64_MIN) {
                        push(-static_cast<double>(v));
                    } else {
                        push(-v);
                    }
                } else if (peek(0).isDouble()) {
                    push(-pop().asDouble());
                } else {
                    RUNTIME_ERROR_OR_THROW("Negation requires a number");
                }
                break;
            }
            case OpCode::OP_NOT: {
                push(!isTruthy(pop()));
                break;
            }
            case OpCode::OP_CONVERT: {
                // Convert top of stack to the annotated type.
                // Operand: uint8_t type tag — 0=float, 1=int, 2=bool, 3=string
                uint8_t typeTag = READ_BYTE();
                Value v = pop();
                switch (typeTag) {
                    case 0: { // float
                        if (v.isInt())
                            push(static_cast<double>(v.asInt()));
                        else if (v.isDouble())
                            push(v);
                        else if (v.isBool())
                            push(v.asBool() ? 1.0 : 0.0);
                        else if (v.isGcString()) {
                            try {
                                push(std::stod(v.asGcString()->value));
                            } catch (const std::exception&) {
                                push(0.0);
                            }
                        }
                        else
                            push(0.0);
                        break;
                    }
                    case 1: { // int
                        if (v.isInt())
                            push(v);
                        else if (v.isDouble())
                            push(static_cast<int64_t>(std::trunc(v.asDouble())));
                        else if (v.isBool())
                            push(v.asBool() ? static_cast<int64_t>(1) : static_cast<int64_t>(0));
                        else if (v.isGcString()) {
                            try {
                                push(static_cast<int64_t>(std::trunc(
                                    std::stod(v.asGcString()->value))));
                            } catch (const std::exception&) {
                                push(static_cast<int64_t>(0));
                            }
                        }
                        else
                            push(static_cast<int64_t>(0));
                        break;
                    }
                    case 2: { // bool
                        push(isTruthy(v));
                        break;
                    }
                    case 3: { // string
                        push(GcHeap::instance().alloc<GcString>(valueToString(v)));
                        break;
                    }
                    default:
                        push(v);  // unknown tag — no conversion
                        break;
                }
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
                markGlobalDirty(slot);
                break;
            }
            case OpCode::OP_DEFINE_GLOBAL_WIDE: {
                uint16_t slot = READ_SHORT();
                if (slot >= globalValues.size()) {
                    RUNTIME_ERROR_OR_THROW("OP_DEFINE_GLOBAL_WIDE: slot out of range");
                }
                globalValues[slot] = pop();
                globalDefined[slot] = true;
                markGlobalDirty(slot);
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
            case OpCode::OP_GET_GLOBAL_WIDE: {
                uint16_t slot = READ_SHORT();
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
            case OpCode::OP_GET_GLOBAL_SAFE_WIDE: {
                uint16_t slot = READ_SHORT();
                uint16_t fallbackIndex = READ_SHORT();
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
                markGlobalDirty(slot);
                break;
            }
            case OpCode::OP_SET_GLOBAL_WIDE: {
                uint16_t slot = READ_SHORT();
                if (slot >= globalValues.size() || !globalDefined[slot]) {
                    RUNTIME_ERROR_OR_THROW("Undefined variable '" +
                        (slot < globalNames.size() ? globalNames[slot] : std::to_string(slot)) + "'");
                }
                globalValues[slot] = peek(0);
                markGlobalDirty(slot);
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
            case OpCode::OP_JUMP_IF_NULL: {
                uint16_t offset = readShort();
                if (pop().isNull()) {
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
            case OpCode::OP_PUSH_SPREAD: {
                // Initialise a new per-call spread counter.
                spreadCountStack_.push_back(0);
                break;
            }
            case OpCode::OP_SPREAD: {
                // Pop an Array from the stack, push each element individually,
                // and accumulate the element count in the current spread counter.
                Value top = pop();
                if (!top.isArray()) {
                    runtimeErrorOrThrow("Spread operand must be an array");
                    return InterpretResult::RUNTIME_ERROR;
                }
                auto& elements = top.asArray()->elements;
                int count = static_cast<int>(elements.size());
                for (auto& elem : elements) {
                    push(elem);
                }
                spreadCountStack_.back() += count;
                break;
            }
            case OpCode::OP_CALL_N: {
                // Call with dynamic argument count from spread expansion.
                // operand = fixed (non-spread) argument count.
                // totalArgs = fixedCount + accumulated spread element count.
                if (GcHeap::instance().needsGC()) {
                    collectGarbage();
                }
                if (spreadCountStack_.empty()) {
                    runtimeErrorOrThrow("OP_CALL_N without matching OP_PUSH_SPREAD");
                    return InterpretResult::RUNTIME_ERROR;
                }
                uint8_t fixedCount = READ_BYTE();
                int spreadCount = spreadCountStack_.back();
                spreadCountStack_.pop_back();
                int totalArgs = fixedCount + spreadCount;
                if (totalArgs > 255) {
                    runtimeErrorOrThrow("Too many arguments: " +
                                        std::to_string(totalArgs));
                    return InterpretResult::RUNTIME_ERROR;
                }
                Value callee = peek(totalArgs);
                if (!callValue(callee, static_cast<uint8_t>(totalArgs))) {
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_CALL_KW: {
                // Named-arg call with 8-bit name indices.
                uint8_t posCount = READ_BYTE();
                uint8_t kwCount = READ_BYTE();
                std::vector<size_t> kwNameIndices(kwCount);
                for (uint8_t i = 0; i < kwCount; i++) {
                    kwNameIndices[i] = READ_BYTE();
                }
                auto result = executeCallKw(posCount, kwCount, kwNameIndices);
                if (result != InterpretResult::OK) {
                    if (exceptionInFlight) goto dispatch;
                    return result;
                }
                break;
            }
            case OpCode::OP_CALL_KW_WIDE: {
                // Named-arg call with 16-bit name indices.
                uint8_t posCount = READ_BYTE();
                uint8_t kwCount = READ_BYTE();
                std::vector<size_t> kwNameIndices(kwCount);
                for (uint8_t i = 0; i < kwCount; i++) {
                    kwNameIndices[i] = READ_SHORT();
                }
                auto result = executeCallKw(posCount, kwCount, kwNameIndices);
                if (result != InterpretResult::OK) {
                    if (exceptionInFlight) goto dispatch;
                    return result;
                }
                break;
            }
            case OpCode::OP_TAIL_CALL: {
                // Tail call optimization: reuse the current call frame
                // instead of pushing a new one. This enables infinite
                // tail recursion without stack overflow.
                //
                // If the callee is a VoraFunction, we replace the current
                // frame with it. Otherwise, we fall back to a regular call
                // followed by a return from the current frame.
                if (GcHeap::instance().needsGC()) {
                    collectGarbage();
                }

                uint8_t argCount = READ_BYTE();
                Value callee = peek(argCount);

                if (!callee.isCallable()) {
                    RUNTIME_ERROR_OR_THROW("Can only call functions");
                }

                auto callable = callee.asCallable();
                auto voraFn = dynamic_cast<VoraFunction*>(callable.get());

                // Only optimize VoraFunction tail calls. For native functions,
                // bound methods, and class constructors, fall back to a regular
                // call + return sequence.
                if (voraFn) {
                    const FunctionPrototype* proto = voraFn->getPrototype();
                    if (!proto) {
                        RUNTIME_ERROR_OR_THROW("Function has no compiled body");
                    }

                    // Check arity
                    if (argCount < static_cast<uint8_t>(voraFn->requiredArity()) ||
                        argCount > static_cast<uint8_t>(voraFn->arity())) {
                        std::string expected;
                        if (voraFn->requiredArity() == voraFn->arity()) {
                            expected = std::to_string(voraFn->arity());
                        } else {
                            expected = std::to_string(voraFn->requiredArity()) + ".." +
                                       std::to_string(voraFn->arity());
                        }
                        RUNTIME_ERROR_OR_THROW("Wrong arity: expected " + expected +
                            " but got " + std::to_string(argCount));
                    }

                    // Collect arguments from the stack
                    std::vector<Value> arguments(argCount);
                    for (int i = argCount - 1; i >= 0; i--) {
                        arguments[static_cast<size_t>(i)] = pop();
                    }
                    pop(); // pop the callee itself

                    // Close open upvalues for the current frame
                    for (auto it = openUpvalues.begin(); it != openUpvalues.end(); ) {
                        if (it->first >= frameBaseIndex &&
                            it->first < frameBaseIndex + MAX_LOCALS_PER_FRAME) {
                            it->second->close();
                            it = openUpvalues.erase(it);
                        } else {
                            ++it;
                        }
                    }

                    // Clean up catch handlers
                    while (!catchHandlers.empty() &&
                           catchHandlers.back().frameCount > frames.size()) {
                        catchHandlers.pop_back();
                    }

                    // Reset exception flag
                    exceptionInFlight = false;

                    // Reuse the current frame
                    if (!frames.empty()) {
                        frames.back().function = GcPtr<VoraFunction>(voraFn);
                    } else {
                        frames.push_back({ip, frameBaseIndex, currentChunk,
                                          GcPtr<VoraFunction>(voraFn)});
                    }

                    // Switch to callee's chunk
                    currentChunk = &proto->chunk;
                    ip = currentChunk->code.data();

                    // Replace locals with new arguments
                    stackTopIndex = frameBaseIndex;
                    currentArgCount = argCount;
                    // Update paramProvidedMask for the reused frame
                    {
                        uint64_t mask = 0;
                        size_t n = std::min(static_cast<size_t>(argCount),
                                           static_cast<size_t>(voraFn->arity()));
                        for (size_t i = 0; i < n; i++) mask |= (1ULL << i);
                        if (!frames.empty()) frames.back().paramProvidedMask = mask;
                    }
                    for (size_t i = 0; i < static_cast<size_t>(voraFn->arity()); i++) {
                        if (i < arguments.size()) {
                            push(arguments[i]);
                        } else {
                            push(nullptr);
                        }
                    }
                } else if (auto bound = dynamic_cast<BoundMethod*>(callable.get())) {
                    // BoundMethod tail call: reuse the current frame, just like
                    // the VoraFunction path above. This avoids the broken
                    // callValue() fallback which switches execution context
                    // (new frame, chunk, ip) before the pop/restore code runs.
                    const FunctionPrototype* proto = bound->getMethodProto();
                    if (!proto) {
                        RUNTIME_ERROR_OR_THROW("Bound method has no prototype");
                    }

                    int arity = proto->arity;
                    int requiredArity = proto->requiredArity;

                    // Check arity
                    if (argCount < static_cast<uint8_t>(requiredArity) ||
                        argCount > static_cast<uint8_t>(arity)) {
                        std::string expected;
                        if (requiredArity == arity) {
                            expected = std::to_string(arity);
                        } else {
                            expected = std::to_string(requiredArity) + ".." +
                                       std::to_string(arity);
                        }
                        RUNTIME_ERROR_OR_THROW("Wrong arity: expected " + expected +
                            " but got " + std::to_string(argCount));
                    }

                    // Collect arguments from the stack
                    std::vector<Value> arguments(argCount);
                    for (int i = argCount - 1; i >= 0; i--) {
                        arguments[static_cast<size_t>(i)] = pop();
                    }
                    pop(); // pop the callee (bound method) itself

                    // Close open upvalues for the current frame
                    for (auto it = openUpvalues.begin(); it != openUpvalues.end(); ) {
                        if (it->first >= frameBaseIndex &&
                            it->first < frameBaseIndex + MAX_LOCALS_PER_FRAME) {
                            it->second->close();
                            it = openUpvalues.erase(it);
                        } else {
                            ++it;
                        }
                    }

                    // Clean up catch handlers
                    while (!catchHandlers.empty() &&
                           catchHandlers.back().frameCount > frames.size()) {
                        catchHandlers.pop_back();
                    }

                    // Reset exception flag
                    exceptionInFlight = false;

                    // Reuse the current frame
                    if (!frames.empty()) {
                        frames.back().function = bound->getMethodFunc();
                    } else {
                        frames.push_back({ip, frameBaseIndex, currentChunk,
                                          bound->getMethodFunc()});
                    }

                    // Switch to method's chunk
                    currentChunk = &proto->chunk;
                    ip = currentChunk->code.data();

                    // Replace locals: push 'this' + arguments
                    stackTopIndex = frameBaseIndex;
                    currentArgCount = argCount;
                    // Update paramProvidedMask for the reused frame
                    {
                        uint64_t mask = 0;
                        size_t n = std::min(static_cast<size_t>(argCount),
                                           static_cast<size_t>(arity));
                        for (size_t i = 0; i < n; i++) mask |= (1ULL << i);
                        if (!frames.empty()) frames.back().paramProvidedMask = mask;
                    }

                    // Push 'this' (bound instance) as slot 0
                    push(bound->getInstance());

                    // Push method arguments (up to arity)
                    for (size_t i = 0; i < static_cast<size_t>(arity); i++) {
                        if (i < arguments.size()) {
                            push(arguments[i]);
                        } else {
                            push(nullptr);
                        }
                    }

                    // Push rest parameter if method has one
                    if (proto->hasRest) {
                        auto restArray = GcHeap::instance().alloc<Array>();
                        for (size_t i = arity; i < arguments.size(); i++) {
                            restArray->elements.push_back(arguments[i]);
                        }
                        push(Value(GcPtr<Array>(restArray)));
                    }
                } else {
                    // Non-Vora callee (NativeFunction / ClassConstructor):
                    // fall back to regular call. callValue pops callee+args
                    // and pushes the result, without switching execution context.
                    if (!callValue(callee, argCount)) {
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    // Now the result is on the stack. Return from current frame.
                    Value returnValue = pop();
                    stackTopIndex = frameBaseIndex;

                    // Close open upvalues for this frame
                    for (auto it = openUpvalues.begin(); it != openUpvalues.end(); ) {
                        if (it->first >= frameBaseIndex &&
                            it->first < frameBaseIndex + MAX_LOCALS_PER_FRAME) {
                            it->second->close();
                            it = openUpvalues.erase(it);
                        } else {
                            ++it;
                        }
                    }

                    // Restore caller's frame
                    if (frames.empty()) {
                        push(returnValue);
                        // Top-level: nothing to return to — just push and continue?
                        // This shouldn't happen; emit OP_RETURN would've been used.
                        break;
                    }
                    CallFrame frame = frames.back();
                    frames.pop_back();
                    ip = frame.returnIp;
                    frameBaseIndex = frame.frameBase;
                    currentChunk = frame.callerChunk;

                    // Clean up catch handlers
                    while (!catchHandlers.empty() &&
                           catchHandlers.back().frameCount > frames.size()) {
                        catchHandlers.pop_back();
                    }

                    push(returnValue);
                }

                break;
            }
            case OpCode::OP_CLOSURE: {
                uint8_t constIndex = READ_BYTE();
                Value protoVal = currentChunk->constants[constIndex];
                if (!protoVal.isFunctionPrototype()) {
                    RUNTIME_ERROR_OR_THROW("OP_CLOSURE: expected function prototype in constant pool");
                }
                auto protoPtr = protoVal.asFunctionPrototype();
                auto function = GcHeap::instance().alloc<VoraFunction>(
                    protoPtr->name, protoPtr->arity,
                    protoPtr->requiredArity, protoPtr->hasRest, protoPtr.get());

                // Read upvalue descriptors and capture upvalues
                uint8_t upvalueCount = READ_BYTE();
                for (uint8_t u = 0; u < upvalueCount; u++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        // Capture as open upvalue — location points to
                        // the live stack slot so mutations and self-
                        // references work correctly.
                        function->upvalues.push_back(
                            captureUpvalue(frameBaseIndex + index));
                    } else {
                        // Capture upvalue from enclosing function
                        if (!frames.empty()) {
                            auto& encFn = frames.back().function;
                            if (encFn && index < encFn->upvalues.size()) {
                                function->upvalues.push_back(encFn->upvalues[index]);
                            } else {
                                auto fallback = std::make_shared<Upvalue>();
                                fallback->isClosed = true;
                                fallback->closed = nullptr;
                                function->upvalues.push_back(fallback);
                            }
                        } else {
                            auto fallback = std::make_shared<Upvalue>();
                            fallback->isClosed = true;
                            fallback->closed = nullptr;
                            function->upvalues.push_back(fallback);
                        }
                    }
                }

                push(function);
                break;
            }
            case OpCode::OP_CLOSURE_WIDE: {
                uint16_t constIndex = READ_SHORT();
                Value protoVal = currentChunk->constants[constIndex];
                if (!protoVal.isFunctionPrototype()) {
                    RUNTIME_ERROR_OR_THROW("OP_CLOSURE_WIDE: expected function prototype");
                }
                auto protoPtr = protoVal.asFunctionPrototype();
                auto function = GcHeap::instance().alloc<VoraFunction>(
                    protoPtr->name, protoPtr->arity,
                    protoPtr->requiredArity, protoPtr->hasRest, protoPtr.get());
                uint8_t upvalueCount = READ_BYTE();
                for (uint8_t u = 0; u < upvalueCount; u++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        function->upvalues.push_back(captureUpvalue(frameBaseIndex + index));
                    } else {
                        if (!frames.empty()) {
                            auto& encFn = frames.back().function;
                            if (encFn && index < encFn->upvalues.size()) {
                                function->upvalues.push_back(encFn->upvalues[index]);
                            } else {
                                auto fb = std::make_shared<Upvalue>();
                                fb->isClosed = true; fb->closed = nullptr;
                                function->upvalues.push_back(fb);
                            }
                        } else {
                            auto fb = std::make_shared<Upvalue>();
                            fb->isClosed = true; fb->closed = nullptr;
                            function->upvalues.push_back(fb);
                        }
                    }
                }
                push(function);
                break;
            }
            case OpCode::OP_YIELD: {
                // Yield from generator: save state, return to caller.
                // The yielded value is on top of stack.
                if (!currentGenerator) {
                    RUNTIME_ERROR_OR_THROW("'yield' used outside of generator");
                }

                Value yieldValue = pop();

                // Skip over dead OP_POP/OP_POPN emitted by ExprStmt wrapper.
                // yield is parsed as an expression; when used as a statement,
                // the compiler wraps it with OP_POP to discard the result.
                // Since OP_YIELD already handles the value and switches context,
                // this OP_POP is dead code that we must skip on resume.
                if (*ip == static_cast<uint8_t>(OpCode::OP_POP)) {
                    ip++;
                } else if (*ip == static_cast<uint8_t>(OpCode::OP_POPN)) {
                    ip += 2;  // OP_POPN + operand byte
                }

                // Save current IP offset (after dead-code skip)
                currentGenerator->savedIpOffset =
                    static_cast<size_t>(ip - currentChunk->code.data());
                currentGenerator->savedFrameBase = frameBaseIndex;

                // Snapshot all locals from frame base to stack top
                currentGenerator->savedStack.clear();
                for (size_t i = frameBaseIndex; i < stackTopIndex; i++) {
                    currentGenerator->savedStack.push_back(stack[i]);
                }

                // Pop all locals
                stackTopIndex = frameBaseIndex;

                // Close open upvalues for this frame (prevent stale stack refs)
                for (auto it = openUpvalues.begin(); it != openUpvalues.end(); ) {
                    if (it->first >= frameBaseIndex &&
                        it->first < frameBaseIndex + MAX_LOCALS_PER_FRAME) {
                        it->second->close();
                        it = openUpvalues.erase(it);
                    } else {
                        ++it;
                    }
                }

                // Pop the generator's call frame
                frames.pop_back();

                // Restore caller's execution context (saved by next())
                ip = currentGenerator->callerIp;
                frameBaseIndex = currentGenerator->callerFrameBase;
                currentChunk = currentGenerator->callerChunk;

                // Discard any frames pushed inside the generator
                while (frames.size() > currentGenerator->callerFramesSize) {
                    frames.pop_back();
                }

                // Clean up catch handlers registered inside the generator
                while (!catchHandlers.empty() &&
                       catchHandlers.back().frameCount > frames.size()) {
                    catchHandlers.pop_back();
                }

                // Push yield value as the return value of next()
                push(yieldValue);

                currentGenerator = nullptr;
                break;
            }
            case OpCode::OP_AWAIT: {
                // Await expression: suspend until the awaited Task resolves.
                // The awaited value is on top of stack.
                Value awaited = pop();

                // If not a Task, wrap as an immediately fulfilled Task
                if (!awaited.isTask()) {
                    // Non-Task values resolve immediately: push and continue
                    push(awaited);
                    break;
                }

                auto awaitedTask = awaited.asTask();

                // If already fulfilled, push result and continue
                if (awaitedTask->state == Task::Fulfilled) {
                    push(awaitedTask->result);
                    break;
                }

                // If rejected, throw the error
                if (awaitedTask->state == Task::Rejected) {
                    if (!runtimeErrorOrThrow("Unhandled async rejection")) {
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    goto dispatch;
                }

                // Task is pending: save state and return to event loop
                if (!currentGenerator) {
                    RUNTIME_ERROR_OR_THROW("'await' used outside of async function");
                    break;
                }

                // Register as awaiter so we get resumed when the task completes
                if (currentGenerator->task) {
                    awaitedTask->awaiters.push_back(currentGenerator->task);
                }

                // Skip dead OP_POP/OP_POPN (same as OP_YIELD)
                if (*ip == static_cast<uint8_t>(OpCode::OP_POP)) {
                    ip++;
                } else if (*ip == static_cast<uint8_t>(OpCode::OP_POPN)) {
                    ip += 2;
                }

                // Save current IP and state (same as OP_YIELD)
                currentGenerator->savedIpOffset =
                    static_cast<size_t>(ip - currentChunk->code.data());
                currentGenerator->savedFrameBase = frameBaseIndex;

                currentGenerator->savedStack.clear();
                for (size_t i = frameBaseIndex; i < stackTopIndex; i++) {
                    currentGenerator->savedStack.push_back(stack[i]);
                }

                stackTopIndex = frameBaseIndex;

                // Close open upvalues for this frame
                for (auto it = openUpvalues.begin(); it != openUpvalues.end(); ) {
                    if (it->first >= frameBaseIndex &&
                        it->first < frameBaseIndex + MAX_LOCALS_PER_FRAME) {
                        it->second->close();
                        it = openUpvalues.erase(it);
                    } else {
                        ++it;
                    }
                }

                // Register current task as an awaiter on the awaited task
                if (currentGenerator->task) {
                    awaitedTask->awaiters.push_back(currentGenerator->task);
                }

                // Pop the generator's call frame
                frames.pop_back();

                // Restore caller's execution context
                ip = currentGenerator->callerIp;
                frameBaseIndex = currentGenerator->callerFrameBase;
                currentChunk = currentGenerator->callerChunk;

                while (frames.size() > currentGenerator->callerFramesSize) {
                    frames.pop_back();
                }

                while (!catchHandlers.empty() &&
                       catchHandlers.back().frameCount > frames.size()) {
                    catchHandlers.pop_back();
                }

                // Push null as placeholder — event loop will resume when task completes
                push(nullptr);

                currentGenerator = nullptr;
                break;
            }
            case OpCode::OP_RETURN: {
                // Return from current function or top-level
                if (frames.empty()) {
                    return InterpretResult::OK;
                }

                // Generator return
                if (currentGenerator) {
                    Value returnValue = pop();

                    stackTopIndex = frameBaseIndex;

                    for (auto it = openUpvalues.begin(); it != openUpvalues.end(); ) {
                        if (it->first >= frameBaseIndex &&
                            it->first < frameBaseIndex + MAX_LOCALS_PER_FRAME) {
                            it->second->close();
                            it = openUpvalues.erase(it);
                        } else { ++it; }
                    }

                    currentGenerator->done = true;

                    // Check if async before clearing currentGenerator
                    bool isAsyncGen = currentGenerator->task != nullptr;

                    // Async generator: fulfill the Task instead of throwing StopIteration
                    if (isAsyncGen) {
                        auto task = currentGenerator->task;
                        task->result = returnValue;
                        task->state = Task::Fulfilled;

                        // Resume awaiters: set the first awaiter as pendingResume
                        if (!task->awaiters.empty()) {
                            auto awaiterTask = task->awaiters[0];
                            auto awaiterGen = awaiterTask->generator;
                            // Save current context into awaiter's generator (as the "caller")
                            awaiterGen->callerIp = currentGenerator->callerIp;
                            awaiterGen->callerChunk = currentGenerator->callerChunk;
                            awaiterGen->callerFrameBase = currentGenerator->callerFrameBase;
                            awaiterGen->callerFramesSize = currentGenerator->callerFramesSize;
                            // Set up pendingResume for the awaiter
                            const FunctionPrototype* awaiterProto = awaiterGen->function->getPrototype();
                            pendingIp = awaiterProto->chunk.code.data() + awaiterGen->savedIpOffset;
                            pendingChunk = &awaiterProto->chunk;
                            pendingGenerator = awaiterGen;
                            pendingResume = true;
                        }
                    }

                    // Pop generator frame, restore caller
                    CallFrame genFrame = frames.back();
                    frames.pop_back();
                    ip = currentGenerator->callerIp;
                    frameBaseIndex = currentGenerator->callerFrameBase;
                    currentChunk = currentGenerator->callerChunk;

                    while (frames.size() > currentGenerator->callerFramesSize) {
                        frames.pop_back();
                    }

                    while (!catchHandlers.empty() &&
                           catchHandlers.back().frameCount > frames.size()) {
                        catchHandlers.pop_back();
                    }

                    // Restore currentGenerator from parent frame (if any)
                    currentGenerator = frames.empty() ? nullptr : frames.back().generator;

                    if (isAsyncGen) {
                        // Async task completes normally.
                        // The Task is already on the caller's stack (pushed by callVoraFunction).
                        // Don't push returnValue — it's stored in task->result for await to read.
                    } else {
                        // Regular generator: throw StopIteration (catchable by try/catch)
                        if (throwException(
                                GcHeap::instance().alloc<GcString>("StopIteration"))) {
                            goto dispatch;
                        }
                        runtimeError("Uncaught StopIteration");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    break;
                }

                // Get return value (top of stack)
                Value returnValue = pop();

                // Pop all locals by restoring stack to frame base
                stackTopIndex = frameBaseIndex;

                // Close open upvalues for this frame (locals going out of scope).
                // Must close BEFORE erasing — existing closures hold shared_ptr<Upvalue>
                // with location pointing to our stack. Closing migrates the value
                // to heap storage so closures don't see garbage after stack reuse.
                for (auto it = openUpvalues.begin(); it != openUpvalues.end(); ) {
                    if (it->first >= frameBaseIndex && it->first < frameBaseIndex + MAX_LOCALS_PER_FRAME) {
                        it->second->close();
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
                // Check per-slot mask first (set by OP_CALL_KW and callVoraFunction).
                // Fall back to currentArgCount for paths that don't set the mask
                // (e.g. ClassConstructor inline execution, top-level code).
                bool provided = false;
                if (!frames.empty() && frames.back().paramProvidedMask != 0) {
                    provided = (frames.back().paramProvidedMask & (1ULL << slot)) != 0;
                } else {
                    provided = (slot < currentArgCount);
                }
                if (provided) {
                    ip += skipOffset;
                }
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
                    push(fn->upvalues[idx]->get());
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
                    fn->upvalues[idx]->set(peek(0));
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
                    // Close the upvalue: copy stack value to heap storage
                    // and redirect location to the heap copy. Closures that
                    // have already captured this upvalue will now read from
                    // the heap copy, preserving the value after the stack
                    // slot is reused.
                    it->second->close();
                    openUpvalues.erase(it);
                }
                break;
            }

            // --- Defer ---
            case OpCode::OP_DEFER_PUSH: {
                Value closure = pop();
                frames.back().deferStack.push_back(std::move(closure));
                break;
            }

            case OpCode::OP_DEFER_FLUSH: {
                flushDeferStack();
                if (exceptionInFlight) goto dispatch;
                break;
            }

            // --- Arrays ---
            case OpCode::OP_ARRAY: {
                uint8_t count = READ_BYTE();
                if (count > stackTopIndex) {
                    RUNTIME_ERROR_OR_THROW("Not enough values on stack for array literal");
                    return InterpretResult::RUNTIME_ERROR;
                }
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
                size_t totalSlots = static_cast<size_t>(pairCount) * 2;
                if (totalSlots > stackTopIndex) {
                    RUNTIME_ERROR_OR_THROW("Not enough values on stack for dict literal");
                    return InterpretResult::RUNTIME_ERROR;
                }
                auto dict = GcHeap::instance().alloc<Dict>();
                size_t startIdx = stackTopIndex - totalSlots;
                for (uint8_t i = 0; i < pairCount; i++) {
                    std::string key = valueToString(stack[startIdx + i * 2]);
                    dict->pairs[key] = stack[startIdx + i * 2 + 1];
                }
                stackTopIndex -= totalSlots;
                push(dict);
                break;
            }
            case OpCode::OP_INDEX: {
                Value indexVal = pop();
                Value target = pop();

                // Dict: string key lookup, or numeric index access.
                // NOTE: Numeric index access via std::advance is O(n) for
                // unordered_map. The compiler-generated for-in loop uses
                // Iterator snapshots (O(1) per step) — this path is for
                // manual dict[i] indexing by user code.
                if (target.isDict()) {
                    const auto& dpairs =
                        target.asDict()->pairs;
                    if (isNumeric(indexVal)) {
                        // Numeric index: return Nth key
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

                // Map: key lookup by Value (arbitrary-type keys)
                if (target.isMap()) {
                    const auto& mpairs = target.asMap()->pairs;
                    if (isNumeric(indexVal)) {
                        double raw = toDouble(indexVal);
                        // If it's a clean non-negative integer, treat as positional
                        // Nth key access for for-in compatibility
                        if (raw >= 0 && std::floor(raw) == raw) {
                            size_t idx = static_cast<size_t>(raw);
                            if (idx >= mpairs.size()) {
                                RUNTIME_ERROR_OR_THROW("Index out of bounds");
                            }
                            auto it = mpairs.begin();
                            std::advance(it, idx);
                            push(it->first);
                            break;
                        }
                    }
                    // General key lookup
                    auto it = mpairs.find(indexVal);
                    if (it == mpairs.end()) {
                        RUNTIME_ERROR_OR_THROW("Key not found");
                    }
                    push(it->second);
                    break;
                }

                // Set: numeric index for for-in iteration (positional access)
                if (target.isSet()) {
                    if (!isNumeric(indexVal)) {
                        RUNTIME_ERROR_OR_THROW("Set index must be a number");
                    }
                    double rawIndex = toDouble(indexVal);
                    if (rawIndex < 0 || std::floor(rawIndex) != rawIndex) {
                        RUNTIME_ERROR_OR_THROW("Index must be a non-negative integer");
                    }
                    const auto& selements = target.asSet()->elements;
                    size_t idx = static_cast<size_t>(rawIndex);
                    if (idx >= selements.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    auto it = selements.begin();
                    std::advance(it, idx);
                    push(*it);
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

                if (target.isGcString()) {
                    const auto& str = target.asGcString()->value;
                    if (index >= str.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    push(GcHeap::instance().alloc<GcString>(std::string(1, str[index])));
                } else if (target.isArray()) {
                    const auto& elements =
                        target.asArray()->elements;
                    if (index >= elements.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    push(elements[index]);
                } else if (target.isObjectInstance()) {
                    const auto& props =
                        target.asObjectInstance()->properties;
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
            // --- Null-safe index access (for ?.[] optional chaining) ---
            case OpCode::OP_INDEX_SAFE: {
                Value indexVal = pop();
                Value target = pop();

                // Dict: string-keyed lookup (handled first since keys are non-numeric)
                if (target.isDict()) {
                    const auto& dpairs = target.asDict()->pairs;
                    if (isNumeric(indexVal)) {
                        double raw = toDouble(indexVal);
                        if (raw >= 0 && std::floor(raw) == raw) {
                            size_t idx = static_cast<size_t>(raw);
                            if (idx >= dpairs.size()) {
                                push(nullptr);  // safe: return null on bounds
                            } else {
                                auto it = dpairs.begin();
                                std::advance(it, idx);
                                push(GcHeap::instance().alloc<GcString>(it->first));
                            }
                        } else {
                            std::string key = valueToString(indexVal);
                            auto it = dpairs.find(key);
                            if (it == dpairs.end()) {
                                push(nullptr);  // safe: return null
                            } else {
                                push(it->second);
                            }
                        }
                    } else {
                        std::string key = valueToString(indexVal);
                        auto it = dpairs.find(key);
                        if (it == dpairs.end()) {
                            push(nullptr);  // safe: return null
                        } else {
                            push(it->second);
                        }
                    }
                    break;
                }

                // Map: key lookup by Value
                if (target.isMap()) {
                    const auto& mpairs = target.asMap()->pairs;
                    if (isNumeric(indexVal)) {
                        double raw = toDouble(indexVal);
                        if (raw >= 0 && std::floor(raw) == raw) {
                            size_t idx = static_cast<size_t>(raw);
                            if (idx >= mpairs.size()) {
                                push(nullptr);  // safe: return null on bounds
                            } else {
                                auto it = mpairs.begin();
                                std::advance(it, idx);
                                push(it->first);
                            }
                            break;
                        }
                    }
                    auto it = mpairs.find(indexVal);
                    if (it == mpairs.end()) {
                        push(nullptr);  // safe: return null
                    } else {
                        push(it->second);
                    }
                    break;
                }

                // Set: numeric index for for-in iteration
                if (target.isSet()) {
                    if (!isNumeric(indexVal)) {
                        push(nullptr);  // safe: non-numeric index on set
                        break;
                    }
                    double rawIndex = toDouble(indexVal);
                    if (rawIndex < 0 || std::floor(rawIndex) != rawIndex) {
                        push(nullptr);  // safe: invalid index
                        break;
                    }
                    const auto& selements = target.asSet()->elements;
                    size_t idx = static_cast<size_t>(rawIndex);
                    if (idx >= selements.size()) {
                        push(nullptr);  // safe: return null on bounds
                    } else {
                        auto it = selements.begin();
                        std::advance(it, idx);
                        push(*it);
                    }
                    break;
                }

                if (!isNumeric(indexVal)) {
                    push(nullptr);  // safe: non-numeric index on non-dict/map/set
                    break;
                }

                double rawIndex = toDouble(indexVal);
                if (rawIndex < 0 || std::floor(rawIndex) != rawIndex) {
                    push(nullptr);  // safe: invalid index
                    break;
                }

                size_t index = static_cast<size_t>(rawIndex);

                if (target.isGcString()) {
                    const auto& str = target.asGcString()->value;
                    if (index >= str.size()) {
                        push(nullptr);  // safe: return null on bounds
                    } else {
                        push(GcHeap::instance().alloc<GcString>(std::string(1, str[index])));
                    }
                } else if (target.isArray()) {
                    const auto& elements =
                        target.asArray()->elements;
                    if (index >= elements.size()) {
                        push(nullptr);  // safe: return null on bounds
                    } else {
                        push(elements[index]);
                    }
                } else if (target.isObjectInstance()) {
                    const auto& props =
                        target.asObjectInstance()->properties;
                    if (index >= props.size()) {
                        push(nullptr);  // safe: return null on bounds
                    } else {
                        auto it = props.begin();
                        std::advance(it, index);
                        push(GcHeap::instance().alloc<GcString>(it->first));
                    }
                } else {
                    push(nullptr);  // safe: return null for non-indexable types
                }
                break;
            }
            case OpCode::OP_SET_INDEX: {
                Value value = pop();
                Value indexVal = pop();
                Value target = pop();

                // Dict: string-keyed assignment (handled first since keys are non-numeric)
                if (target.isDict()) {
                    std::string key = valueToString(indexVal);
                    auto dict = target.asDict();
                    dict->pairs[key] = value;
                    writeBarrier(dict.get(), value);  // generational GC
                    push(value);
                    break;
                }

                // Map: arbitrary-key assignment
                if (target.isMap()) {
                    auto map = target.asMap();
                    map->pairs[indexVal] = value;
                    writeBarrier(map.get(), value);  // generational GC
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

                if (target.isArray()) {
                    auto arr = target.asArray();
                    if (index >= arr->elements.size()) {
                        RUNTIME_ERROR_OR_THROW("Index out of bounds");
                    }
                    arr->elements[index] = value;
                    writeBarrier(arr.get(), value);  // generational GC
                    push(value);
                } else if (target.isGcString()) {
                    RUNTIME_ERROR_OR_THROW("Cannot assign to string index (strings are immutable)");
                } else {
                    RUNTIME_ERROR_OR_THROW("Index assignment requires an array");
                }
                break;
            }

            // --- Objects ---

            // --- Superinstructions: fused GET_LOCAL/GET_GLOBAL + GET_PROPERTY ---
            case OpCode::OP_GET_LOCAL_PROP: {
                uint8_t localSlot = READ_BYTE();
                nameIndex = READ_BYTE();
                obj = stack[frameBaseIndex + localSlot];
                goto property_dispatch;
            }
            case OpCode::OP_GET_LOCAL_PROP_WIDE: {
                uint8_t localSlot = READ_BYTE();
                nameIndex = READ_SHORT();
                obj = stack[frameBaseIndex + localSlot];
                goto property_dispatch;
            }
            case OpCode::OP_GET_GLOBAL_PROP: {
                uint8_t globalSlot = READ_BYTE();
                nameIndex = READ_BYTE();
                obj = globalValues[globalSlot];
                goto property_dispatch;
            }
            case OpCode::OP_GET_GLOBAL_PROP_WIDE: {
                uint16_t globalSlot = READ_SHORT();
                nameIndex = READ_BYTE();
                obj = globalValues[globalSlot];
                goto property_dispatch;
            }

            case OpCode::OP_GET_PROPERTY_WIDE:
                nameIndex = READ_SHORT();
                goto get_property_body;
            case OpCode::OP_GET_PROPERTY:
                nameIndex = READ_BYTE();
                get_property_body:
                obj = pop();
                // fall through to shared property dispatch
                property_dispatch: {
                if (!currentChunk->constants[nameIndex].isGcString()) {
                    RUNTIME_ERROR_OR_THROW("Invalid property name in constant pool");
                    return InterpretResult::RUNTIME_ERROR;
                }
                std::string propName = currentChunk->constants[nameIndex].asGcString()->value;

                // Array built-in methods & properties
                if (obj.isArray()) {
                    auto arr = obj.asArray();
                    if (propName == "length") {
                        push(static_cast<int64_t>(arr->elements.size()));
                        break;
                    }
                    auto method = getArrayMethod(propName, arr);
                    if (method) { push(method); break; }
                    RUNTIME_ERROR_OR_THROW("Unknown array method: " + propName);
                }

                // String built-in methods & properties
                if (obj.isGcString()) {
                    const auto& str = obj.asGcString()->value;
                    if (propName == "length") {
                        push(static_cast<int64_t>(str.size()));
                        break;
                    }
                    auto method = getStringMethod(propName, str);
                    if (method) { push(method); break; }
                    RUNTIME_ERROR_OR_THROW("Unknown string method: " + propName);
                }

                // Dict built-in methods & properties
                if (obj.isDict()) {
                    auto dict = obj.asDict();
                    if (propName == "length") {
                        push(static_cast<int64_t>(dict->pairs.size()));
                        break;
                    }
                    auto method = getDictMethod(propName, dict);
                    if (method) { push(method); break; }
                    // Fall back to key lookup (for module objects, etc.)
                    auto keyIt = dict->pairs.find(propName);
                    if (keyIt != dict->pairs.end()) {
                        push(keyIt->second);
                        break;
                    }
                    RUNTIME_ERROR_OR_THROW("Unknown dict key or method: " + propName);
                }

                // Set built-in methods & properties
                if (obj.isSet()) {
                    auto set = obj.asSet();
                    if (propName == "length" || propName == "size") {
                        push(static_cast<int64_t>(set->elements.size()));
                        break;
                    }
                    auto method = getSetMethod(propName, set);
                    if (method) { push(method); break; }
                    RUNTIME_ERROR_OR_THROW("Unknown set method: " + propName);
                }

                // Map built-in methods & properties
                if (obj.isMap()) {
                    auto map = obj.asMap();
                    if (propName == "length" || propName == "size") {
                        push(static_cast<int64_t>(map->pairs.size()));
                        break;
                    }
                    auto method = getMapMethod(propName, map);
                    if (method) { push(method); break; }
                    RUNTIME_ERROR_OR_THROW("Unknown map method: " + propName);
                }

                if (obj.isObjectInstance()) {
                    auto instance = obj.asObjectInstance();
                    // Check instance properties
                    auto it = instance->properties.find(propName);
                    if (it != instance->properties.end()) {
                        push(it->second);
                        break;
                    }
                    // Check class methods via MRO (C3 linearization)
                    bool methodFound = false;
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
                                methodFound = true;
                                break;
                            }
                        }
                    }
                    if (methodFound) break;
                    // Fall back to static methods on the class
                    if (instance->classDefinition) {
                        auto smIt = instance->classDefinition->staticMethods.find(propName);
                        if (smIt != instance->classDefinition->staticMethods.end()) {
                            push(smIt->second);  // static method, no binding needed
                            break;
                        }
                    }
                    RUNTIME_ERROR_OR_THROW("Undefined property: " + propName);
                } else if (obj.isCallable()) { auto callable = obj.asCallable();
                    // Class constructor — check for static methods
                    if (callable) {
                        auto classDef = callable->getClassDef();
                        if (classDef) {
                            auto smIt = classDef->staticMethods.find(propName);
                            if (smIt != classDef->staticMethods.end()) {
                                push(smIt->second);  // static method, no binding
                                break;
                            }
                        }
                    }
                    RUNTIME_ERROR_OR_THROW("Unknown static method: " + propName);
                } else {
                    RUNTIME_ERROR_OR_THROW("Can only access properties on objects");
                }
                break;
            }  // property_dispatch block
            // --- Null-safe property access (for ?. optional chaining) ---
            case OpCode::OP_GET_PROPERTY_SAFE_WIDE:
                nameIndex = READ_SHORT();
                goto get_property_safe_body;
            case OpCode::OP_GET_PROPERTY_SAFE:
                nameIndex = READ_BYTE();
                get_property_safe_body: {
                if (!currentChunk->constants[nameIndex].isGcString()) {
                    RUNTIME_ERROR_OR_THROW("Invalid property name in constant pool");
                    return InterpretResult::RUNTIME_ERROR;
                }
                std::string propName = currentChunk->constants[nameIndex].asGcString()->value;
                Value obj = pop();

                // Array built-in methods & properties
                if (obj.isArray()) {
                    auto arr = obj.asArray();
                    if (propName == "length") {
                        push(static_cast<int64_t>(arr->elements.size()));
                        break;
                    }
                    auto method = getArrayMethod(propName, arr);
                    if (method) { push(method); break; }
                    push(nullptr);  // safe: return null instead of throwing
                    break;
                }

                // String built-in methods & properties
                if (obj.isGcString()) {
                    const auto& str = obj.asGcString()->value;
                    if (propName == "length") {
                        push(static_cast<int64_t>(str.size()));
                        break;
                    }
                    auto method = getStringMethod(propName, str);
                    if (method) { push(method); break; }
                    push(nullptr);  // safe: return null instead of throwing
                    break;
                }

                // Dict built-in methods & properties
                if (obj.isDict()) {
                    auto dict = obj.asDict();
                    if (propName == "length") {
                        push(static_cast<int64_t>(dict->pairs.size()));
                        break;
                    }
                    auto method = getDictMethod(propName, dict);
                    if (method) { push(method); break; }
                    auto keyIt = dict->pairs.find(propName);
                    if (keyIt != dict->pairs.end()) {
                        push(keyIt->second);
                        break;
                    }
                    push(nullptr);  // safe: return null for unknown key
                    break;
                }

                // Set built-in methods & properties
                if (obj.isSet()) {
                    auto set = obj.asSet();
                    if (propName == "length" || propName == "size") {
                        push(static_cast<int64_t>(set->elements.size()));
                        break;
                    }
                    auto method = getSetMethod(propName, set);
                    if (method) { push(method); break; }
                    push(nullptr);  // safe: return null instead of throwing
                    break;
                }

                // Map built-in methods & properties
                if (obj.isMap()) {
                    auto map = obj.asMap();
                    if (propName == "length" || propName == "size") {
                        push(static_cast<int64_t>(map->pairs.size()));
                        break;
                    }
                    auto method = getMapMethod(propName, map);
                    if (method) { push(method); break; }
                    push(nullptr);  // safe: return null instead of throwing
                    break;
                }

                if (obj.isObjectInstance()) {
                    auto instance = obj.asObjectInstance();
                    auto it = instance->properties.find(propName);
                    if (it != instance->properties.end()) {
                        push(it->second);
                        break;
                    }
                    bool safeMethodFound = false;
                    if (instance->classDefinition) {
                        for (const auto& wp : instance->classDefinition->mro) {
                            auto cls = wp;
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
                                safeMethodFound = true;
                                break;
                            }
                        }
                    }
                    if (safeMethodFound) break;
                    // Fall back to static methods on the class
                    if (instance->classDefinition) {
                        auto smIt = instance->classDefinition->staticMethods.find(propName);
                        if (smIt != instance->classDefinition->staticMethods.end()) {
                            push(smIt->second);  // static method
                            break;
                        }
                    }
                    push(nullptr);  // safe: return null for undefined property
                    break;
                } else if (obj.isCallable()) { auto callable = obj.asCallable();
                    // Class constructor — check for static methods
                    if (callable) {
                        auto classDef = callable->getClassDef();
                        if (classDef) {
                            auto smIt = classDef->staticMethods.find(propName);
                            if (smIt != classDef->staticMethods.end()) {
                                push(smIt->second);  // static method
                                break;
                            }
                        }
                    }
                    push(nullptr);  // safe: return null for unknown static method
                    break;
                } else {
                    push(nullptr);  // safe: return null for non-object types
                }
                break;
            }
            case OpCode::OP_SET_PROPERTY_WIDE:
                nameIndex = READ_SHORT();
                goto set_property_body;
            case OpCode::OP_SET_PROPERTY:
                nameIndex = READ_BYTE();
                set_property_body: {
                if (!currentChunk->constants[nameIndex].isGcString()) {
                    RUNTIME_ERROR_OR_THROW("Invalid property name in constant pool");
                    return InterpretResult::RUNTIME_ERROR;
                }
                std::string propName = currentChunk->constants[nameIndex].asGcString()->value;
                Value value = pop();
                Value obj = pop();

                if (obj.isObjectInstance()) {
                    auto instance = obj.asObjectInstance();
                    instance->properties[propName] = value;
                    writeBarrier(instance.get(), value);  // generational GC
                    push(value);
                } else {
                    RUNTIME_ERROR_OR_THROW("Can only set properties on objects");
                }
                break;
            }
            case OpCode::OP_GET_SUPER_WIDE:
                nameIndex = READ_SHORT();
                goto get_super_body;
            case OpCode::OP_GET_SUPER:
                nameIndex = READ_BYTE();
                get_super_body: {
                if (!currentChunk->constants[nameIndex].isGcString()) {
                    RUNTIME_ERROR_OR_THROW("Invalid property name in constant pool");
                    return InterpretResult::RUNTIME_ERROR;
                }
                std::string propName = currentChunk->constants[nameIndex].asGcString()->value;

                // Get 'this' from local slot 0 (must be in a method)
                Value thisVal = stack[frameBaseIndex];
                if (!thisVal.isObjectInstance()) {
                    RUNTIME_ERROR_OR_THROW("'super' used outside of method");
                }
                auto instance = thisVal.asObjectInstance();

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
            uint16_t classIndex;
            case OpCode::OP_CLASS_WIDE:
                classIndex = READ_SHORT();
                goto class_body;
            case OpCode::OP_CLASS:
                classIndex = READ_BYTE();
                class_body: {
                Value classDefVal = currentChunk->constants[classIndex];

                if (!classDefVal.isClassDefinition()) {
                    RUNTIME_ERROR_OR_THROW("OP_CLASS: expected class definition in constant pool");
                }
                auto cd = classDefVal.asClassDefinition();

                // Resolve parent classes from globals (integer-indexed lookup)
                std::vector<GcPtr<ClassDefinition>> resolvedParents;
                for (const auto& parentName : cd->parentNames) {
                    auto it = globalIndex.find(parentName);
                    if (it != globalIndex.end()) {
                        int pSlot = it->second;
                        if (static_cast<size_t>(pSlot) < globalValues.size() &&
                            globalDefined[static_cast<size_t>(pSlot)]) {
                            const auto& parentVal = globalValues[static_cast<size_t>(pSlot)];
                            if (parentVal.isCallable()) { auto callable = parentVal.asCallable();
                                if (callable) {
                                    auto parentDef = callable->getClassDef();
                                    if (parentDef) {
                                        resolvedParents.push_back(parentDef);
                                        cd->parentClasses.push_back(parentDef);
                                    }
                                }
                            }
                        }
                    }
                }

                // Store instance method VoraFunctions on the ClassDefinition
                for (const auto& methodProto : cd->methodProtos) {
                    auto methodFn = GcHeap::instance().alloc<VoraFunction>(
                        methodProto->name, methodProto->arity,
                        methodProto->requiredArity, methodProto->hasRest,
                        methodProto.get());
                    cd->methods[methodProto->name] = methodFn;
                }

                // Store static method VoraFunctions on the ClassDefinition
                for (const auto& staticProto : cd->staticMethodProtos) {
                    auto staticFn = GcHeap::instance().alloc<VoraFunction>(
                        staticProto->name, staticProto->arity,
                        staticProto->requiredArity, staticProto->hasRest,
                        staticProto.get());
                    cd->staticMethods[staticProto->name] = staticFn;
                }

                // Compute C3 MRO and cache on ClassDefinition
                {
                    auto mroShared = computeC3MRO(cd, resolvedParents);
                    cd->mro = std::move(mroShared);
                }

                // Create constructor VoraFunction
                auto ctorFn = GcHeap::instance().alloc<VoraFunction>(
                    cd->ctorProto->name, cd->ctorProto->arity,
                    cd->ctorProto->requiredArity, cd->ctorProto->hasRest,
                    cd->ctorProto.get());

                // Create ClassConstructor — the clean, dedicated constructor callable.
                // Globals are synced at call time via callInVM(), not snapshotted here.
                auto ctorCallable = GcHeap::instance().alloc<ClassConstructor>(
                    cd, ctorFn);

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
            uint16_t importPathIndex, importNameIndex;
            case OpCode::OP_IMPORT_WIDE:
                importPathIndex = READ_SHORT();
                importNameIndex = READ_SHORT();
                goto import_body;
            case OpCode::OP_IMPORT:
                importPathIndex = READ_BYTE();
                importNameIndex = READ_BYTE();
                import_body: {
                Value pathVal = currentChunk->constants[importPathIndex];
                if (!pathVal.isGcString()) {
                    RUNTIME_ERROR_OR_THROW("OP_IMPORT: expected string constant for path");
                    break;
                }
                std::string rawPath = pathVal.asGcString()->value;

                // Resolve the absolute path
                std::string resolved = resolveModulePath(rawPath);
                if (resolved.empty()) {
                    RUNTIME_ERROR_OR_THROW("Module not found: " + rawPath);
                    break;
                }

                // Check module cache
                auto it = moduleCache.find(resolved);
                if (it != moduleCache.end()) {
                    push(it->second);  // push cached module dict
                    break;
                }

                // Load, compile, and execute module
                Value moduleObj = loadModule(resolved);
                if (moduleObj.isNull()) {
                    std::string errMsg = loadModuleError;
                    loadModuleError.clear();

                    if (errMsg.empty()) {
                        // Error was already reported by a nested OP_IMPORT
                        // handler. Silently propagate the failure up.
                        return InterpretResult::RUNTIME_ERROR;
                    }

                    // This is the first (innermost) error — print it once
                    // with import chain context.
                    if (importStack.size() > 1) {
                        errMsg += "\nImport chain:";
                        for (size_t i = 0; i < importStack.size(); i++) {
                            errMsg += "\n  in import of " + importStack[i];
                        }
                    }

                    RUNTIME_ERROR_OR_THROW(errMsg);
                    break;
                }

                // Cache and push result
                moduleCache[resolved] = moduleObj;
                push(moduleObj);
                break;
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
#undef READ_SHORT
#undef READ_LONG_CONSTANT
#undef RUNTIME_ERROR_OR_THROW
#undef READ_CONSTANT
    } catch (const std::runtime_error& e) {
        runtimeError(e.what());
        return InterpretResult::RUNTIME_ERROR;
    } catch (const std::exception& e) {
        runtimeError(std::string("Internal VM error: ") + e.what());
        return InterpretResult::RUNTIME_ERROR;
    } catch (...) {
        runtimeError("Internal VM error (unknown exception)");
        return InterpretResult::RUNTIME_ERROR;
    }
}

// =========================================================================
// Module System — Path Resolution + Loading
// =========================================================================

std::string VM::resolveModulePath(const std::string& rawPath) const {
    // Step 1: Expand environment variables ($VAR and %VAR%)
    std::string expanded = rawPath;
    // Expand $VAR patterns
    size_t dollarPos = 0;
    while ((dollarPos = expanded.find('$', dollarPos)) != std::string::npos) {
        size_t end = dollarPos + 1;
        while (end < expanded.size() &&
               (std::isalnum(static_cast<unsigned char>(expanded[end])) ||
                expanded[end] == '_')) {
            end++;
        }
        if (end > dollarPos + 1) {
            std::string varName = expanded.substr(dollarPos + 1, end - dollarPos - 1);
            const char* envVal = std::getenv(varName.c_str());
            std::string replacement = envVal ? envVal : "";
            expanded.replace(dollarPos, end - dollarPos, replacement);
            dollarPos += replacement.size();
        } else {
            dollarPos++;
        }
    }
    // Expand %VAR% patterns (Windows style)
    size_t pctPos = 0;
    while ((pctPos = expanded.find('%', pctPos)) != std::string::npos) {
        size_t end = expanded.find('%', pctPos + 1);
        if (end != std::string::npos) {
            std::string varName = expanded.substr(pctPos + 1, end - pctPos - 1);
            const char* envVal = std::getenv(varName.c_str());
            std::string replacement = envVal ? envVal : "";
            expanded.replace(pctPos, end - pctPos + 1, replacement);
            pctPos += replacement.size();
        } else {
            break;
        }
    }

    // Helper: check if a path is absolute
    auto isAbsolute = [](const std::string& p) -> bool {
        if (p.empty()) return false;
        if (p[0] == '/') return true;
        // Windows: drive letter + colon
        if (p.size() >= 2 && p[1] == ':' && std::isalpha(static_cast<unsigned char>(p[0]))) {
            return true;
        }
        return false;
    };

    // Helper: check if a file exists
    auto fileExists = [](const std::string& p) -> bool {
        std::ifstream f(p);
        return f.good();
    };

    // Helper: append ".va" if no extension
    auto withExt = [](const std::string& p) -> std::string {
        if (p.size() > 3 && p.substr(p.size() - 3) == ".va") return p;
        return p + ".va";
    };

    // Helper: normalize a path — collapse ., /, .., double slashes, mixed
    // separators. Ensures the same physical file always produces the same
    // resolved path, which is critical for module cache and circular import
    // detection (A → B → A with different path strings would loop forever).
    auto normalizePath = [](std::string p) -> std::string {
        // Replace backslashes with forward slashes
        for (auto& c : p) { if (c == '\\') c = '/'; }
        // Collapse double slashes and "/./" patterns
        std::vector<std::string> parts;
        size_t start = 0;
        while (start < p.size()) {
            size_t end = p.find('/', start);
            if (end == std::string::npos) end = p.size();
            std::string part = p.substr(start, end - start);
            start = end + 1;
            if (part.empty() || part == ".") continue;
            if (part == "..") {
                if (!parts.empty()) parts.pop_back();
            } else {
                parts.push_back(part);
            }
        }
        std::string result;
        for (size_t i = 0; i < parts.size(); i++) {
            if (i > 0) result += '/';
            result += parts[i];
        }
        return result.empty() ? "." : result;
    };

    std::string result;

    // Step 2: Absolute path
    if (isAbsolute(expanded)) {
        result = normalizePath(withExt(expanded));
        if (fileExists(result)) return result;
        return "";
    }

    // Step 3: Relative path (./ or ../)
    if (expanded.size() >= 2 && expanded[0] == '.' &&
        (expanded[1] == '/' || expanded[1] == '\\')) {
        result = normalizePath(currentModuleDir + "/" + expanded);
        result = withExt(result);
        if (fileExists(result)) return result;
        return "";
    }
    if (expanded.size() >= 3 && expanded[0] == '.' && expanded[1] == '.' &&
        (expanded[2] == '/' || expanded[2] == '\\')) {
        result = normalizePath(currentModuleDir + "/" + expanded);
        result = withExt(result);
        if (fileExists(result)) return result;
        return "";
    }

    // Step 4: Stdlib module (no ./ or ../ prefix → stdDir lookup).
    // "math"       → stdDir/math.va
    // "std/math"   → strip leading "std/" to avoid doubling, → stdDir/math.va
    // "mylib/foo"  → stdDir/mylib/foo.va
    result = normalizePath(stdDir + "/" + expanded);
    result = withExt(result);
    if (fileExists(result)) return result;

    // If path starts with a segment matching stdDir's leaf name, try
    // stripping it (e.g. "std/math" + stdDir="./std" → stdDir/math.va).
    auto slash = expanded.find_first_of("/\\");
    if (slash != std::string::npos) {
        auto stdLeaf = stdDir.find_last_of("/\\");
        std::string leaf = (stdLeaf != std::string::npos) ? stdDir.substr(stdLeaf + 1) : stdDir;
        if (expanded.compare(0, slash, leaf) == 0) {
            result = normalizePath(stdDir + "/" + expanded.substr(slash + 1));
            result = withExt(result);
            if (fileExists(result)) return result;
        }
    }

    return "";
}

Value VM::loadModule(const std::string& resolvedPath) {
    // Guard against circular imports. Module A → B → A would loop
    // infinitely without this check since moduleCache is only populated
    // after loadModule() returns.
    for (size_t i = 0; i < importStack.size(); i++) {
        if (importStack[i] == resolvedPath) {
            std::string chain;
            for (size_t j = i; j < importStack.size(); j++) {
                if (j > i) chain += " → ";
                chain += importStack[j];
            }
            chain += " → " + resolvedPath;
            loadModuleError = "Circular import detected: " + chain;
            return Value(nullptr);
        }
    }
    importStack.push_back(resolvedPath);
    // Pop on every return path (success or failure).
    struct PopGuard {
        std::vector<std::string>& s;
        ~PopGuard() { s.pop_back(); }
    } guard{importStack};

    // 1. Read file
    std::ifstream file(resolvedPath, std::ios::binary);
    if (!file) {
        loadModuleError = "Failed to read module: " + resolvedPath;
        return Value(nullptr);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    // Strip UTF-8 BOM if present
    if (source.size() >= 3 &&
        static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB &&
        static_cast<unsigned char>(source[2]) == 0xBF) {
        source = source.substr(3);
    }

    // 2. Lex → Parse → Compile
    StderrErrorReporter moduleReporter(source);
    Lexer lexer(source, moduleReporter);
    auto tokens = lexer.scanTokens();
    if (lexer.hasError()) {
        loadModuleError = "Lexer error in module: " + resolvedPath;
        return Value(nullptr);
    }

    Parser parser(tokens, moduleReporter);
    parser.setSource(source);
    auto program = parser.parse();
    if (!program || parser.hasError()) {
        loadModuleError = "Parse error in module: " + resolvedPath;
        return Value(nullptr);
    }

    // 3. Create child VM first so we can seed the compiler with its globals
    VM childVM;
    childVM.stdDir = this->stdDir;

    // Share the import stack so circular imports are detected across
    // nested module loads (A → B → A).
    childVM.importStack = this->importStack;

    // Set currentModuleDir to the module file's directory
    auto slashPos = resolvedPath.find_last_of("/\\");
    if (slashPos != std::string::npos) {
        childVM.currentModuleDir = resolvedPath.substr(0, slashPos);
    } else {
        childVM.currentModuleDir = ".";
    }

    // Seed child VM with the global names so builtin slot numbers match
    childVM.initGlobals(globalNames);
    registerBuiltins(childVM);

    // Now compile with the compiler seeded from the child VM's globals,
    // so that global slot numbers in the bytecode match the child VM.
    // Use defined=false so that modules can shadow builtins
    // (e.g. `export let abs = abs`).
    Compiler compiler(moduleReporter);
    compiler.setSource(source);
    compiler.seedGlobals(childVM.globalNames, false);
    Chunk chunk = compiler.compile(program.get());
    if (compiler.hadError) {
        loadModuleError = "Compile error in module: " + resolvedPath;
        return Value(nullptr);
    }

    // Extend child VM globals with any new names the module compiler created
    // (e.g. PI, E, TAU, random in math.va). Without this, OP_DEFINE_GLOBAL
    // for these new globals would fail with "slot out of range" because the
    // child VM's globalNames was only seeded from the parent's names before
    // compilation. initGlobals is idempotent — existing names are preserved.
    childVM.initGlobals(compiler.getGlobalNames());

    // 4. Execute module in child VM
    InterpretResult result = childVM.interpret(chunk);
    if (result != InterpretResult::OK) {
        // Don't set loadModuleError — the child VM already reported the
        // error through its own OP_IMPORT handler. Setting a new message
        // here would cause the parent to print the same error again.
        return Value(nullptr);
    }

    // 5. Extract exported symbols from child VM's globals → build Dict
    auto exports = GcHeap::instance().alloc<Dict>();
    const auto& exportNames = compiler.getExportNames();

    for (const auto& exportName : exportNames) {
        // Look up the slot in the child VM
        auto idxIt = childVM.globalIndex.find(exportName);
        if (idxIt != childVM.globalIndex.end()) {
            int slot = idxIt->second;
            if (slot >= 0 && static_cast<size_t>(slot) < childVM.globalValues.size() &&
                childVM.globalDefined[slot]) {
                exports->pairs[exportName] = childVM.globalValues[slot];
            }
        }
    }

    return Value(exports);
}

} // namespace vora
