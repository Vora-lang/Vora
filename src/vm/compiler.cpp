#include "compiler.h"

#include <cmath>
#include <iostream>
#include <variant>

#include "../gc/gc_heap.h"
#include "../runtime/vora_function.h"

namespace vora {

// =========================================================================
// Helpers: bytecode emission
// =========================================================================

void Compiler::emitByte(uint8_t byte) {
    chunk.write(byte, currentLine, currentColumn);
}

void Compiler::emitBytes(uint8_t a, uint8_t b) {
    chunk.write(a, b, currentLine, currentColumn);
}

void Compiler::emitConstant(Value value) {
    chunk.writeConstant(value, currentLine, currentColumn);
}

size_t Compiler::emitJump(OpCode instruction) {
    emitByte(static_cast<uint8_t>(instruction));
    size_t offset = chunk.code.size();
    emitByte(0xFF);
    emitByte(0xFF);
    return offset;
}

void Compiler::patchJump(size_t operandOffset) {
    if (hadError) return;
    size_t jumpEnd = chunk.code.size();
    size_t jumpSize = jumpEnd - operandOffset - 2;

    if (jumpSize > UINT16_MAX) {
        error("Jump offset " + std::to_string(jumpSize) +
              " exceeds 16-bit limit (max " + std::to_string(UINT16_MAX) +
              "). Function body too large.");
        return;
    }

    chunk.writeAt(operandOffset, static_cast<uint8_t>(jumpSize & 0xFF));
    chunk.writeAt(operandOffset + 1, static_cast<uint8_t>((jumpSize >> 8) & 0xFF));
}

void Compiler::emitLoop(size_t loopStart) {
    if (hadError) return;
    emitByte(static_cast<uint8_t>(OpCode::OP_LOOP));

    size_t offset = chunk.code.size() - loopStart + 2;
    if (offset > UINT16_MAX) {
        error("Loop offset " + std::to_string(offset) +
              " exceeds 16-bit limit (max " + std::to_string(UINT16_MAX) +
              "). Loop body too large.");
        return;
    }

    emitByte(static_cast<uint8_t>(offset & 0xFF));
    emitByte(static_cast<uint8_t>((offset >> 8) & 0xFF));
}

uint8_t Compiler::makeConstant(Value value) {
    if (hadError) return 0;
    size_t index = chunk.addConstant(value);
    // Guard against constant pool overflow (> 256) for 8-bit operand opcodes
    // (e.g. property names, closure/class indices). OP_CONSTANT_LONG handles
    // value constants with 16-bit indices — see Chunk::writeConstant().
    if (index > UINT8_MAX) {
        error("Constant pool overflow (" + std::to_string(index) +
              " entries, max 256).");
        return 0;
    }
    return static_cast<uint8_t>(index);
}

uint8_t Compiler::identifierConstant(const std::string& name) {
    return makeConstant(GcHeap::instance().alloc<GcString>(name));
}

uint8_t Compiler::addFunctionPrototype(FunctionPrototype proto) {
    // Store as a shared_ptr in the constant pool
    auto ptr = GcHeap::instance().alloc<FunctionPrototype>(std::move(proto));
    return makeConstant(ptr);
}

void Compiler::compileExpr(const Expr& expr) {
    expr.accept(*this);
}

// =========================================================================
// Local variable management
// =========================================================================

void Compiler::beginScope() {
    scopeDepth++;
    scopeLocalCounts.push_back(0);
}

void Compiler::endScope() {
    int count = scopeLocalCounts.back();
    scopeLocalCounts.pop_back();

    if (count > 0) {
        // Emit OP_CLOSE_UPVALUE for each captured local that is going out of scope
        for (int i = 0; i < count; i++) {
            int localIdx = static_cast<int>(locals.size()) - count + i;
            if (localIdx >= 0 && locals[static_cast<size_t>(localIdx)].captured) {
                emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSE_UPVALUE),
                          static_cast<uint8_t>(localIdx));
            }
        }
        // Emit POPN to remove locals from stack
        emitBytes(static_cast<uint8_t>(OpCode::OP_POPN),
                  static_cast<uint8_t>(count));
        // Remove locals from tracking
        for (int i = 0; i < count; i++) {
            locals.pop_back();
        }
    }

    scopeDepth--;
}

void Compiler::addLocal(const std::string& name, bool isConst) {
    // Check for redefinition in current scope
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth == -1 || locals[i].depth < scopeDepth) break;
        if (locals[i].name == name) {
            error("Variable '" + name + "' already defined in this scope");
            return;
        }
    }

    locals.push_back({name, scopeDepth, false, isConst});
    scopeLocalCounts.back()++;
}

int Compiler::resolveLocal(const std::string& name) const {
    // Search from innermost to outermost
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth != -1 && locals[i].name == name) {
            return i;
        }
    }
    return -1; // Not a local
}

int Compiler::currentLocalCount() const {
    return static_cast<int>(locals.size());
}

int Compiler::resolveGlobal(const std::string& name) {
    // Walk up the enclosing compiler chain to find the root compiler.
    // All global slot assignments MUST happen in the root compiler so
    // that nested function compilers share the same numbering.
    Compiler* root = this;
    while (root->enclosing) {
        root = root->enclosing;
    }
    // Linear scan — global count is small.
    for (size_t i = 0; i < root->globalNames.size(); i++) {
        if (root->globalNames[i] == name) return static_cast<int>(i);
    }
    int slot = static_cast<int>(root->globalNames.size());
    root->globalNames.push_back(name);
    root->globalDefined.push_back(false);
    root->globalIsConst.push_back(false);
    return slot;
}

int Compiler::defineGlobal(const std::string& name) {
    // Walk to root compiler (same as resolveGlobal).
    Compiler* root = this;
    while (root->enclosing) {
        root = root->enclosing;
    }

    // Check for redefinition.
    for (size_t i = 0; i < root->globalNames.size(); i++) {
        if (root->globalNames[i] == name) {
            if (root->globalDefined[i]) {
                error("Global variable '" + name + "' already defined");
            } else {
                // Forward reference created the slot — mark as defined now.
                root->globalDefined[i] = true;
            }
            return static_cast<int>(i);
        }
    }

    // New global — allocate slot and mark defined.
    int slot = static_cast<int>(root->globalNames.size());
    root->globalNames.push_back(name);
    root->globalDefined.push_back(true);
    root->globalIsConst.push_back(false);
    return slot;
}

int Compiler::defineGlobalConst(const std::string& name) {
    // Walk to root compiler.
    Compiler* root = this;
    while (root->enclosing) {
        root = root->enclosing;
    }

    // Check for redefinition.
    for (size_t i = 0; i < root->globalNames.size(); i++) {
        if (root->globalNames[i] == name) {
            if (root->globalDefined[i]) {
                error("Global variable '" + name + "' already defined");
            } else {
                // Forward reference — mark as defined const.
                root->globalDefined[i] = true;
                root->globalIsConst[i] = true;
            }
            return static_cast<int>(i);
        }
    }

    // New const global.
    int slot = static_cast<int>(root->globalNames.size());
    root->globalNames.push_back(name);
    root->globalDefined.push_back(true);
    root->globalIsConst.push_back(true);
    return slot;
}

bool Compiler::isLocalConst(const std::string& name) const {
    int slot = resolveLocal(name);
    if (slot >= 0 && static_cast<size_t>(slot) < locals.size()) {
        return locals[static_cast<size_t>(slot)].isConst;
    }
    return false;
}

int Compiler::resolveUpvalue(Compiler* compiler, const std::string& name) {
    // Walk up the enclosing compiler chain to find the variable
    if (compiler->enclosing == nullptr) return -1;

    uint8_t resolvedIndex;
    bool resolvedIsLocal;
    bool resolvedIsConst = false;

    // Try to resolve as a local in the immediate enclosing function
    int localSlot = compiler->enclosing->resolveLocal(name);
    if (localSlot >= 0) {
        // Mark the local as captured in the enclosing function
        auto& local = compiler->enclosing->locals[static_cast<size_t>(localSlot)];
        local.captured = true;
        resolvedIsConst = local.isConst;
        resolvedIndex = static_cast<uint8_t>(localSlot);
        resolvedIsLocal = true;
    } else {
        // Try to resolve as an upvalue in the immediate enclosing function
        int upvalueIdx = compiler->enclosing->resolveUpvalue(compiler->enclosing, name);
        if (upvalueIdx >= 0) {
            resolvedIsConst = compiler->enclosing->upvalues[static_cast<size_t>(upvalueIdx)].isConst;
            resolvedIndex = static_cast<uint8_t>(upvalueIdx);
            resolvedIsLocal = false;
        } else {
            return -1;
        }
    }

    // Check if this upvalue is already captured (deduplicate)
    for (int i = 0; i < static_cast<int>(upvalues.size()); i++) {
        if (upvalues[i].index == resolvedIndex &&
            upvalues[i].isLocal == resolvedIsLocal) {
            return i;  // already captured, reuse existing index
        }
    }

    // Add new upvalue descriptor
    int idx = static_cast<int>(upvalues.size());
    upvalues.push_back({resolvedIndex, resolvedIsLocal, resolvedIsConst});
    return idx;
}

bool Compiler::isUpvalueConst(int upvalueIdx) const {
    if (upvalueIdx >= 0 && static_cast<size_t>(upvalueIdx) < upvalues.size()) {
        return upvalues[static_cast<size_t>(upvalueIdx)].isConst;
    }
    return false;
}

// =========================================================================
// Main entry: compile a Program into a Chunk
// =========================================================================

Chunk Compiler::compile(const Program* program) {
    program->accept(*this);

    // Execute deferred calls (LIFO) before implicit return
    emitDeferCalls();

    // Implicit return null at end of program
    emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

    return std::move(chunk);
}

// =========================================================================
// ProgramVisitor
// =========================================================================

void Compiler::visitProgram(const Program& program) {
    for (const auto& stmt : program.statements) {
        stmt->accept(*this);
    }
}

// =========================================================================
// StmtVisitor — core statement compilation
// =========================================================================

void Compiler::visitExprStmt(const ExprStmt& stmt) {
    stmt.expression->accept(*this);
    if (replMode) {
        // In REPL mode, print the expression result (unless it's null)
        emitByte(static_cast<uint8_t>(OpCode::OP_PRINT_POP));
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    }
}

void Compiler::visitBlockStmt(const BlockStmt& stmt) {
    // Enter new scope
    beginScope();

    for (const auto& s : stmt.statements) {
        s->accept(*this);
    }

    // Exit scope (emits OP_POPN to clean up locals)
    endScope();
}

// =========================================================================
// Error nodes — tolerate at bytecode level
// =========================================================================

void Compiler::visitErrorExpr(const ErrorExpr& /*expr*/) {
    // Emit null as a safe placeholder so the VM doesn't crash on partial ASTs.
    emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
}

void Compiler::visitErrorStmt(const ErrorStmt& /*stmt*/) {
    // No-op: error statements produce no bytecode.
}

void Compiler::emitDeferCalls() {
    // Emit calls to all deferred closures in LIFO order (reverse of registration).
    // Each defer: OP_CLOSURE <protoIndex> <upvalueCount> {<isLocal> <index>}*
    //             OP_CALL 0
    //             OP_POP  (discard the defer's return value)
    for (auto it = deferredProtos.rbegin(); it != deferredProtos.rend(); ++it) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE), it->protoIndex);
        emitByte(static_cast<uint8_t>(it->upvalues.size()));
        for (const auto& uv : it->upvalues) {
            emitByte(uv.isLocal ? 1 : 0);
            emitByte(uv.index);
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 0);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    }
}

} // namespace vora
