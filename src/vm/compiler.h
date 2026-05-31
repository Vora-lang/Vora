#pragma once

#include <cstdint>
#include <string>

#include "chunk.h"
#include "opcode.h"

#include "../ast/expr.h"
#include "../ast/expr_visitor.h"
#include "../ast/program.h"
#include "../ast/stmt.h"
#include "../ast/stmt_visitor.h"
#include "../lexer/token.h"
#include "../runtime/value.h"

namespace vora {

// Compiler: AST → bytecode (side-effectful emission into a Chunk).
// Implements ExprVisitor<void> + StmtVisitor<void> + ProgramVisitor<void>.
//
// Phase 1 supports: expressions, global variables, if/else, while,
// ternary, arrays, indexing, native function calls (print, clock, etc.).
// Deferred to Phase 2+: functions/closures, local variables, objects,
// break/continue, for-in, try/catch/throw.
class Compiler : public ExprVisitor<void>,
                 public StmtVisitor<void>,
                 public ProgramVisitor<void> {
public:
    Chunk compile(const Program* program);

    // --- ProgramVisitor ---
    void visitProgram(const Program& program) override;

    // --- ExprVisitor ---
    void visitLiteralExpr(const LiteralExpr& expr) override;
    void visitBinaryExpr(const BinaryExpr& expr) override;
    void visitGroupingExpr(const GroupingExpr& expr) override;
    void visitUnaryExpr(const UnaryExpr& expr) override;
    void visitVariableExpr(const VariableExpr& expr) override;
    void visitAssignmentExpr(const AssignmentExpr& expr) override;
    void visitCallExpr(const CallExpr& expr) override;
    void visitArrayExpr(const ArrayExpr& expr) override;
    void visitIndexExpr(const IndexExpr& expr) override;
    void visitPropertyExpr(const PropertyExpr& expr) override;
    void visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) override;
    void visitThisExpr(const ThisExpr& expr) override;
    void visitIncDecExpr(const IncDecExpr& expr) override;
    void visitTernaryExpr(const TernaryExpr& expr) override;

    // --- StmtVisitor ---
    void visitExprStmt(const ExprStmt& stmt) override;
    void visitLetStmt(const LetStmt& stmt) override;
    void visitBlockStmt(const BlockStmt& stmt) override;
    void visitReturnStmt(const ReturnStmt& stmt) override;
    void visitIfStmt(const IfStmt& stmt) override;
    void visitWhileStmt(const WhileStmt& stmt) override;
    void visitForStmt(const ForStmt& stmt) override;
    void visitFuncStmt(const FuncStmt& stmt) override;
    void visitObjStmt(const ObjStmt& stmt) override;
    void visitBreakStmt(const BreakStmt& stmt) override;
    void visitContinueStmt(const ContinueStmt& stmt) override;
    void visitTryStmt(const TryStmt& stmt) override;
    void visitThrowStmt(const ThrowStmt& stmt) override;

    // Access to compiled chunk
    const Chunk& getChunk() const { return chunk; }

private:
    Chunk chunk;
    int currentLine = 1;
    int currentColumn = 1;

    void emitByte(uint8_t byte);
    void emitBytes(uint8_t a, uint8_t b);
    void emitConstant(Value value);
    size_t emitJump(OpCode instruction);
    void patchJump(size_t operandOffset);
    void emitLoop(size_t loopStart);
    uint8_t makeConstant(Value value);
    uint8_t identifierConstant(const std::string& name);

    // Compile an expression (convenience)
    void compileExpr(const Expr& expr);
};

} // namespace vora
