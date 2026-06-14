#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// Upvalue descriptor: describes a captured variable.
struct UpvalueDescriptor {
    uint8_t index;   // local slot index (if isLocal) or enclosing upvalue index
    bool isLocal;    // true = captured from immediate enclosing function's local
};

// Compiled function prototype stored in constant pool.
struct FunctionPrototype : GcObject {
    std::string name;
    int arity;            // total arity (max params including defaults)
    int requiredArity;    // params without default values
    Chunk chunk;
    std::vector<UpvalueDescriptor> upvalues;  // captured variables

    void trace(std::vector<GcObject*>& wl) override {
        // FunctionPrototype doesn't directly reference other GcObjects.
    }
    size_t gcSize() const override { return sizeof(FunctionPrototype); }
};

// Class data stored in constant pool for OP_CLASS.
struct ClassData : GcObject {
    std::string name;
    std::vector<std::string> parentNames;
    std::vector<std::string> params;
    GcPtr<FunctionPrototype> ctor;
    std::vector<GcPtr<FunctionPrototype>> methods;

    void trace(std::vector<GcObject*>& wl) override {
        if (ctor) wl.push_back(ctor.get());
        for (auto& m : methods) {
            if (m) wl.push_back(m.get());
        }
    }
    size_t gcSize() const override { return sizeof(ClassData); }
};

// Compiler: AST → bytecode (side-effectful emission into a Chunk).
// Implements ExprVisitor<void> + StmtVisitor<void> + ProgramVisitor<void>.
//
// Phase 1: expressions, global variables, if/else, while, ternary,
//          arrays, indexing, native function calls.
// Phase 2: local variables, break/continue, for-in, functions/closures,
//          try/catch/throw.
// Phase 3: objects (Obj), property access, inheritance.
class Compiler : public ExprVisitor<void>,
                 public StmtVisitor<void>,
                 public ProgramVisitor<void> {
public:
    Chunk compile(const Program* program);

    // True if any compilation error occurred (constant pool overflow, jump
    // offset overflow, etc.). The caller must check this before using the
    // compiled chunk — the bytecode may be incomplete.
    bool hadError = false;

    // --- ProgramVisitor ---
    void visitProgram(const Program& program) override;

    // --- ExprVisitor ---
    void visitLiteralExpr(const LiteralExpr& expr) override;
    void visitBinaryExpr(const BinaryExpr& expr) override;
    void visitGroupingExpr(const GroupingExpr& expr) override;
    void visitUnaryExpr(const UnaryExpr& expr) override;
    void visitVariableExpr(const VariableExpr& expr) override;
    void visitAssignmentExpr(const AssignmentExpr& expr) override;
    void visitCompoundAssignmentExpr(const CompoundAssignmentExpr& expr) override;
    void visitCallExpr(const CallExpr& expr) override;
    void visitArrayExpr(const ArrayExpr& expr) override;
    void visitDictExpr(const DictExpr& expr) override;
    void visitIndexExpr(const IndexExpr& expr) override;
    void visitPropertyExpr(const PropertyExpr& expr) override;
    void visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) override;
    void visitIndexAssignmentExpr(const IndexAssignmentExpr& expr) override;
    void visitThisExpr(const ThisExpr& expr) override;
    void visitSuperExpr(const SuperExpr& expr) override;
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

    // Set source text for error display (compiler errors show source snippets)
    void setSource(const std::string& source) { chunk.source = source; }

    // Global variable interning: maps name → slot for direct VM indexing
    const std::vector<std::string>& getGlobalNames() const { return globalNames; }

private:
    Chunk chunk;
    int currentLine = 1;
    int currentColumn = 1;

    // =========================================================================
    // Global variable interning
    // =========================================================================
    std::vector<std::string> globalNames;
    std::vector<bool> globalDefined;  // parallel: true if OP_DEFINE_GLOBAL emitted
    int resolveGlobal(const std::string& name);  // returns slot index (allocates if needed)
    int defineGlobal(const std::string& name);   // like resolveGlobal but errors on redefinition

    // =========================================================================
    // Local variable tracking
    // =========================================================================
    struct Local {
        std::string name;
        int depth;       // scope depth where declared
        bool captured;   // true if referenced by a nested closure
    };
    std::vector<Local> locals;
    int scopeDepth = 0;
    std::vector<int> scopeLocalCounts;  // #locals added per scope (parallel to scopes)

    void beginScope();
    void endScope();
    void addLocal(const std::string& name);
    int resolveLocal(const std::string& name) const;  // returns -1 if not found
    int currentLocalCount() const;

    // =========================================================================
    // Loop context (break/continue back-patching)
    // =========================================================================
    struct LoopContext {
        size_t loopStart;                   // offset to re-evaluate condition
        size_t continueTarget;              // offset to jump for continue (increment section)
        std::vector<size_t> breakJumps;     // OP_JUMP placeholders to patch (break)
        std::vector<size_t> continueJumps;  // OP_JUMP placeholders to patch (continue)
        int enclosingScopeDepth;            // scope depth at loop entry
        int extraLocalsToPop = 0;           // for-in: auto-generated locals to pop on break
    };
    std::vector<LoopContext> loopStack;

    // =========================================================================
    // Try context
    // =========================================================================
    int tryNesting = 0;  // depth of active try blocks (for OP_POP_CATCH cleanup)

    // =========================================================================
    // Finally context (for routing break/continue/return through finally)
    // =========================================================================
    int finallyNesting = 0;                        // count of active try blocks with finally
    std::vector<std::vector<uint8_t>> finallyBytecodeStack;  // recorded bytecode per finally
    std::vector<size_t> pendingReturnJumps;        // return jumps to route through finally

    // =========================================================================
    // Function context
    // =========================================================================
    Compiler* enclosing = nullptr;  // outer compiler (for closures)

    // =========================================================================
    // Upvalue tracking (closures)
    // =========================================================================
    std::vector<UpvalueDescriptor> upvalues;
    int resolveUpvalue(Compiler* compiler, const std::string& name);

    // =========================================================================
    // Bytecode emission
    // =========================================================================
    void emitByte(uint8_t byte);
    void emitBytes(uint8_t a, uint8_t b);
    void emitConstant(Value value);
    size_t emitJump(OpCode instruction);
    void patchJump(size_t operandOffset);
    void emitLoop(size_t loopStart);
    uint8_t makeConstant(Value value);
    uint8_t identifierConstant(const std::string& name);
    uint8_t addFunctionPrototype(FunctionPrototype proto);

    // Compile an expression (convenience)
    void compileExpr(const Expr& expr);

    // Compile a string with optional ${...} interpolation
    void compileInterpolatedString(const std::string& str);
    void compileVariableOrPropertyRef(const std::string& name);
};

} // namespace vora
