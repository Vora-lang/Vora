#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "chunk.h"
#include "opcode.h"

#include "../ast/expr.h"
#include "../ast/expr_visitor.h"
#include "../ast/program.h"
#include "../ast/stmt.h"
#include "../ast/stmt_visitor.h"
#include "../common/error_reporter.h"
#include "../lexer/token.h"
#include "../runtime/value.h"

namespace vora {

// Upvalue descriptor: describes a captured variable.
struct UpvalueDescriptor {
    uint8_t index;   // local slot index (if isLocal) or enclosing upvalue index
    bool isLocal;    // true = captured from immediate enclosing function's local
    bool isConst = false;  // true if the captured variable was declared const
};

// Compiled function prototype stored in constant pool.
struct FunctionPrototype : GcObject {
    std::string name;
    int arity;            // total arity (max params including defaults)
    int requiredArity;    // params without default values
    Chunk chunk;
    std::vector<UpvalueDescriptor> upvalues;  // captured variables
    bool isGenerator = false;  // true if function body contains yield
    bool hasRest = false;      // true if function has ...rest parameter

    void trace(std::vector<GcObject*>& wl) override {
        // FunctionPrototype doesn't directly reference other GcObjects.
    }
    size_t gcSize() const override { return sizeof(FunctionPrototype); }
};

// ClassDefinition — unified compile-time + runtime class representation.
// Replaces the former ClassData (compile-time) and ObjectClass (runtime) split.
// Stored in the constant pool at compile time; OP_CLASS resolves parents,
// builds method VoraFunctions, and computes MRO in-place.
struct ClassDefinition : GcObject {
    // --- Compile-time fields (set by visitObjStmt) ---
    std::string name;
    std::vector<std::string> parentNames;
    std::vector<std::string> params;
    GcPtr<FunctionPrototype> ctorProto;
    std::vector<GcPtr<FunctionPrototype>> methodProtos;

    // --- Runtime fields (set by OP_CLASS) ---
    std::map<std::string, GcPtr<class VoraFunction>> methods;
    std::vector<GcPtr<ClassDefinition>> parentClasses;
    std::vector<GcPtr<ClassDefinition>> mro;

    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override { return sizeof(ClassDefinition); }
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
    explicit Compiler(ErrorReporter& reporter) : errorReporter_(reporter) {}

    Chunk compile(const Program* program);

    // When true, expression statements print their value (if non-null)
    // instead of discarding it. Used by the REPL.
    bool replMode = false;

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
    void visitMatchExpr(const MatchExpr& expr) override;
    void compileMatchCaseBody(const struct MatchCase& case_);
    void visitFuncExpr(const FuncExpr& expr) override;
    void visitYieldExpr(const YieldExpr& expr) override;
    void visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) override;

    // Destructuring compilation helpers
    void compileDestructuredBinding(const BindingPattern& pattern,
                                    const Expr* initializer, bool isConst);
    void compileBindPattern(const BindingPattern& pattern,
                            int sourceSlot, bool isConst);
    void visitSpreadExpr(const SpreadExpr& expr) override;
    void visitListCompExpr(const ListCompExpr& expr) override;
    void visitDictCompExpr(const DictCompExpr& expr) override;
    void visitOptionalChainExpr(const OptionalChainExpr& expr) override;
    void visitErrorExpr(const ErrorExpr& expr) override;

    // --- StmtVisitor ---
    void visitExprStmt(const ExprStmt& stmt) override;
    void visitLetStmt(const LetStmt& stmt) override;
    void visitBlockStmt(const BlockStmt& stmt) override;
    void visitReturnStmt(const ReturnStmt& stmt) override;
    void visitIfStmt(const IfStmt& stmt) override;
    void visitWhileStmt(const WhileStmt& stmt) override;
    void visitDoWhileStmt(const DoWhileStmt& stmt) override;
    void visitForStmt(const ForStmt& stmt) override;
    void visitCForStmt(const CForStmt& stmt) override;
    void visitFuncStmt(const FuncStmt& stmt) override;
    void visitObjStmt(const ObjStmt& stmt) override;
    void visitBreakStmt(const BreakStmt& stmt) override;
    void visitContinueStmt(const ContinueStmt& stmt) override;
    void visitTryStmt(const TryStmt& stmt) override;
    void visitThrowStmt(const ThrowStmt& stmt) override;
    void visitImportStmt(const ImportStmt& stmt) override;
    void visitExportStmt(const ExportStmt& stmt) override;
    void visitDeferStmt(const DeferStmt& stmt) override;
    void visitErrorStmt(const ErrorStmt& stmt) override;

    // Export name tracking (for modules being compiled as import targets)
    const std::vector<std::string>& getExportNames() const { return exportNames; }

    // Access to compiled chunk
    const Chunk& getChunk() const { return chunk; }

    // Set source text for error display (compiler errors show source snippets)
    void setSource(const std::string& source) { chunk.source = source; }

    // Global variable interning: maps name → slot for direct VM indexing
    const std::vector<std::string>& getGlobalNames() const { return globalNames; }

    // Pre-populate the global interning table so that slot assignments
    // match an existing VM (e.g. REPL). All seeded names are marked
    // "defined" so they are treated as already-existing globals.
    // Set defined=false when compiling import targets — modules need to
    // be able to shadow builtins (e.g. `export let abs = abs`).
    void seedGlobals(const std::vector<std::string>& names, bool defined = true) {
        for (const auto& name : names) {
            globalNames.push_back(name);
            globalDefined.push_back(defined);
            globalIsConst.push_back(false);
        }
    }

private:
    Chunk chunk;
    int currentLine = 1;
    int currentColumn = 1;
    ErrorReporter& errorReporter_;

    // Report a compilation error at the current source position.
    // Sets hadError = true and delegates to the error reporter.
    void error(const std::string& message) {
        hadError = true;
        errorReporter_.error(currentLine, currentColumn, 1, message);
    }

    // Report a compilation error at a specific source position.
    void errorAt(int line, int column, int length, const std::string& message) {
        hadError = true;
        errorReporter_.error(line, column, length, message);
    }

    // =========================================================================
    // Global variable interning
    // =========================================================================
    std::vector<std::string> globalNames;
    std::vector<bool> globalDefined;  // parallel: true if OP_DEFINE_GLOBAL emitted
    std::vector<bool> globalIsConst;  // parallel: true if declared with 'const'
    int resolveGlobal(const std::string& name);  // returns slot index (allocates if needed)
    int defineGlobal(const std::string& name);   // like resolveGlobal but errors on redefinition
    int defineGlobalConst(const std::string& name);  // like defineGlobal but for const

    // =========================================================================
    // Local variable tracking
    // =========================================================================
    struct Local {
        std::string name;
        int depth;       // scope depth where declared
        bool captured;   // true if referenced by a nested closure
        bool isConst;    // true if declared with 'const' keyword
    };
    std::vector<Local> locals;
    int scopeDepth = 0;
    std::vector<int> scopeLocalCounts;  // #locals added per scope (parallel to scopes)

    int destructureTempCounter = 0;  // counter for unique temp names in destructuring
    int listCompCounter = 0;        // counter for unique _lcN result-array names
    int dictCompCounter = 0;        // counter for unique _dcN result-dict names
    int matchCounter = 0;           // counter for unique __mN scrutinee names

    void beginScope();
    void endScope();
    void addLocal(const std::string& name, bool isConst = false);
    int resolveLocal(const std::string& name) const;  // returns -1 if not found
    bool isLocalConst(const std::string& name) const;  // check if a local is const
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
        int extraLocalsToPopOnBreak = 0;    // auto-generated locals to pop on break
        int extraLocalsToPopOnContinue = 0; // auto-generated locals to pop on continue
    };
    std::vector<LoopContext> loopStack;

    // =========================================================================
    // Try context
    // =========================================================================
    int tryNesting = 0;  // depth of active try blocks (for OP_POP_CATCH cleanup)
    bool isGenerator = false;  // set true when yield is encountered in function body

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
    // Export tracking (for modules compiled as import targets)
    // =========================================================================
    std::vector<std::string> exportNames;

    // =========================================================================
    // Upvalue tracking (closures)
    // =========================================================================
    std::vector<UpvalueDescriptor> upvalues;
    int resolveUpvalue(Compiler* compiler, const std::string& name);
    bool isUpvalueConst(int upvalueIdx) const;  // check if captured upvalue is const

    // =========================================================================
    // Defer tracking (per-function LIFO execution at return)
    // =========================================================================
    struct DeferredCall {
        uint8_t protoIndex;
        std::vector<UpvalueDescriptor> upvalues;
    };
    std::vector<DeferredCall> deferredProtos;
    void emitDeferCalls();  // emit calls for all deferred closures (LIFO), popping results

    // =========================================================================
    // Bytecode emission
    // =========================================================================
    void emitByte(uint8_t byte);
    void emitBytes(uint8_t a, uint8_t b);
    void emitShort(uint16_t value);   // emit 2-byte little-endian value
    void emitGetGlobal(int slot);      // emits OP_GET_GLOBAL[_WIDE] based on slot
    void emitSetGlobal(int slot);      // emits OP_SET_GLOBAL[_WIDE] based on slot
    void emitDefineGlobal(int slot);   // emits OP_DEFINE_GLOBAL[_WIDE] based on slot
    void emitConvert(const std::string& typeAnnotation);  // emits OP_CONVERT for type annotation
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
