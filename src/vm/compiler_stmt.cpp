#include "compiler.h"

#include <cmath>
#include <iostream>
#include <variant>

#include "../gc/gc_heap.h"
#include "../runtime/vora_function.h"

namespace vora {
// =========================================================================
// StmtVisitor — statement compilation
// =========================================================================

void Compiler::visitLetStmt(const LetStmt& stmt) {
    // Compile initializer (or null if none)
    if (stmt.initializer) {
        stmt.initializer->accept(*this);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }

    if (scopeDepth == 0) {
        // Global variable — use interned slot ID (errors on redefinition).
        int slot = defineGlobal(stmt.name);
        if (hadError) return;
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                  static_cast<uint8_t>(slot));
    } else {
        // Local variable — the initializer value is already on the stack.
        // It stays there as the local's slot. Just record the slot.
        addLocal(stmt.name);
        // The value is already on the stack at the local slot position.
        // No OP_SET_LOCAL needed — the initializer result IS the local.
    }
}

void Compiler::visitReturnStmt(const ReturnStmt& stmt) {
    // Pop catch handlers for any enclosing try blocks before returning
    for (int t = 0; t < tryNesting; t++) {
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // Push return value (must be on stack before finally runs)
    if (stmt.value) {
        stmt.value->accept(*this);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }

    if (finallyNesting > 0) {
        // Instead of OP_RETURN, emit OP_JUMP that will be re-routed through
        // the finally block by visitTryStmt. The return value stays on stack.
        size_t jump = emitJump(OpCode::OP_JUMP);
        pendingReturnJumps.push_back(jump);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    }
}

void Compiler::visitIfStmt(const IfStmt& stmt) {
    // Compile condition
    stmt.condition->accept(*this);
    size_t thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Then branch
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    stmt.thenBranch->accept(*this);

    if (stmt.elseBranch) {
        size_t elseJump = emitJump(OpCode::OP_JUMP);
        patchJump(thenJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        stmt.elseBranch->accept(*this);
        patchJump(elseJump);
    } else {
        // Jump over the false-path OP_POP so the true path
        // doesn't fall through and pop a local variable value.
        size_t skipJump = emitJump(OpCode::OP_JUMP);
        patchJump(thenJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        patchJump(skipJump);
    }
}

void Compiler::visitWhileStmt(const WhileStmt& stmt) {
    size_t loopStart = chunk.code.size();

    // Push loop context for break/continue.
    // While loops: continueTarget == loopStart (backward jump to condition)
    loopStack.push_back({loopStart, loopStart, {}, {}, scopeDepth});

    // Condition
    stmt.condition->accept(*this);
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Body
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    stmt.body->accept(*this);

    // Loop back to condition
    emitLoop(loopStart);

    // Patch exit
    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Patch all break jumps
    for (size_t jumpOffset : loopStack.back().breakJumps) {
        patchJump(jumpOffset);
        // break already includes OP_POP in visitBreakStmt... actually no.
        // The break jumps to after the loop. By the time we get here,
        // the condition might or might not be on the stack.
        // We just jump. But we need to handle the stack:
        // If break is inside the body, the condition is NOT on stack
        // (body pops condition first).
        // Actually, the condition is popped in the body entry. And break
        // might leave some expression values. This is complex.
        // For Phase 2: break just jumps past the loop.
    }

    loopStack.pop_back();
}

void Compiler::visitForStmt(const ForStmt& stmt) {
    currentLine = stmt.forToken.line;
    currentColumn = stmt.forToken.column;
    // Desugar for-in into a while loop using local variables:
    //
    //   for x in iterable { body }
    // becomes:
    //   {
    //     let _iter = iterable
    //     let _i = 0
    //     while (_i < _length(_iter)) {
    //       let x = _iter[_i]
    //       body
    //       _i += 1
    //     }
    //   }
    //
    // For strings, indexing returns a single-char string.
    // The _length is computed via a builtin or by checking array/string size.

    beginScope();

    // Compile iterable expression and store as local _iter
    stmt.iterable->accept(*this);
    addLocal("_iter");

    // Initialize index _i = 0
    emitConstant(0.0);
    addLocal("_i");

    // Call _vora_len to get the length of _iter
    int lenSlot = resolveGlobal("_vora_len");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
              static_cast<uint8_t>(lenSlot));

    int iterSlot = resolveLocal("_iter");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iterSlot));
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
    addLocal("_len");

    // Loop structure:
    //   loopStart:  condition (_i < _len)
    //   body (with beginScope/endScope)
    //   continueTarget: increment (_i += 1)
    //   loop back to loopStart
    //
    // continue requires increment before looping back. We store
    // continueJumps and patch them to the continueTarget.

    size_t loopStart = chunk.code.size();
    // continueTarget = SIZE_MAX signals "for-in loop: use forward jump,
    // target will be set later". extraLocalsToPop = 3 accounts for the
    // auto-generated _iter, _i, _len locals that must be cleaned up on break.
    loopStack.push_back({loopStart, SIZE_MAX, {}, {}, scopeDepth, 3});

    // --- Condition: _i < _len ---
    int iSlot = resolveLocal("_i");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iSlot));

    int lenLocalSlot = resolveLocal("_len");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(lenLocalSlot));

    emitByte(static_cast<uint8_t>(OpCode::OP_LESS));
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // --- Body ---
    beginScope();

    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iterSlot));
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iSlot));
    emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
    addLocal(stmt.variable);

    stmt.body->accept(*this);

    endScope();

    // --- Increment _i (continue target) ---
    // Record this position for continue jumps
    loopStack.back().continueTarget = chunk.code.size();

    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iSlot));
    emitConstant(1.0);
    emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
    emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
              static_cast<uint8_t>(iSlot));
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // --- Loop back ---
    emitLoop(loopStart);

    // Patch exit (condition false → exit loop)
    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Patch continue jumps to the continueTarget (the increment section).
    // Must be done BEFORE endScope() because continue re-enters the loop
    // and needs _iter/_i/_len still on the stack.
    for (size_t jumpOffset : loopStack.back().continueJumps) {
        size_t target = loopStack.back().continueTarget;
        size_t jumpSize = target - jumpOffset - 2;
        if (jumpSize > UINT16_MAX) jumpSize = UINT16_MAX;
        chunk.writeAt(jumpOffset, static_cast<uint8_t>(jumpSize & 0xFF));
        chunk.writeAt(jumpOffset + 1, static_cast<uint8_t>((jumpSize >> 8) & 0xFF));
    }

    // Save break jumps before popping the loop context. They must be
    // patched AFTER endScope() because break already handled cleanup of
    // _iter/_i/_len via extraLocalsToPop — landing before endScope()
    // would double-pop those locals.
    std::vector<size_t> savedBreakJumps = std::move(loopStack.back().breakJumps);

    loopStack.pop_back();

    // End outer scope (pops _iter, _i, _len for normal exit path)
    endScope();

    // Patch break jumps after endScope() — break path skips the
    // endScope() cleanup since it already handled it.
    for (size_t jumpOffset : savedBreakJumps) {
        patchJump(jumpOffset);
    }
}

void Compiler::visitFuncStmt(const FuncStmt& stmt) {
    // Create a separate compiler for the function body
    Compiler fnCompiler;
    fnCompiler.enclosing = this;
    fnCompiler.chunk.source = chunk.source;  // propagate source for error display

    // Function body starts a new scope with parameters as locals
    fnCompiler.beginScope();

    // Count required params (without defaults)
    int requiredArity = 0;
    for (const auto& param : stmt.params) {
        if (!param.defaultValue) requiredArity++;
        else break;  // defaults must come after required params (parser enforces)
    }
    int totalArity = static_cast<int>(stmt.params.size());

    // Add parameters as locals (they'll be set by OP_CALL binding)
    for (const auto& param : stmt.params) {
        fnCompiler.addLocal(param.name);
    }

    // Emit default-parameter preamble for params with defaults
    for (int i = requiredArity; i < totalArity; i++) {
        const auto& param = stmt.params[static_cast<size_t>(i)];

        // OP_DEFAULT_PARAM <slot> <skipOffset>
        // If slot < actualArgCount, skip the default evaluation.
        fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_DEFAULT_PARAM));
        fnCompiler.emitByte(static_cast<uint8_t>(i));  // local slot
        size_t skipOffsetPos = fnCompiler.chunk.code.size();
        fnCompiler.emitByte(0xFF);  // placeholder skip offset (low)
        fnCompiler.emitByte(0xFF);  // placeholder skip offset (high)

        // Compile the default value expression (result on stack)
        param.defaultValue->accept(fnCompiler);

        // Store into the local slot and pop leftover
        fnCompiler.emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                             static_cast<uint8_t>(i));
        fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        // Patch the skip offset to jump over the default eval code
        if (!fnCompiler.hadError) {
            size_t jumpEnd = fnCompiler.chunk.code.size();
            size_t offset = static_cast<size_t>(jumpEnd - skipOffsetPos - 2);
            if (offset > 0xFFFF) {
                // Jump offset overflow
                fnCompiler.hadError = true;
            } else {
                fnCompiler.chunk.code[skipOffsetPos] =
                    static_cast<uint8_t>(offset & 0xFF);
                fnCompiler.chunk.code[skipOffsetPos + 1] =
                    static_cast<uint8_t>((offset >> 8) & 0xFF);
            }
        }
    }

    // Compile the function body
    for (const auto& s : stmt.body->statements) {
        s->accept(fnCompiler);
    }

    // Implicit return null at end of function
    fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

    // End parameter scope (will POPN params)
    fnCompiler.endScope();

    // Build function prototype (copy upvalues before moving)
    auto capturedUpvalues = fnCompiler.upvalues;  // copy before move
    FunctionPrototype proto;
    proto.name = stmt.name;
    proto.arity = totalArity;
    proto.requiredArity = requiredArity;
    proto.upvalues = std::move(fnCompiler.upvalues);
    proto.chunk = std::move(fnCompiler.chunk);

    // Store prototype in constant pool
    uint8_t protoIndex = addFunctionPrototype(std::move(proto));

    // Emit OP_CLOSURE to create the runtime VoraFunction
    // Format: OP_CLOSURE <protoIndex> <upvalueCount> {<isLocal> <index>}*
    emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE), protoIndex);
    emitByte(static_cast<uint8_t>(capturedUpvalues.size()));
    for (const auto& uv : capturedUpvalues) {
        emitByte(uv.isLocal ? 1 : 0);
        emitByte(uv.index);
    }

    // Bind to name
    if (scopeDepth == 0) {
        // Global function — use interned slot ID (errors on redefinition).
        int slot = defineGlobal(stmt.name);
        if (hadError) return;
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                  static_cast<uint8_t>(slot));
    } else {
        // Local function — the OP_CLOSURE result stays on stack as local slot
        addLocal(stmt.name);
    }
}

void Compiler::visitObjStmt(const ObjStmt& stmt) {
    // Phase 3: Object compilation.
    // Strategy: emit OP_CLASS with all methods pre-compiled, then call
    // the class constructor. The class object stores method prototypes
    // and parent info. At runtime, OP_CLASS + constructor call creates
    // an instance.

    // For now, we need to handle this properly. Let me implement it.

    // Build class prototype info
    // The class constant contains: name, parent name, params,
    // compiled methods map, and constructor body

    // First, compile each method
    std::vector<GcPtr<FunctionPrototype>> methodProtos;
    for (const auto& methodStmt : stmt.methods) {
        if (auto funcStmt = dynamic_cast<const FuncStmt*>(methodStmt.get())) {
            Compiler methodCompiler;
            methodCompiler.enclosing = this;
            methodCompiler.chunk.source = chunk.source;  // propagate source for error display

            // Count required params for methods
            int mRequiredArity = 0;
            for (const auto& mp : funcStmt->params) {
                if (!mp.defaultValue) mRequiredArity++;
                else break;
            }
            int mTotalArity = static_cast<int>(funcStmt->params.size());

            methodCompiler.beginScope();
            // Add 'this' as local at slot 0
            methodCompiler.addLocal("this");
            // Add parameters (slots 1, 2, ...)
            for (const auto& param : funcStmt->params) {
                methodCompiler.addLocal(param.name);
            }

            // Emit default-parameter preamble for method params with defaults
            for (int i = mRequiredArity; i < mTotalArity; i++) {
                const auto& param = funcStmt->params[static_cast<size_t>(i)];
                int slot = i + 1;  // slot 0 is 'this', params start at slot 1

                methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_DEFAULT_PARAM));
                methodCompiler.emitByte(static_cast<uint8_t>(slot));
                size_t skipPos = methodCompiler.chunk.code.size();
                methodCompiler.emitByte(0xFF);
                methodCompiler.emitByte(0xFF);

                param.defaultValue->accept(methodCompiler);

                methodCompiler.emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                                         static_cast<uint8_t>(slot));
                methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                if (!methodCompiler.hadError) {
                    size_t jumpEnd = methodCompiler.chunk.code.size();
                    size_t off = static_cast<size_t>(jumpEnd - skipPos - 2);
                    if (off > 0xFFFF) {
                        methodCompiler.hadError = true;
                    } else {
                        methodCompiler.chunk.code[skipPos] =
                            static_cast<uint8_t>(off & 0xFF);
                        methodCompiler.chunk.code[skipPos + 1] =
                            static_cast<uint8_t>((off >> 8) & 0xFF);
                    }
                }
            }

            // Compile method body
            for (const auto& s : funcStmt->body->statements) {
                s->accept(methodCompiler);
            }

            methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
            methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

            methodCompiler.endScope();

            auto proto = GcHeap::instance().alloc<FunctionPrototype>();
            proto->name = funcStmt->name;
            proto->arity = mTotalArity;
            proto->requiredArity = mRequiredArity;
            proto->chunk = std::move(methodCompiler.chunk);
            methodProtos.push_back(proto);
        }
    }

    // Compile constructor body
    // Count required params
    int ctorRequiredArity = 0;
    for (const auto& cp : stmt.params) {
        if (!cp.defaultValue) ctorRequiredArity++;
        else break;
    }
    int ctorTotalArity = static_cast<int>(stmt.params.size());

    Compiler ctorCompiler;
    ctorCompiler.enclosing = this;
    ctorCompiler.chunk.source = chunk.source;  // propagate source for error display
    ctorCompiler.beginScope();
    ctorCompiler.addLocal("this");  // slot 0
    for (const auto& param : stmt.params) {
        ctorCompiler.addLocal(param.name);
    }

    // Emit default-parameter preamble for constructor params with defaults
    for (int i = ctorRequiredArity; i < ctorTotalArity; i++) {
        const auto& param = stmt.params[static_cast<size_t>(i)];
        int slot = i + 1;  // slot 0 is 'this'

        ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_DEFAULT_PARAM));
        ctorCompiler.emitByte(static_cast<uint8_t>(slot));
        size_t skipPos = ctorCompiler.chunk.code.size();
        ctorCompiler.emitByte(0xFF);
        ctorCompiler.emitByte(0xFF);

        param.defaultValue->accept(ctorCompiler);

        ctorCompiler.emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                               static_cast<uint8_t>(slot));
        ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        if (!ctorCompiler.hadError) {
            size_t jumpEnd = ctorCompiler.chunk.code.size();
            size_t off = static_cast<size_t>(jumpEnd - skipPos - 2);
            if (off > 0xFFFF) {
                ctorCompiler.hadError = true;
            } else {
                ctorCompiler.chunk.code[skipPos] =
                    static_cast<uint8_t>(off & 0xFF);
                ctorCompiler.chunk.code[skipPos + 1] =
                    static_cast<uint8_t>((off >> 8) & 0xFF);
            }
        }
    }

    for (const auto& s : stmt.body->statements) {
        s->accept(ctorCompiler);
    }
    ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    ctorCompiler.endScope();

    auto ctorProto = GcHeap::instance().alloc<FunctionPrototype>();
    ctorProto->name = stmt.name;
    ctorProto->arity = ctorTotalArity;
    ctorProto->requiredArity = ctorRequiredArity;
    ctorProto->chunk = std::move(ctorCompiler.chunk);

    // Extract param names for ClassDefinition
    std::vector<std::string> paramNames;
    for (const auto& p : stmt.params) {
        paramNames.push_back(p.name);
    }

    // Store class definition in constant pool
    auto classDef = GcHeap::instance().alloc<ClassDefinition>();
    classDef->name = stmt.name;
    classDef->parentNames = stmt.parentNames;
    classDef->params = paramNames;
    classDef->ctorProto = ctorProto;
    classDef->methodProtos = methodProtos;

    uint8_t classIndex = makeConstant(classDef);

    // Emit OP_CLASS to create the class constructor callable
    emitBytes(static_cast<uint8_t>(OpCode::OP_CLASS), classIndex);

    // Bind to name
    if (scopeDepth == 0) {
        int slot = defineGlobal(stmt.name);
        if (hadError) return;
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                  static_cast<uint8_t>(slot));
    } else {
        addLocal(stmt.name);
    }
}

void Compiler::visitBreakStmt(const BreakStmt& stmt) {
    currentLine = stmt.keyword.line;
    currentColumn = stmt.keyword.column;
    if (loopStack.empty()) {
        return;
    }

    // Pop catch handlers for any enclosing try blocks (they are skipped by
    // the break jump and would otherwise leak on the VM's catch handler stack).
    for (int t = 0; t < tryNesting; t++) {
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // Pop locals from current scope down to the loop's enclosing scope
    int localsToPop = 0;
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth > loopStack.back().enclosingScopeDepth) {
            localsToPop++;
        } else {
            break;
        }
    }
    // For-in loops generate _iter, _i, _len at enclosingScopeDepth which
    // must also be cleaned up on break (they persist through continue).
    localsToPop += loopStack.back().extraLocalsToPop;

    if (localsToPop > 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_POPN),
                  static_cast<uint8_t>(localsToPop));
    }

    // Emit jump to after the loop. Patch later.
    size_t jumpOffset = emitJump(OpCode::OP_JUMP);
    loopStack.back().breakJumps.push_back(jumpOffset);
}

void Compiler::visitContinueStmt(const ContinueStmt& stmt) {
    currentLine = stmt.keyword.line;
    currentColumn = stmt.keyword.column;
    if (loopStack.empty()) {
        return;
    }

    // Pop catch handlers for any enclosing try blocks.
    for (int t = 0; t < tryNesting; t++) {
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // Pop locals from current scope down to the loop's enclosing scope
    int localsToPop = 0;
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth > loopStack.back().enclosingScopeDepth) {
            localsToPop++;
        } else {
            break;
        }
    }
    if (localsToPop > 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_POPN),
                  static_cast<uint8_t>(localsToPop));
    }

    // For while loops (continueTarget == loopStart): emit OP_LOOP backward.
    // For for-in loops (continueTarget == SIZE_MAX, not yet set): emit OP_JUMP
    // forward placeholder, patched later when continueTarget is known.
    if (loopStack.back().continueTarget == loopStack.back().loopStart) {
        emitLoop(loopStack.back().loopStart);
    } else {
        size_t jumpOffset = emitJump(OpCode::OP_JUMP);
        loopStack.back().continueJumps.push_back(jumpOffset);
    }
}

void Compiler::visitTryStmt(const TryStmt& stmt) {
    // Emit OP_PUSH_CATCH <localCount> <offset16>.
    // localCount tells the VM where to place the exception value on the stack
    // (frameBaseIndex + localCount). This ensures it lands after all existing
    // locals, matching the slot where the catch variable will be allocated.
    emitByte(static_cast<uint8_t>(OpCode::OP_PUSH_CATCH));
    emitByte(static_cast<uint8_t>(currentLocalCount()));
    size_t pushCatchPlaceholder = chunk.code.size();
    emitByte(0xFF);  // offset low placeholder
    emitByte(0xFF);  // offset high placeholder

    // Track try nesting so break/continue/return inside emit OP_POP_CATCH
    tryNesting++;

    // =========================================================================
    // Snapshot loop break/continue counts and return jump count BEFORE try body
    // =========================================================================
    std::vector<size_t> savedBreakCounts;
    std::vector<size_t> savedContinueCounts;
    for (const auto& lc : loopStack) {
        savedBreakCounts.push_back(lc.breakJumps.size());
        savedContinueCounts.push_back(lc.continueJumps.size());
    }
    size_t savedReturnCount = pendingReturnJumps.size();

    // Track finally nesting for return routing
    if (stmt.finallyBlock) finallyNesting++;

    // Compile try body (break/continue/return inside may emit new jumps)
    stmt.tryBlock->accept(*this);

    if (stmt.finallyBlock) finallyNesting--;
    tryNesting--;

    // =========================================================================
    // Capture new break/continue jumps emitted inside the try body.
    // They will be re-routed through the finally block (if present).
    // =========================================================================
    struct CapturedJump { size_t offset; int loopIdx; };
    std::vector<CapturedJump> capturedBreaks;
    std::vector<CapturedJump> capturedContinues;
    for (size_t li = 0; li < loopStack.size(); li++) {
        auto& lc = loopStack[li];
        while (lc.breakJumps.size() > savedBreakCounts[li]) {
            capturedBreaks.push_back({lc.breakJumps.back(), static_cast<int>(li)});
            lc.breakJumps.pop_back();
        }
        while (lc.continueJumps.size() > savedContinueCounts[li]) {
            capturedContinues.push_back({lc.continueJumps.back(), static_cast<int>(li)});
            lc.continueJumps.pop_back();
        }
    }
    // Capture return jumps emitted inside try body
    std::vector<size_t> capturedReturns;
    while (pendingReturnJumps.size() > savedReturnCount) {
        capturedReturns.push_back(pendingReturnJumps.back());
        pendingReturnJumps.pop_back();
    }

    // =========================================================================
    // Normal try exit: OP_POP_CATCH
    // =========================================================================
    emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));

    if (stmt.catchBlock) {
        // Skip catch block in normal flow
        size_t skipCatchJump = emitJump(OpCode::OP_JUMP);

        // Record catch block position and patch PUSH_CATCH to jump here
        size_t catchTarget = chunk.code.size();
        size_t catchJumpSize = catchTarget - pushCatchPlaceholder - 2;
        if (catchJumpSize > UINT16_MAX) catchJumpSize = UINT16_MAX;
        chunk.writeAt(pushCatchPlaceholder,
                      static_cast<uint8_t>(catchJumpSize & 0xFF));
        chunk.writeAt(pushCatchPlaceholder + 1,
                      static_cast<uint8_t>((catchJumpSize >> 8) & 0xFF));

        // Clear exceptionInFlight — the catch block handles the exception,
        // so any following OP_FINALLY_END should NOT re-throw.
        emitByte(static_cast<uint8_t>(OpCode::OP_CLEAR_EXCEPTION));
        // Pop catch handler (entered via exception path)
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));

        // Enter catch scope and bind exception variable
        beginScope();
        addLocal(stmt.catchVar);
        stmt.catchBlock->accept(*this);
        endScope();

        // Patch the skip-catch jump from normal flow
        patchJump(skipCatchJump);
    } else {
        // No catch — patch PUSH_CATCH to skip to after try
        patchJump(pushCatchPlaceholder);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // =========================================================================
    // Finally block — compiled once for the normal flow, then bytecode is
    // captured and replayed at each captured break/continue/return site.
    // =========================================================================
    if (stmt.finallyBlock) {
        // Record the finally bytecode (before OP_FINALLY_END)
        size_t finallyStart = chunk.code.size();
        stmt.finallyBlock->accept(*this);
        size_t finallyEnd = chunk.code.size();
        std::vector<uint8_t> finallyBytes(
            chunk.code.begin() + finallyStart,
            chunk.code.begin() + finallyEnd);
        emitByte(static_cast<uint8_t>(OpCode::OP_FINALLY_END));

        // Push finally bytecode onto stack (for nested finally blocks, the
        // outermost finally bytecode is replayed first at capture sites).
        finallyBytecodeStack.push_back(std::move(finallyBytes));

        // --- Route captured break jumps through finally ---
        for (const auto& cj : capturedBreaks) {
            // Patch the break's OP_JUMP placeholder to point to current position
            patchJump(cj.offset);
            // Replay finally bytecode
            for (uint8_t b : finallyBytecodeStack.back()) emitByte(b);
            emitByte(static_cast<uint8_t>(OpCode::OP_FINALLY_END));
            // New jump — re-added to the loop's breakJumps for normal patching
            size_t newJump = emitJump(OpCode::OP_JUMP);
            loopStack[static_cast<size_t>(cj.loopIdx)].breakJumps.push_back(newJump);
        }

        // --- Route captured continue jumps through finally ---
        for (const auto& cj : capturedContinues) {
            patchJump(cj.offset);
            for (uint8_t b : finallyBytecodeStack.back()) emitByte(b);
            emitByte(static_cast<uint8_t>(OpCode::OP_FINALLY_END));
            size_t newJump = emitJump(OpCode::OP_JUMP);
            loopStack[static_cast<size_t>(cj.loopIdx)].continueJumps.push_back(newJump);
        }

        // --- Route captured return jumps through finally ---
        for (size_t jumpOffset : capturedReturns) {
            // The return value is already on the stack (pushed by visitReturnStmt
            // before the OP_JUMP). After finally runs, it'll still be at peek(0).
            patchJump(jumpOffset);
            for (uint8_t b : finallyBytecodeStack.back()) emitByte(b);
            emitByte(static_cast<uint8_t>(OpCode::OP_FINALLY_END));
            emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
        }

        // Pop the finally bytecode stack entry
        finallyBytecodeStack.pop_back();
    }
}

void Compiler::visitThrowStmt(const ThrowStmt& stmt) {
    currentLine = stmt.keyword.line;
    currentColumn = stmt.keyword.column;
    // Compile the value to throw
    stmt.value->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_THROW));
}

} // namespace vora
