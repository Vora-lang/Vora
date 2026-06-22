#include "compiler.h"

#include <cctype>
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
    // Destructured binding: let [a, b] = expr  or  let {x, y} = expr
    if (stmt.binding) {
        compileDestructuredBinding(*stmt.binding, stmt.initializer.get(), stmt.isConst);
        return;
    }

    // Compile initializer (or null if none)
    if (stmt.initializer) {
        stmt.initializer->accept(*this);
    } else {
        // const must have an initializer
        if (stmt.isConst) {
            error("const variable '" + stmt.name + "' must be initialized");
            return;
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }

    if (scopeDepth == 0) {
        // Global variable — use interned slot ID (errors on redefinition).
        int slot;
        if (stmt.isConst) {
            slot = defineGlobalConst(stmt.name);
        } else {
            slot = defineGlobal(stmt.name);
        }
        if (hadError) return;
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                  static_cast<uint8_t>(slot));
    } else {
        // Local variable — the initializer value is already on the stack.
        // It stays there as the local's slot. Just record the slot.
        addLocal(stmt.name, stmt.isConst);
        // The value is already on the stack at the local slot position.
        // No OP_SET_LOCAL needed — the initializer result IS the local.
    }
}

// =========================================================================
// Destructuring compilation helpers
// =========================================================================

void Compiler::compileDestructuredBinding(const BindingPattern& pattern,
                                          const Expr* initializer, bool isConst) {
    // Compile the RHS value (the source array/object to destructure).
    if (initializer) {
        initializer->accept(*this);
    } else {
        if (isConst) {
            error("const destructuring requires an initializer");
            return;
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }

    // For global scope, we pop values via OP_DEFINE_GLOBAL.
    // For local scope, they stay on the stack as locals.
    // The pattern compilation handles both cases.
    compileBindPattern(pattern, scopeDepth == 0 ? -1 : -1, isConst);
}

void Compiler::compileBindPattern(const BindingPattern& pattern,
                                  int sourceSlot, bool isConst) {
    switch (pattern.kind()) {

    case BindingKind::Identifier: {
        const auto& id = static_cast<const IdentifierBinding&>(pattern);
        // The value to bind is already on top of stack.
        if (scopeDepth == 0) {
            int slot;
            if (isConst) {
                slot = defineGlobalConst(id.name);
            } else {
                slot = defineGlobal(id.name);
            }
            if (hadError) return;
            emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                      static_cast<uint8_t>(slot));
        } else {
            addLocal(id.name, isConst);
        }
        break;
    }

    case BindingKind::Array: {
        const auto& arr = static_cast<const ArrayBinding&>(pattern);
        // Source array is on top of stack. Save it in a temp local so we
        // can access it repeatedly while processing elements.
        bool hasRest = arr.rest != nullptr;
        bool isGlobal = (scopeDepth == 0);
        int tempId = destructureTempCounter++;
        int srcSlot = -1;

        if (isGlobal) {
            // Global scope: use OP_DUP to keep array accessible.
            // OP_DEFINE_GLOBAL pops the bound value, leaving the DUP'd array.
        } else {
            // Local scope: save array in a temp local so we can reload it.
            // We can't DUP because addLocal doesn't pop — the local value
            // would stay on stack and the next DUP would dup it, not the array.
            std::string srcName = "_ds" + std::to_string(tempId) + "_src";
            addLocal(srcName);
            srcSlot = resolveLocal(srcName);
        }

        for (size_t i = 0; i < arr.elements.size(); i++) {
            if (isGlobal) {
                emitByte(static_cast<uint8_t>(OpCode::OP_DUP));  // dup array
            } else {
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));         // load array
            }
            emitConstant(static_cast<double>(i));             // push index
            emitByte(static_cast<uint8_t>(OpCode::OP_INDEX)); // array[i]
            compileBindPattern(*arr.elements[i], -1, isConst);
        }

        // Rest element: ...rest
        if (hasRest) {
            if (scopeDepth == 0) {
                error("Rest element in destructuring is only supported inside functions");
                emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
                compileBindPattern(*arr.rest, -1, isConst);
            } else {
                int restStartIdx = static_cast<int>(arr.elements.size());

                // Create empty rest array
                std::string arrName = "_ds" + std::to_string(tempId) + "_arr";
                emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY), 0);
                addLocal(arrName);
                int arrSlot = resolveLocal(arrName);

                // Loop counter: _rest_i = restStartIdx
                std::string iName = "_ds" + std::to_string(tempId) + "_i";
                emitConstant(static_cast<double>(restStartIdx));
                addLocal(iName);
                int iSlot = resolveLocal(iName);

                // Get array length: _rest_len = _vora_len(_arr_src)
                std::string lenName = "_ds" + std::to_string(tempId) + "_len";
                int lenBuiltinSlot = resolveGlobal("_vora_len");
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                          static_cast<uint8_t>(lenBuiltinSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
                addLocal(lenName);
                int lenSlot = resolveLocal(lenName);

                // While loop: _rest_i < _rest_len
                size_t loopStart = chunk.code.size();
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(lenSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_LESS_NN));
                size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

                // Body: _rest_arr = _rest_arr + [arr[i]]
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(arrSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
                emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY), 1);
                emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(arrSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                // i = i + 1
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitConstant(1.0);
                emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                emitLoop(loopStart);
                patchJump(exitJump);
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                // Push rest array and bind
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(arrSlot));
                compileBindPattern(*arr.rest, -1, isConst);
            }
        }

        // Pop the source array temp (no longer needed)
        // Actually, the temp will be cleaned up by endScope.
        // For global scope, pop the original array.
        if (scopeDepth == 0) {
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        break;
    }

    case BindingKind::Object: {
        const auto& obj = static_cast<const ObjectBinding&>(pattern);
        // Source object is on top of stack. Save in temp local.
        int tempId = destructureTempCounter++;
        bool isGlobal = (scopeDepth == 0);
        int srcSlot = -1;

        if (isGlobal) {
            // Global scope: OP_DUP works because OP_DEFINE_GLOBAL pops.
        } else {
            std::string srcName = "_ds" + std::to_string(tempId) + "_obj";
            addLocal(srcName);
            srcSlot = resolveLocal(srcName);
        }

        for (size_t i = 0; i < obj.properties.size(); i++) {
            const auto& prop = obj.properties[i];
            if (isGlobal) {
                emitByte(static_cast<uint8_t>(OpCode::OP_DUP));  // dup object
            } else {
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));         // load object
            }
            uint8_t keyIndex = identifierConstant(prop.key);
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), keyIndex);
            compileBindPattern(*prop.pattern, -1, isConst);
        }

        // Rest for object: not implemented in v1
        if (obj.rest) {
            emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
            compileBindPattern(*obj.rest, -1, isConst);
        }

        // Pop source object at global scope
        if (scopeDepth == 0) {
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        break;
    }
    }
}

void Compiler::visitReturnStmt(const ReturnStmt& stmt) {
    // Execute deferred calls (LIFO) before returning
    emitDeferCalls();

    // Pop catch handlers for any enclosing try blocks before returning
    for (int t = 0; t < tryNesting; t++) {
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // Tail call optimization (TCO): when returning a direct function call
    // and we're not inside a try/finally block, emit OP_TAIL_CALL to
    // reuse the current call frame instead of growing the stack.
    // This enables infinite tail recursion without stack overflow.
    if (stmt.value && tryNesting == 0 && finallyNesting == 0) {
        if (auto* call = dynamic_cast<CallExpr*>(stmt.value.get())) {
            // Compile callee first (goes below arguments on stack)
            call->callee->accept(*this);
            // Compile arguments (pushed on top of callee)
            for (const auto& arg : call->arguments) {
                arg->accept(*this);
            }
            emitBytes(static_cast<uint8_t>(OpCode::OP_TAIL_CALL),
                      static_cast<uint8_t>(call->arguments.size()));
            return;  // OP_TAIL_CALL handles frame reuse, no OP_RETURN needed
        }
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

void Compiler::visitDoWhileStmt(const DoWhileStmt& stmt) {
    size_t loopStart = chunk.code.size();

    // Push loop context with continueTarget = SIZE_MAX initially.
    // Unlike while (where continueTarget == loopStart for backward jump),
    // do-while has continue jump forward to the condition, which hasn't
    // been compiled yet. This matches the for-in/C-for pattern.
    loopStack.push_back({loopStart, SIZE_MAX, {}, {}, scopeDepth});

    // --- Body (always executes at least once, unconditionally) ---
    stmt.body->accept(*this);

    // --- Condition (continue target) ---
    // Record this position as the continue target and patch any forward
    // continue jumps emitted during body compilation.
    loopStack.back().continueTarget = chunk.code.size();
    for (size_t jumpOffset : loopStack.back().continueJumps) {
        patchJump(jumpOffset);
    }
    loopStack.back().continueJumps.clear();

    stmt.condition->accept(*this);
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Pop truthy condition value from successful check
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Loop back to body start (not condition — body re-executes)
    emitLoop(loopStart);

    // --- Exit ---
    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Patch all break jumps
    for (size_t jumpOffset : loopStack.back().breakJumps) {
        patchJump(jumpOffset);
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

void Compiler::visitCForStmt(const CForStmt& stmt) {
    // Desugar C-style for into a while loop:
    //
    //   for (init; cond; incr) { body }
    // becomes (pseudo-bytecode):
    //   {
    //     init;                        // let locals at scope N
    //     loopStart:
    //       if (!cond) goto exit;
    //       body;                      // may add nested scopes
    //     continueTarget:
    //       incr;
    //       goto loopStart;
    //     exit:
    //   }
    //
    // Scoping follows the for-in pattern:
    //   - beginScope wraps the whole loop (initializer locals are scoped here)
    //   - enclosingScopeDepth = scopeDepth (inside the scope) so continue only
    //     pops body-locals, never the initializer locals
    //   - extraLocalsToPop tracks initializer locals so break can clean them up
    //   - Break jumps land after endScope(); continue jumps land at increment

    beginScope();

    // Record how many locals the initializer adds (for break cleanup)
    size_t localsBeforeInit = locals.size();

    // --- Initializer ---
    if (stmt.initializer) {
        stmt.initializer->accept(*this);
    }

    int extraLocals = static_cast<int>(locals.size() - localsBeforeInit);

    size_t loopStart = chunk.code.size();
    // continueTarget = SIZE_MAX signals "target will be set later"
    loopStack.push_back({loopStart, SIZE_MAX, {}, {}, scopeDepth, extraLocals});

    // --- Condition ---
    if (stmt.condition) {
        stmt.condition->accept(*this);
    } else {
        // Empty condition = always true
        emitByte(static_cast<uint8_t>(OpCode::OP_TRUE));
    }
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // --- Body ---
    stmt.body->accept(*this);

    // --- Increment (continue target) ---
    loopStack.back().continueTarget = chunk.code.size();
    if (stmt.increment) {
        stmt.increment->accept(*this);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));  // discard increment result
    }

    // --- Loop back ---
    emitLoop(loopStart);

    // --- Exit ---
    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Patch continue jumps to the increment section
    for (size_t jumpOffset : loopStack.back().continueJumps) {
        size_t target = loopStack.back().continueTarget;
        size_t jumpSize = target - jumpOffset - 2;
        if (jumpSize > UINT16_MAX) jumpSize = UINT16_MAX;
        chunk.writeAt(jumpOffset, static_cast<uint8_t>(jumpSize & 0xFF));
        chunk.writeAt(jumpOffset + 1, static_cast<uint8_t>((jumpSize >> 8) & 0xFF));
    }

    // Save break jumps before popping the loop context.
    // Break path: pops extraLocals (init locals) + body locals → OP_JUMP.
    // Normal path: endScope() pops init locals.
    std::vector<size_t> savedBreakJumps = std::move(loopStack.back().breakJumps);
    loopStack.pop_back();

    // End the C-for scope (normal exit path — pops initializer locals)
    endScope();

    // Patch break jumps after endScope() — break already cleaned up init locals
    // via extraLocalsToPop, so it skips endScope().
    for (size_t jumpOffset : savedBreakJumps) {
        patchJump(jumpOffset);
    }
}

void Compiler::visitFuncStmt(const FuncStmt& stmt) {
    // Create a separate compiler for the function body
    Compiler fnCompiler(errorReporter_);
    fnCompiler.enclosing = this;
    fnCompiler.chunk.source = chunk.source;  // propagate source for error display

    // Function body starts a new scope with parameters as locals
    fnCompiler.beginScope();

    // Detect rest parameter (always last if present)
    bool hasRest = !stmt.params.empty() && stmt.params.back().isRest;

    // Count required params (without defaults, excluding rest)
    int requiredArity = 0;
    for (const auto& param : stmt.params) {
        if (param.isRest) break;
        if (!param.defaultValue) requiredArity++;
        else break;  // defaults must come after required params (parser enforces)
    }
    int totalArity = hasRest
        ? static_cast<int>(stmt.params.size()) - 1
        : static_cast<int>(stmt.params.size());

    // Add fixed parameters as locals (they'll be bound by callVoraFunction)
    for (const auto& param : stmt.params) {
        if (param.isRest) break;
        fnCompiler.addLocal(param.name);
    }
    // Add rest parameter as a local (value set by callVoraFunction as array)
    if (hasRest) {
        fnCompiler.addLocal(stmt.params.back().name);
    }

    // Emit default-parameter preamble for fixed params with defaults
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

    // Pre-allocate local for self-recursion: add the function name as a
    // local in the ENCLOSING scope BEFORE compiling the body. This lets
    // recursive references resolve via upvalue capture. The OP_CLOSURE
    // result will be pushed onto the stack at this local's position.
    if (scopeDepth > 0) {
        addLocal(stmt.name);
    }

    // Compile the function body
    for (const auto& s : stmt.body->statements) {
        s->accept(fnCompiler);
    }

    // Execute deferred calls (LIFO) before implicit return
    fnCompiler.emitDeferCalls();

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
    proto.hasRest = hasRest;
    proto.upvalues = std::move(fnCompiler.upvalues);
    proto.chunk = std::move(fnCompiler.chunk);
    proto.isGenerator = fnCompiler.isGenerator;

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
    }
    // For local functions (scopeDepth > 0): the name was pre-allocated via
    // addLocal before body compilation. OP_CLOSURE already pushed the
    // function onto the stack — since addLocal was called beforehand and
    // the stack grows from the local region, the pushed value is already
    // at the correct position for the pre-allocated local slot. No extra
    // OP_SET_LOCAL or OP_POP needed.
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
            Compiler methodCompiler(errorReporter_);
            methodCompiler.enclosing = this;
            methodCompiler.chunk.source = chunk.source;  // propagate source for error display

            // Detect rest parameter for methods
            bool mHasRest = !funcStmt->params.empty() && funcStmt->params.back().isRest;

            // Count required params for methods (excluding rest)
            int mRequiredArity = 0;
            for (const auto& mp : funcStmt->params) {
                if (mp.isRest) break;
                if (!mp.defaultValue) mRequiredArity++;
                else break;
            }
            int mTotalArity = mHasRest
                ? static_cast<int>(funcStmt->params.size()) - 1
                : static_cast<int>(funcStmt->params.size());

            methodCompiler.beginScope();
            // Add 'this' as local at slot 0
            methodCompiler.addLocal("this");
            // Add fixed parameters (slots 1, 2, ...)
            for (const auto& param : funcStmt->params) {
                if (param.isRest) break;
                methodCompiler.addLocal(param.name);
            }
            // Add rest parameter as a local
            if (mHasRest) {
                methodCompiler.addLocal(funcStmt->params.back().name);
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

            methodCompiler.emitDeferCalls();
            methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
            methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

            methodCompiler.endScope();

            auto proto = GcHeap::instance().alloc<FunctionPrototype>();
            proto->name = funcStmt->name;
            proto->arity = mTotalArity;
            proto->requiredArity = mRequiredArity;
            proto->hasRest = mHasRest;
            proto->chunk = std::move(methodCompiler.chunk);
            methodProtos.push_back(proto);
        }
    }

    // Compile constructor body
    // Detect rest parameter for constructor
    bool ctorHasRest = !stmt.params.empty() && stmt.params.back().isRest;

    // Count required params (excluding rest)
    int ctorRequiredArity = 0;
    for (const auto& cp : stmt.params) {
        if (cp.isRest) break;
        if (!cp.defaultValue) ctorRequiredArity++;
        else break;
    }
    int ctorTotalArity = ctorHasRest
        ? static_cast<int>(stmt.params.size()) - 1
        : static_cast<int>(stmt.params.size());

    Compiler ctorCompiler(errorReporter_);
    ctorCompiler.enclosing = this;
    ctorCompiler.chunk.source = chunk.source;  // propagate source for error display
    ctorCompiler.beginScope();
    ctorCompiler.addLocal("this");  // slot 0
    // Add fixed parameters (slots 1, 2, ...)
    for (const auto& param : stmt.params) {
        if (param.isRest) break;
        ctorCompiler.addLocal(param.name);
    }
    // Add rest parameter as a local
    if (ctorHasRest) {
        ctorCompiler.addLocal(stmt.params.back().name);
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
    ctorCompiler.emitDeferCalls();
    ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    ctorCompiler.endScope();

    auto ctorProto = GcHeap::instance().alloc<FunctionPrototype>();
    ctorProto->name = stmt.name;
    ctorProto->arity = ctorTotalArity;
    ctorProto->requiredArity = ctorRequiredArity;
    ctorProto->hasRest = ctorHasRest;
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

void Compiler::visitImportStmt(const ImportStmt& stmt) {
    currentLine = stmt.keyword.line;
    currentColumn = stmt.keyword.column;

    // --- from "path" import a, b, c ---
    if (!stmt.importNames.empty()) {
        // Emit OP_IMPORT with the path and a dummy name (not used for binding).
        uint8_t pathIndex = makeConstant(
            GcHeap::instance().alloc<GcString>(stmt.modulePath));
        uint8_t nameIndex = makeConstant(
            GcHeap::instance().alloc<GcString>(""));
        emitBytes(static_cast<uint8_t>(OpCode::OP_IMPORT), pathIndex);
        emitByte(nameIndex);
        // Stack: [moduleDict]

        for (size_t i = 0; i < stmt.importNames.size(); i++) {
            const auto& name = stmt.importNames[i];
            bool isLast = (i == stmt.importNames.size() - 1);

            if (!isLast) {
                // Duplicate the dict so OP_GET_PROPERTY can pop its copy.
                emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
            }
            // OP_GET_PROPERTY pops the dict, looks up `name`, pushes the value.
            uint8_t propIdx = makeConstant(
                GcHeap::instance().alloc<GcString>(name));
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), propIdx);
            // Stack: [moduleDict (or gone if last), value]

            // Bind the extracted value.
            if (scopeDepth > 0) {
                addLocal(name, false);
            } else {
                int slot = resolveGlobal(name);
                emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                          static_cast<uint8_t>(slot));
            }
        }
        return;
    }

    // --- import "path" / import "path" as alias ---
    // Derive variable name: use `as` alias if provided, otherwise derive
    // from the module path basename.
    std::string varName;
    if (!stmt.alias.empty()) {
        varName = stmt.alias;
    } else {
        varName = stmt.modulePath;
        auto slashPos = varName.find_last_of("/\\");
        if (slashPos != std::string::npos) {
            varName = varName.substr(slashPos + 1);
        }
        if (varName.size() > 3 && varName.substr(varName.size() - 3) == ".va") {
            varName = varName.substr(0, varName.size() - 3);
        }
        while (!varName.empty() && !std::isalpha(static_cast<unsigned char>(varName[0]))) {
            varName = varName.substr(1);
        }
        if (varName.empty()) {
            varName = stmt.modulePath;
            auto sp = varName.find_last_of("/\\");
            if (sp != std::string::npos) varName = varName.substr(sp + 1);
        }
    }

    // Emit OP_IMPORT with path and variable name as constants
    uint8_t pathIndex = makeConstant(
        GcHeap::instance().alloc<GcString>(stmt.modulePath));
    uint8_t nameIndex = makeConstant(
        GcHeap::instance().alloc<GcString>(varName));
    emitBytes(static_cast<uint8_t>(OpCode::OP_IMPORT), pathIndex);
    emitByte(nameIndex);

    // OP_IMPORT pushes the module dict onto the stack.
    if (scopeDepth > 0) {
        addLocal(varName, false);
    } else {
        int slot = resolveGlobal(varName);
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                  static_cast<uint8_t>(slot));
    }
}

void Compiler::visitExportStmt(const ExportStmt& stmt) {
    // Compile the inner declaration as normal
    stmt.declaration->accept(*this);

    // Track the exported name based on declaration type
    if (auto* fs = dynamic_cast<FuncStmt*>(stmt.declaration.get())) {
        exportNames.push_back(fs->name);
    } else if (auto* ls = dynamic_cast<LetStmt*>(stmt.declaration.get())) {
        exportNames.push_back(ls->name);
    } else if (auto* os = dynamic_cast<ObjStmt*>(stmt.declaration.get())) {
        exportNames.push_back(os->name);
    }
}

void Compiler::visitDeferStmt(const DeferStmt& stmt) {
    // Compile the deferred expression as a 0-argument function prototype.
    // Store it so that at return points we emit OP_CLOSURE + OP_CALL for
    // each deferred prototype in LIFO order.

    Compiler fnCompiler(errorReporter_);
    fnCompiler.enclosing = this;
    fnCompiler.chunk.source = chunk.source;

    fnCompiler.beginScope();

    // Compile the expression as the body of a 0-arg function
    stmt.expression->accept(fnCompiler);

    // Implicit return of the expression value
    fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

    fnCompiler.endScope();

    // Capture upvalues BEFORE moving from fnCompiler
    auto capturedUpvalues = fnCompiler.upvalues;

    // Build function prototype (0 args, 0 required)
    FunctionPrototype proto;
    proto.name = "<defer>";
    proto.arity = 0;
    proto.requiredArity = 0;
    proto.hasRest = false;
    proto.upvalues = std::move(fnCompiler.upvalues);
    proto.chunk = std::move(fnCompiler.chunk);
    proto.isGenerator = fnCompiler.isGenerator;

    uint8_t protoIndex = addFunctionPrototype(std::move(proto));

    // Record for emission at return points (LIFO order via reverse iteration)
    deferredProtos.push_back({protoIndex, std::move(capturedUpvalues)});
}

} // namespace vora
