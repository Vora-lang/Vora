#include "compiler.h"

#include <variant>

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
    // Placeholder: 2 bytes 0xFF 0xFF
    size_t offset = chunk.code.size();
    emitByte(0xFF);
    emitByte(0xFF);
    return offset;
}

void Compiler::patchJump(size_t operandOffset) {
    // Jump offset = bytes from after the 2-byte operand to current code end
    size_t jumpEnd = chunk.code.size();
    size_t jumpSize = jumpEnd - operandOffset - 2;

    if (jumpSize > UINT16_MAX) {
        // Jump too far — would need long jump support
        jumpSize = UINT16_MAX;
    }

    // Write little-endian: low byte first
    chunk.writeAt(operandOffset, static_cast<uint8_t>(jumpSize & 0xFF));
    chunk.writeAt(operandOffset + 1, static_cast<uint8_t>((jumpSize >> 8) & 0xFF));
}

void Compiler::emitLoop(size_t loopStart) {
    emitByte(static_cast<uint8_t>(OpCode::OP_LOOP));

    // Backward offset: from loopStart to current code end + 3 (past OP_LOOP + 2-byte operand)
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

void Compiler::compileExpr(const Expr& expr) {
    expr.accept(*this);
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
        // String literals: emit the raw string (no interpolation in VM Phase 1)
        emitConstant(v);
    }
}

void Compiler::visitBinaryExpr(const BinaryExpr& expr) {
    // Short-circuit logical operators
    if (expr.op.type == TokenType::AND) {
        // a && b: compile a; if falsy, jump over b (keeping a on stack)
        expr.left->accept(*this);
        size_t jump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // pop truthy left
        expr.right->accept(*this);
        patchJump(jump);
        return;
    }

    if (expr.op.type == TokenType::OR) {
        // a || b: compile a; if truthy, jump over b (keeping a on stack)
        expr.left->accept(*this);
        size_t elseJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        // Left is truthy — skip right
        size_t endJump = emitJump(OpCode::OP_JUMP);
        patchJump(elseJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // pop falsy left
        expr.right->accept(*this);
        patchJump(endJump);
        return;
    }

    // Regular binary operators: compile both sides, then emit opcode
    expr.left->accept(*this);
    expr.right->accept(*this);

    switch (expr.op.type) {
        case TokenType::PLUS:     emitByte(static_cast<uint8_t>(OpCode::OP_ADD)); break;
        case TokenType::MINUS:    emitByte(static_cast<uint8_t>(OpCode::OP_SUBTRACT)); break;
        case TokenType::MULTIPLY: emitByte(static_cast<uint8_t>(OpCode::OP_MULTIPLY)); break;
        case TokenType::DIVIDE:   emitByte(static_cast<uint8_t>(OpCode::OP_DIVIDE)); break;
        case TokenType::MODULO:   emitByte(static_cast<uint8_t>(OpCode::OP_MODULO)); break;
        case TokenType::POWER:    emitByte(static_cast<uint8_t>(OpCode::OP_POWER)); break;
        case TokenType::EQUAL_EQUAL:     emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL)); break;
        case TokenType::NOT_EQUAL:       emitByte(static_cast<uint8_t>(OpCode::OP_NOT_EQUAL)); break;
        case TokenType::LESS:            emitByte(static_cast<uint8_t>(OpCode::OP_LESS)); break;
        case TokenType::LESS_EQUAL:      emitByte(static_cast<uint8_t>(OpCode::OP_LESS_EQUAL)); break;
        case TokenType::GREATER:         emitByte(static_cast<uint8_t>(OpCode::OP_GREATER)); break;
        case TokenType::GREATER_EQUAL:   emitByte(static_cast<uint8_t>(OpCode::OP_GREATER_EQUAL)); break;
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
    uint8_t nameIndex = identifierConstant(expr.name);
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), nameIndex);
}

void Compiler::visitAssignmentExpr(const AssignmentExpr& expr) {
    currentLine = expr.nameToken.line;
    expr.value->accept(*this);
    uint8_t nameIndex = identifierConstant(expr.name);
    emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), nameIndex);
}

void Compiler::visitCallExpr(const CallExpr& expr) {
    currentLine = expr.paren.line;
    // Compile callee first (goes below arguments on stack)
    expr.callee->accept(*this);
    // Compile arguments (pushed on top of callee)
    for (const auto& arg : expr.arguments) {
        arg->accept(*this);
    }
    // Emit call — VM pops arguments in reverse order, then pops callee
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
    uint8_t nameIndex = identifierConstant("this");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), nameIndex);
}

void Compiler::visitIncDecExpr(const IncDecExpr& expr) {
    currentLine = expr.op.line;
    double delta = (expr.op.type == TokenType::PLUS_PLUS) ? 1.0 : -1.0;

    // Phase 1: only supports VariableExpr targets (global variables)
    if (auto var = dynamic_cast<const VariableExpr*>(expr.target.get())) {
        // Get current value
        var->accept(*this);
        if (expr.isPrefix) {
            // Prefix: increment, then leave result on stack
            emitConstant(delta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            // Duplicate for assignment
            // Store: we need the new value on stack AND assigned to global
            // For simplicity: compile as newVal = oldVal + delta; push newVal
            // But we need to also set the global...
            // For Phase 1: push oldVal, push delta, add, dup, set_global
            // Actually: GET_GLOBAL, push delta, ADD, SET_GLOBAL (SET leaves value)
            uint8_t nameIndex = identifierConstant(var->name);
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), nameIndex);
        } else {
            // Postfix: leave old value on stack, then assign incremented value
            // Push oldVal, push 1, ADD, SET_GLOBAL, POP (leaving old val)...
            // This is complex. For Phase 1 simplicity: treat as prefix
            emitConstant(delta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            uint8_t nameIndex = identifierConstant(var->name);
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), nameIndex);
        }
    }
    // Property targets deferred to Phase 3
}

void Compiler::visitTernaryExpr(const TernaryExpr& expr) {
    // Compile condition, jump if false to else branch
    expr.condition->accept(*this);
    size_t thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Then branch
    emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // pop condition
    expr.thenBranch->accept(*this);
    size_t elseJump = emitJump(OpCode::OP_JUMP);

    // Else branch
    patchJump(thenJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // pop condition
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
    uint8_t nameIndex = identifierConstant(stmt.name);
    emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), nameIndex);
}

void Compiler::visitBlockStmt(const BlockStmt& stmt) {
    // Phase 1: no local scoping. Compile statements sequentially.
    for (const auto& s : stmt.statements) {
        s->accept(*this);
    }
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
    currentLine = stmt.condition ? 0 : 0; // will be set by condition tokens

    // Compile condition
    stmt.condition->accept(*this);
    size_t thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Then branch
    emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // pop condition
    stmt.thenBranch->accept(*this);

    if (stmt.elseBranch) {
        size_t elseJump = emitJump(OpCode::OP_JUMP);
        patchJump(thenJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // pop condition
        stmt.elseBranch->accept(*this);
        patchJump(elseJump);
    } else {
        patchJump(thenJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // pop condition when no else
    }
}

void Compiler::visitWhileStmt(const WhileStmt& stmt) {
    size_t loopStart = chunk.code.size();

    // Condition
    stmt.condition->accept(*this);
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Body
    emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // pop condition
    stmt.body->accept(*this);

    // Loop back
    emitLoop(loopStart);

    // Patch exit
    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // pop condition on exit
}

void Compiler::visitForStmt(const ForStmt& /*stmt*/) {
    // Deferred to Phase 2
    emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
}

void Compiler::visitFuncStmt(const FuncStmt& /*stmt*/) {
    // Deferred to Phase 2
    emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
}

void Compiler::visitObjStmt(const ObjStmt& /*stmt*/) {
    // Deferred to Phase 3
    emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
}

void Compiler::visitBreakStmt(const BreakStmt& /*stmt*/) {
    // Deferred to Phase 2
}

void Compiler::visitContinueStmt(const ContinueStmt& /*stmt*/) {
    // Deferred to Phase 2
}

void Compiler::visitTryStmt(const TryStmt& /*stmt*/) {
    // Deferred to Phase 2
    emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
}

void Compiler::visitThrowStmt(const ThrowStmt& /*stmt*/) {
    // Deferred to Phase 2
}

} // namespace vora
