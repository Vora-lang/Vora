#include "compiler.h"

#include <cmath>
#include <iostream>
#include <variant>

#include "../ast/param_decl.h"
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

void Compiler::emitShort(uint16_t value) {
    chunk.write(static_cast<uint8_t>(value & 0xFF),
                static_cast<uint8_t>((value >> 8) & 0xFF),
                currentLine, currentColumn);
}

void Compiler::emitGetGlobal(int slot) {
    if (slot <= 255) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                  static_cast<uint8_t>(slot));
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL_WIDE));
        emitShort(static_cast<uint16_t>(slot));
    }
}

void Compiler::emitSetGlobal(int slot) {
    if (slot <= 255) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL),
                  static_cast<uint8_t>(slot));
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL_WIDE));
        emitShort(static_cast<uint16_t>(slot));
    }
}

void Compiler::emitDefineGlobal(int slot) {
    if (slot <= 255) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                  static_cast<uint8_t>(slot));
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL_WIDE));
        emitShort(static_cast<uint16_t>(slot));
    }
}

void Compiler::emitConvert(const std::string& typeAnnotation) {
    // Map type annotation string to OP_CONVERT type tag.
    // Supported annotations: float, int, bool, str, string
    uint8_t tag;
    if (typeAnnotation == "float") {
        tag = 0;
    } else if (typeAnnotation == "int") {
        tag = 1;
    } else if (typeAnnotation == "bool") {
        tag = 2;
    } else if (typeAnnotation == "str" || typeAnnotation == "string") {
        tag = 3;
    } else {
        // Unknown type annotation — no conversion emitted.
        return;
    }
    emitBytes(static_cast<uint8_t>(OpCode::OP_CONVERT), tag);
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

size_t Compiler::makeConstant(Value value) {
    if (hadError) return 0;
    return chunk.addConstant(value);
}

size_t Compiler::identifierConstant(const std::string& name) {
    return makeConstant(GcHeap::instance().alloc<GcString>(name));
}

size_t Compiler::addFunctionPrototype(FunctionPrototype proto) {
    auto ptr = GcHeap::instance().alloc<FunctionPrototype>(std::move(proto));
    return makeConstant(ptr);
}

// ── Wide-aware emitter helpers ───────────────────────────────────────────────

static bool needsWide(size_t index) { return index > UINT8_MAX; }

void Compiler::emitGetProperty(size_t nameIndex) {
    if (needsWide(nameIndex)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY_WIDE));
        emitShort(static_cast<uint16_t>(nameIndex));
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY),
                  static_cast<uint8_t>(nameIndex));
    }
}

void Compiler::emitGetPropertySafe(size_t nameIndex) {
    if (needsWide(nameIndex)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY_SAFE_WIDE));
        emitShort(static_cast<uint16_t>(nameIndex));
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY_SAFE),
                  static_cast<uint8_t>(nameIndex));
    }
}

void Compiler::emitSetProperty(size_t nameIndex) {
    if (needsWide(nameIndex)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY_WIDE));
        emitShort(static_cast<uint16_t>(nameIndex));
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY),
                  static_cast<uint8_t>(nameIndex));
    }
}

void Compiler::emitGetLocalProp(int localSlot, size_t nameIndex) {
    if (needsWide(nameIndex)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_GET_LOCAL_PROP_WIDE));
        emitByte(static_cast<uint8_t>(localSlot));
        emitShort(static_cast<uint16_t>(nameIndex));
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_GET_LOCAL_PROP));
        emitByte(static_cast<uint8_t>(localSlot));
        emitByte(static_cast<uint8_t>(nameIndex));
    }
}

void Compiler::emitGetGlobalProp(int globalSlot, size_t nameIndex) {
    if (needsWide(globalSlot)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL_PROP_WIDE));
        emitShort(static_cast<uint16_t>(globalSlot));
        emitByte(static_cast<uint8_t>(nameIndex));
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL_PROP));
        emitByte(static_cast<uint8_t>(globalSlot));
        emitByte(static_cast<uint8_t>(nameIndex));
    }
}

void Compiler::emitGetSuper(size_t nameIndex) {
    if (needsWide(nameIndex)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_GET_SUPER_WIDE));
        emitShort(static_cast<uint16_t>(nameIndex));
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_SUPER),
                  static_cast<uint8_t>(nameIndex));
    }
}

void Compiler::emitClosure(size_t protoIndex,
                           const std::vector<UpvalueDescriptor>& upvalues) {
    if (needsWide(protoIndex)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_CLOSURE_WIDE));
        emitShort(static_cast<uint16_t>(protoIndex));
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE),
                  static_cast<uint8_t>(protoIndex));
    }
    emitByte(static_cast<uint8_t>(upvalues.size()));
    for (auto& uv : upvalues) {
        emitByte(static_cast<uint8_t>(uv.isLocal ? 1 : 0));
        emitByte(uv.index);
    }
}

void Compiler::emitClass(size_t classIndex, uint8_t /*methodCount*/) {
    (void)0; // methodCount is stored in ClassDefinition::methodProtos
    if (needsWide(classIndex)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_CLASS_WIDE));
        emitShort(static_cast<uint16_t>(classIndex));
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::OP_CLASS),
                  static_cast<uint8_t>(classIndex));
    }
}

void Compiler::emitImport(size_t pathIndex, size_t nameIndex) {
    if (needsWide(pathIndex) || needsWide(nameIndex)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_IMPORT_WIDE));
        emitShort(static_cast<uint16_t>(pathIndex));
        emitShort(static_cast<uint16_t>(nameIndex));
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_IMPORT));
        emitByte(static_cast<uint8_t>(pathIndex));
        emitByte(static_cast<uint8_t>(nameIndex));
    }
}

void Compiler::emitGetGlobalSafe(int slot, size_t fallbackIndex) {
    if (slot > UINT8_MAX || needsWide(fallbackIndex)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL_SAFE_WIDE));
        emitShort(static_cast<uint16_t>(slot));
        emitShort(static_cast<uint16_t>(fallbackIndex));
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL_SAFE),
                  static_cast<uint8_t>(slot));
        emitByte(static_cast<uint8_t>(fallbackIndex));
    }
}

void Compiler::emitCallKw(uint8_t posCount, uint8_t kwCount,
                          const std::vector<size_t>& kwNameIndices) {
    bool hasWide = false;
    for (auto idx : kwNameIndices) {
        if (needsWide(idx)) { hasWide = true; break; }
    }
    if (hasWide) {
        emitByte(static_cast<uint8_t>(OpCode::OP_CALL_KW_WIDE));
        emitByte(posCount);
        emitByte(kwCount);
        for (auto idx : kwNameIndices) {
            emitShort(static_cast<uint16_t>(idx));
        }
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_CALL_KW));
        emitByte(posCount);
        emitByte(kwCount);
        for (auto idx : kwNameIndices) {
            emitByte(static_cast<uint8_t>(idx));
        }
    }
}

void Compiler::compileExpr(const Expr& expr) {
    if (++recursionDepth_ > MAX_COMPILE_DEPTH) {
        error("AST too deeply nested (max " +
              std::to_string(MAX_COMPILE_DEPTH) + " levels)");
        recursionDepth_--;
        return;
    }
    expr.accept(*this);
    recursionDepth_--;
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
    emitDeferFlush();

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

void Compiler::emitDeferFlush() {
    // Emit OP_DEFER_FLUSH if this function has any defer statements.
    // At runtime, this calls all closures on the frame's defer stack (LIFO),
    // discarding return values. If the defer stack is empty, it's a no-op.
    if (deferCount_ > 0) {
        emitByte(static_cast<uint8_t>(OpCode::OP_DEFER_FLUSH));
    }
}

void Compiler::compileDefaultParamPreamble(Compiler& child,
                                           const std::vector<ParamDecl>& params,
                                           int slotBase, int requiredArity,
                                           int totalArity) {
    for (int i = requiredArity; i < totalArity; i++) {
        const auto& param = params[static_cast<size_t>(i)];
        int slot = i + slotBase;

        // OP_DEFAULT_PARAM <slot> <skipOffset16>
        // At runtime: if slot < actualArgCount, skip the default evaluation.
        child.emitByte(static_cast<uint8_t>(OpCode::OP_DEFAULT_PARAM));
        child.emitByte(static_cast<uint8_t>(slot));
        size_t skipPos = child.chunk.code.size();
        child.emitByte(0xFF);  // placeholder skip offset (low)
        child.emitByte(0xFF);  // placeholder skip offset (high)

        // Compile the default value expression (result on stack)
        param.defaultValue->accept(child);

        // Store into the local slot and pop leftover
        child.emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                        static_cast<uint8_t>(slot));
        child.emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        // Patch the skip offset to jump over the default eval code
        if (!child.hadError) {
            size_t jumpEnd = child.chunk.code.size();
            size_t offset = jumpEnd - skipPos - 2;
            if (offset > 0xFFFF) {
                // Jump offset overflow
                child.hadError = true;
            } else {
                child.chunk.code[skipPos] =
                    static_cast<uint8_t>(offset & 0xFF);
                child.chunk.code[skipPos + 1] =
                    static_cast<uint8_t>((offset >> 8) & 0xFF);
            }
        }
    }
}

} // namespace vora
