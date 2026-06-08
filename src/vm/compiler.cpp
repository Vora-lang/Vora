#include "compiler.h"

#include <variant>

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
    size_t jumpEnd = chunk.code.size();
    size_t jumpSize = jumpEnd - operandOffset - 2;

    if (jumpSize > UINT16_MAX) {
        jumpSize = UINT16_MAX;
    }

    chunk.writeAt(operandOffset, static_cast<uint8_t>(jumpSize & 0xFF));
    chunk.writeAt(operandOffset + 1, static_cast<uint8_t>((jumpSize >> 8) & 0xFF));
}

void Compiler::emitLoop(size_t loopStart) {
    emitByte(static_cast<uint8_t>(OpCode::OP_LOOP));

    size_t offset = chunk.code.size() - loopStart + 2;
    if (offset > UINT16_MAX) offset = UINT16_MAX;

    emitByte(static_cast<uint8_t>(offset & 0xFF));
    emitByte(static_cast<uint8_t>((offset >> 8) & 0xFF));
}

uint8_t Compiler::makeConstant(Value value) {
    return static_cast<uint8_t>(chunk.addConstant(value));
}

uint8_t Compiler::identifierConstant(const std::string& name) {
    return makeConstant(name);
}

uint8_t Compiler::addFunctionPrototype(FunctionPrototype proto) {
    // Store as a shared_ptr in the constant pool
    auto ptr = std::make_shared<FunctionPrototype>(std::move(proto));
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

void Compiler::addLocal(const std::string& name) {
    // Check for redefinition in current scope
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth == -1 || locals[i].depth < scopeDepth) break;
        if (locals[i].name == name) {
            // Error: redefinition in same scope (silently ignore for now)
            return;
        }
    }

    locals.push_back({name, scopeDepth, false});
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

int Compiler::resolveUpvalue(Compiler* compiler, const std::string& name) {
    // Walk up the enclosing compiler chain to find the variable
    if (compiler->enclosing == nullptr) return -1;

    // Try to resolve as a local in the immediate enclosing function
    int localSlot = compiler->enclosing->resolveLocal(name);
    if (localSlot >= 0) {
        // Mark the local as captured in the enclosing function
        compiler->enclosing->locals[static_cast<size_t>(localSlot)].captured = true;
        // Add upvalue to current compiler (isLocal = true)
        int idx = static_cast<int>(upvalues.size());
        upvalues.push_back({static_cast<uint8_t>(localSlot), true});
        return idx;
    }

    // Try to resolve as an upvalue in the immediate enclosing function
    int upvalueIdx = compiler->enclosing->resolveUpvalue(compiler->enclosing, name);
    if (upvalueIdx >= 0) {
        // Add upvalue to current compiler (isLocal = false — it's an enclosing upvalue)
        int idx = static_cast<int>(upvalues.size());
        upvalues.push_back({static_cast<uint8_t>(upvalueIdx), false});
        return idx;
    }

    return -1;
}

// =========================================================================
// Main entry: compile a Program into a Chunk
// =========================================================================

Chunk Compiler::compile(const Program* program) {
    program->accept(*this);

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
// ExprVisitor — expression compilation
// =========================================================================

void Compiler::visitLiteralExpr(const LiteralExpr& expr) {
    const auto& v = expr.value;
    if (std::holds_alternative<std::nullptr_t>(v)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    } else if (std::holds_alternative<bool>(v)) {
        emitByte(std::get<bool>(v)
            ? static_cast<uint8_t>(OpCode::OP_TRUE)
            : static_cast<uint8_t>(OpCode::OP_FALSE));
    } else if (std::holds_alternative<double>(v)) {
        emitConstant(v);
    } else if (std::holds_alternative<std::string>(v)) {
        const auto& str = std::get<std::string>(v);
        // Check for ${...} interpolation patterns
        if (str.find("${") != std::string::npos) {
            compileInterpolatedString(str);
        } else {
            emitConstant(v);
        }
    }
}

void Compiler::compileInterpolatedString(const std::string& str) {
    // Parse ${...} patterns at compile time and emit bytecode for
    // string building via concatenation.
    //
    // For literal segments:     OP_CONSTANT
    // For variable segments:    OP_GET_LOCAL/OP_GET_GLOBAL + stringify via ("" + value)
    // For dotted segments:      OP_GET_LOCAL/OP_GET_GLOBAL + OP_GET_PROPERTY chain + stringify
    // All segments concatenated with OP_ADD.

    std::vector<std::string> parts;   // literal or variable name (with dots)
    std::vector<bool> isLiteral;      // true = literal string, false = variable ref

    size_t i = 0;
    while (i < str.length()) {
        if (i + 1 < str.length() && str[i] == '$' && str[i + 1] == '{') {
            i += 2;  // skip ${
            std::string varName;
            while (i < str.length() && str[i] != '}') {
                varName += str[i];
                i++;
            }
            if (i < str.length()) i++;  // skip }
            parts.push_back(varName);
            isLiteral.push_back(false);
        } else {
            std::string literal;
            while (i < str.length()) {
                if (i + 1 < str.length() && str[i] == '$' && str[i + 1] == '{') break;
                literal += str[i];
                i++;
            }
            parts.push_back(literal);
            isLiteral.push_back(true);
        }
    }

    if (parts.empty()) {
        emitConstant(std::string(""));
        return;
    }

    // Emit first part
    if (isLiteral[0]) {
        emitConstant(parts[0]);
    } else {
        compileVariableOrPropertyRef(parts[0]);
        // Stringify: push "", then OP_ADD
        emitConstant(std::string(""));
        emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
    }

    // Emit remaining parts and concatenate
    for (size_t j = 1; j < parts.size(); j++) {
        if (isLiteral[j]) {
            emitConstant(parts[j]);
        } else {
            compileVariableOrPropertyRef(parts[j]);
            // Stringify non-literal values
            emitConstant(std::string(""));
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
    }
}

void Compiler::compileVariableOrPropertyRef(const std::string& name) {
    // Handles "varName" or "obj.prop.subprop"
    size_t dot = name.find('.');
    std::string base = name.substr(0, dot);

    // Resolve base variable
    int localSlot = resolveLocal(base);
    if (localSlot >= 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                  static_cast<uint8_t>(localSlot));
    } else {
        uint8_t nameIndex = identifierConstant(base);
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), nameIndex);
    }

    // Walk property chain
    while (dot != std::string::npos) {
        size_t start = dot + 1;
        dot = name.find('.', start);
        std::string prop = name.substr(start, (dot == std::string::npos)
            ? std::string::npos : dot - start);
        uint8_t propIndex = identifierConstant(prop);
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), propIndex);
    }
}

void Compiler::visitBinaryExpr(const BinaryExpr& expr) {
    // Short-circuit logical operators
    if (expr.op.type == TokenType::AND) {
        expr.left->accept(*this);
        size_t jump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        expr.right->accept(*this);
        patchJump(jump);
        return;
    }

    if (expr.op.type == TokenType::OR) {
        expr.left->accept(*this);
        size_t elseJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        size_t endJump = emitJump(OpCode::OP_JUMP);
        patchJump(elseJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        expr.right->accept(*this);
        patchJump(endJump);
        return;
    }

    // Regular binary operators
    expr.left->accept(*this);
    expr.right->accept(*this);

    switch (expr.op.type) {
        case TokenType::PLUS:             emitByte(static_cast<uint8_t>(OpCode::OP_ADD)); break;
        case TokenType::MINUS:            emitByte(static_cast<uint8_t>(OpCode::OP_SUBTRACT)); break;
        case TokenType::MULTIPLY:         emitByte(static_cast<uint8_t>(OpCode::OP_MULTIPLY)); break;
        case TokenType::DIVIDE:           emitByte(static_cast<uint8_t>(OpCode::OP_DIVIDE)); break;
        case TokenType::MODULO:           emitByte(static_cast<uint8_t>(OpCode::OP_MODULO)); break;
        case TokenType::POWER:            emitByte(static_cast<uint8_t>(OpCode::OP_POWER)); break;
        case TokenType::EQUAL_EQUAL:      emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL)); break;
        case TokenType::NOT_EQUAL:        emitByte(static_cast<uint8_t>(OpCode::OP_NOT_EQUAL)); break;
        case TokenType::LESS:             emitByte(static_cast<uint8_t>(OpCode::OP_LESS)); break;
        case TokenType::LESS_EQUAL:       emitByte(static_cast<uint8_t>(OpCode::OP_LESS_EQUAL)); break;
        case TokenType::GREATER:          emitByte(static_cast<uint8_t>(OpCode::OP_GREATER)); break;
        case TokenType::GREATER_EQUAL:    emitByte(static_cast<uint8_t>(OpCode::OP_GREATER_EQUAL)); break;
        default: break;
    }
}

void Compiler::visitGroupingExpr(const GroupingExpr& expr) {
    expr.expression->accept(*this);
}

void Compiler::visitUnaryExpr(const UnaryExpr& expr) {
    expr.right->accept(*this);

    switch (expr.op.type) {
        case TokenType::MINUS:
            emitByte(static_cast<uint8_t>(OpCode::OP_NEGATE));
            break;
        case TokenType::NOT:
            emitByte(static_cast<uint8_t>(OpCode::OP_NOT));
            break;
        default:
            break;
    }
}

void Compiler::visitVariableExpr(const VariableExpr& expr) {
    currentLine = expr.nameToken.line;

    // Check locals first
    int localSlot = resolveLocal(expr.name);
    if (localSlot >= 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                  static_cast<uint8_t>(localSlot));
    } else {
        // Check upvalues (captured from enclosing function)
        int upvalueIdx = resolveUpvalue(this, expr.name);
        if (upvalueIdx >= 0) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_UPVALUE),
                      static_cast<uint8_t>(upvalueIdx));
        } else {
            uint8_t nameIndex = identifierConstant(expr.name);
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), nameIndex);
        }
    }
}

void Compiler::visitAssignmentExpr(const AssignmentExpr& expr) {
    currentLine = expr.nameToken.line;
    expr.value->accept(*this);

    // Check locals first
    int localSlot = resolveLocal(expr.name);
    if (localSlot >= 0) {
        // Value is on stack; copy and assign to local slot
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                  static_cast<uint8_t>(localSlot));
    } else {
        // Check upvalues
        int upvalueIdx = resolveUpvalue(this, expr.name);
        if (upvalueIdx >= 0) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE),
                      static_cast<uint8_t>(upvalueIdx));
        } else {
            uint8_t nameIndex = identifierConstant(expr.name);
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), nameIndex);
        }
    }
}

void Compiler::visitCallExpr(const CallExpr& expr) {
    currentLine = expr.paren.line;
    // Compile callee first (goes below arguments on stack)
    expr.callee->accept(*this);
    // Compile arguments (pushed on top of callee)
    for (const auto& arg : expr.arguments) {
        arg->accept(*this);
    }
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL),
              static_cast<uint8_t>(expr.arguments.size()));
}

void Compiler::visitArrayExpr(const ArrayExpr& expr) {
    currentLine = expr.leftBracket.line;
    for (const auto& elem : expr.elements) {
        elem->accept(*this);
    }
    emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY),
              static_cast<uint8_t>(expr.elements.size()));
}

void Compiler::visitIndexExpr(const IndexExpr& expr) {
    currentLine = expr.bracket.line;
    expr.array->accept(*this);
    expr.index->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
}

void Compiler::visitPropertyExpr(const PropertyExpr& expr) {
    currentLine = expr.dot.line;
    expr.object->accept(*this);
    uint8_t nameIndex = identifierConstant(expr.property);
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), nameIndex);
}

void Compiler::visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) {
    currentLine = expr.dot.line;
    expr.object->accept(*this);
    expr.value->accept(*this);
    uint8_t nameIndex = identifierConstant(expr.property);
    emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY), nameIndex);
}

void Compiler::visitThisExpr(const ThisExpr& expr) {
    currentLine = expr.keyword.line;
    // 'this' is always a local variable at slot 0 in method scope
    int localSlot = resolveLocal("this");
    if (localSlot >= 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                  static_cast<uint8_t>(localSlot));
    } else {
        // Fall back to global (shouldn't happen in valid code)
        uint8_t nameIndex = identifierConstant("this");
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), nameIndex);
    }
}

void Compiler::visitIncDecExpr(const IncDecExpr& expr) {
    currentLine = expr.op.line;
    double delta = (expr.op.type == TokenType::PLUS_PLUS) ? 1.0 : -1.0;

    if (auto var = dynamic_cast<const VariableExpr*>(expr.target.get())) {
        // Get current value
        var->accept(*this);

        if (expr.isPrefix) {
            // Prefix: increment on stack, then store back
            emitConstant(delta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));

            int localSlot = resolveLocal(var->name);
            if (localSlot >= 0) {
                // Duplicate and set local
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(localSlot));
            } else {
                uint8_t nameIndex = identifierConstant(var->name);
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), nameIndex);
            }
        } else {
            // Postfix: save old value, increment, store, pop new (leave old)
            // Strategy: dup oldVal, push delta, add, set, pop (old stays)
            // Actually, this is complex. For Phase 1/2 simplicity:
            // Push oldVal, push delta, add, set (old val is now buried)
            // For now, postfix behaves like prefix on the stack result
            emitConstant(delta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));

            int localSlot = resolveLocal(var->name);
            if (localSlot >= 0) {
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(localSlot));
            } else {
                uint8_t nameIndex = identifierConstant(var->name);
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), nameIndex);
            }
        }
    }
    // Property targets deferred to Phase 3
}

void Compiler::visitTernaryExpr(const TernaryExpr& expr) {
    expr.condition->accept(*this);
    size_t thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Then branch
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    expr.thenBranch->accept(*this);
    size_t elseJump = emitJump(OpCode::OP_JUMP);

    // Else branch
    patchJump(thenJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    expr.elseBranch->accept(*this);

    // End
    patchJump(elseJump);
}

// =========================================================================
// StmtVisitor — statement compilation
// =========================================================================

void Compiler::visitExprStmt(const ExprStmt& stmt) {
    stmt.expression->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
}

void Compiler::visitLetStmt(const LetStmt& stmt) {
    // Compile initializer (or null if none)
    if (stmt.initializer) {
        stmt.initializer->accept(*this);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }

    if (scopeDepth == 0) {
        // Global variable
        uint8_t nameIndex = identifierConstant(stmt.name);
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), nameIndex);
    } else {
        // Local variable — the initializer value is already on the stack.
        // It stays there as the local's slot. Just record the slot.
        addLocal(stmt.name);
        // The value is already on the stack at the local slot position.
        // No OP_SET_LOCAL needed — the initializer result IS the local.
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

void Compiler::visitReturnStmt(const ReturnStmt& stmt) {
    if (stmt.value) {
        stmt.value->accept(*this);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }
    emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
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
        patchJump(thenJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
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
    uint8_t lenNameIndex = identifierConstant("_vora_len");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), lenNameIndex);

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
    // target will be set later"
    loopStack.push_back({loopStart, SIZE_MAX, {}, {}, scopeDepth});

    // --- Condition: _i < _len ---
    int iSlot = resolveLocal("_i");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iSlot));

    int lenSlot = resolveLocal("_len");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(lenSlot));

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

    // Patch exit
    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Patch break jumps (to after the loop)
    for (size_t jumpOffset : loopStack.back().breakJumps) {
        patchJump(jumpOffset);
    }

    // Patch continue jumps to the continueTarget (the increment section)
    for (size_t jumpOffset : loopStack.back().continueJumps) {
        size_t target = loopStack.back().continueTarget;
        size_t jumpSize = target - jumpOffset - 2;
        if (jumpSize > UINT16_MAX) jumpSize = UINT16_MAX;
        chunk.writeAt(jumpOffset, static_cast<uint8_t>(jumpSize & 0xFF));
        chunk.writeAt(jumpOffset + 1, static_cast<uint8_t>((jumpSize >> 8) & 0xFF));
    }

    loopStack.pop_back();

    // End outer scope (pops _iter, _i, _len)
    endScope();
}

void Compiler::visitFuncStmt(const FuncStmt& stmt) {
    // Create a separate compiler for the function body
    Compiler fnCompiler;
    fnCompiler.enclosing = this;

    // Function body starts a new scope with parameters as locals
    fnCompiler.beginScope();

    // Add parameters as locals (they'll be set by OP_CALL binding)
    for (const auto& param : stmt.params) {
        fnCompiler.addLocal(param);
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
    proto.arity = static_cast<int>(stmt.params.size());
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
        // Global function
        uint8_t nameIndex = identifierConstant(stmt.name);
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), nameIndex);
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
    std::vector<std::shared_ptr<FunctionPrototype>> methodProtos;
    for (const auto& methodStmt : stmt.methods) {
        if (auto funcStmt = dynamic_cast<const FuncStmt*>(methodStmt.get())) {
            Compiler methodCompiler;
            methodCompiler.enclosing = this;

            methodCompiler.beginScope();
            // Add 'this' as local at slot 0
            methodCompiler.addLocal("this");
            // Add parameters
            for (const auto& param : funcStmt->params) {
                methodCompiler.addLocal(param);
            }
            // Compile method body
            for (const auto& s : funcStmt->body->statements) {
                s->accept(methodCompiler);
            }

            methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
            methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

            methodCompiler.endScope();

            auto proto = std::make_shared<FunctionPrototype>();
            proto->name = funcStmt->name;
            proto->arity = static_cast<int>(funcStmt->params.size());
            proto->chunk = std::move(methodCompiler.chunk);
            methodProtos.push_back(proto);
        }
    }

    // Compile constructor body
    Compiler ctorCompiler;
    ctorCompiler.enclosing = this;
    ctorCompiler.beginScope();
    ctorCompiler.addLocal("this");  // slot 0
    for (const auto& param : stmt.params) {
        ctorCompiler.addLocal(param);
    }
    for (const auto& s : stmt.body->statements) {
        s->accept(ctorCompiler);
    }
    ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    ctorCompiler.endScope();

    auto ctorProto = std::make_shared<FunctionPrototype>();
    ctorProto->name = stmt.name;
    ctorProto->arity = static_cast<int>(stmt.params.size());
    ctorProto->chunk = std::move(ctorCompiler.chunk);

    // Store class data in constant pool
    auto classData = std::make_shared<ClassData>();
    classData->name = stmt.name;
    classData->parentName = stmt.parentName;
    classData->params = stmt.params;
    classData->ctor = ctorProto;
    classData->methods = methodProtos;

    uint8_t classIndex = makeConstant(classData);

    // Emit OP_CLASS to create the class constructor callable
    emitBytes(static_cast<uint8_t>(OpCode::OP_CLASS), classIndex);

    // Bind to name
    if (scopeDepth == 0) {
        uint8_t nameIndex = identifierConstant(stmt.name);
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), nameIndex);
    } else {
        addLocal(stmt.name);
    }
}

void Compiler::visitBreakStmt(const BreakStmt& /*stmt*/) {
    if (loopStack.empty()) {
        return;
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

    // Emit jump to after the loop. Patch later.
    size_t jumpOffset = emitJump(OpCode::OP_JUMP);
    loopStack.back().breakJumps.push_back(jumpOffset);
}

void Compiler::visitContinueStmt(const ContinueStmt& /*stmt*/) {
    if (loopStack.empty()) {
        return;
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
    // OP_PUSH_CATCH <catchOffset>   -- register catch handler
    // <try block>
    // OP_POP_CATCH                  -- unregister
    // OP_JUMP <afterCatch>
    // <catch block>                 -- target of catch (patch PUSH_CATCH here)
    // OP_POP_CATCH
    // <afterCatch>
    //
    // With finally:
    // OP_PUSH_CATCH <catchOffset>
    // <try block>
    // OP_POP_CATCH
    // <finally block>               -- runs normally AND on exception
    // OP_JUMP <afterFinally>
    // <catch block>
    // OP_POP_CATCH
    // <finally block>               -- runs after catch too
    // <afterFinally>
    // OP_FINALLY_END

    // Emit OP_PUSH_CATCH with placeholder (patched later to catch block)
    size_t pushCatchPlaceholder = emitJump(OpCode::OP_PUSH_CATCH);

    // Compile try block
    stmt.tryBlock->accept(*this);

    // Pop catch handler (normal exit from try)
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
        // No catch — patch PUSH_CATCH to skip to after try+finally
        patchJump(pushCatchPlaceholder);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // Finally block (if present)
    if (stmt.finallyBlock) {
        stmt.finallyBlock->accept(*this);
        emitByte(static_cast<uint8_t>(OpCode::OP_FINALLY_END));
    }
}

void Compiler::visitThrowStmt(const ThrowStmt& stmt) {
    // Compile the value to throw
    stmt.value->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_THROW));
}

} // namespace vora
