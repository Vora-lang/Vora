/// @file compiler.h
/// @brief AST-to-bytecode compiler for the Vora language.
///
/// The Compiler class walks the AST produced by the parser and emits bytecode
/// instructions into a Chunk. It implements the templated visitor interfaces
/// (ExprVisitor<void>, StmtVisitor<void>, ProgramVisitor<void>) so that
/// compilation is purely side-effectful — each visit* method emits the
/// corresponding bytecode sequence.
///
/// This header also defines the supporting data structures used during
/// compilation and at runtime: FunctionPrototype (compiled function blueprints
/// stored in the constant pool), ClassDefinition (unified compile-time and
/// runtime class representation), UpvalueDescriptor (closure capture info),
/// and LoopContext (break/continue back-patching state).

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

/// @brief Describes a captured variable for closure upvalue resolution.
///
/// When a nested function references a variable from an enclosing scope,
/// the compiler creates an UpvalueDescriptor to track whether the variable
/// is a local of the immediate enclosing function or an upvalue further up
/// the chain.
struct UpvalueDescriptor {
    uint8_t index;           ///< Local slot index (if isLocal) or enclosing upvalue index.
    bool isLocal;            ///< True if captured from the immediate enclosing function's local.
    bool isConst = false;    ///< True if the captured variable was declared const.
};

/// @brief Compiled function prototype stored in the constant pool.
///
/// Created at compile time and stored as a constant. At runtime, OP_CLOSURE
/// instantiates a VoraFunction from this prototype. The prototype owns the
/// function's bytecode chunk, parameter metadata, and upvalue descriptors.
struct FunctionPrototype : GcObject {
    std::string name;                          ///< Function name (empty for anonymous/lambda).
    int arity;                                 ///< Total arity including parameters with defaults.
    int requiredArity;                         ///< Number of parameters without default values.
    Chunk chunk;                               ///< Compiled bytecode for the function body.
    std::vector<UpvalueDescriptor> upvalues;   ///< Variables captured from enclosing scopes.
    bool isGenerator = false;                  ///< True if the function body contains a yield expression.
    bool hasRest = false;                      ///< True if the function declares a ...rest parameter.
    std::vector<std::string> paramNames;       ///< Fixed parameter names in definition order (excludes rest).
    std::vector<std::string> localNames;       ///< All local variable names in declaration order (for debugger).

    /// @brief Trace GC roots reachable from this object.
    /// @param wl Work list to append referenced GcObjects to.
    void trace(std::vector<GcObject*>& wl) override {
        // FunctionPrototype doesn't directly reference other GcObjects.
    }

    /// @brief Return the GC-tracked size of this object.
    /// @return Size in bytes (sizeof(FunctionPrototype)).
    size_t gcSize() const override { return sizeof(FunctionPrototype); }
};

/// @brief Unified compile-time and runtime class representation.
///
/// Replaces the former ClassData (compile-time) and ObjectClass (runtime) split.
/// Stored in the constant pool at compile time; OP_CLASS resolves parents,
/// builds method VoraFunctions, and computes the C3 linearization MRO in-place
/// on the same object.
///
/// Fields are divided into two groups: compile-time fields set by visitObjStmt
/// during parsing/compilation, and runtime fields set by OP_CLASS during
/// bytecode execution.
struct ClassDefinition : GcObject {
    // --- Compile-time fields (set by visitObjStmt) ---
    std::string name;                                          ///< Class name.
    std::vector<std::string> parentNames;                      ///< Names of parent classes (resolved at runtime).
    std::vector<std::string> params;                           ///< Constructor parameter names.
    GcPtr<FunctionPrototype> ctorProto;                        ///< Constructor function prototype.
    std::vector<GcPtr<FunctionPrototype>> methodProtos;        ///< Instance method prototypes.
    std::vector<GcPtr<FunctionPrototype>> staticMethodProtos;  ///< Static method prototypes (declared with `this.func`).

    // --- Runtime fields (set by OP_CLASS) ---
    std::map<std::string, GcPtr<class VoraFunction>> methods;          ///< Resolved instance methods (name → function).
    std::map<std::string, GcPtr<class VoraFunction>> staticMethods;    ///< Resolved static methods (name → function).
    std::vector<GcPtr<ClassDefinition>> parentClasses;                 ///< Resolved parent ClassDefinitions.
    std::vector<GcPtr<ClassDefinition>> mro;                           ///< C3-linearized method resolution order.

    /// @brief Trace GC roots reachable from this object.
    /// @param wl Work list to append referenced GcObjects to.
    void trace(std::vector<GcObject*>& wl) override;

    /// @brief Return the GC-tracked size of this object.
    /// @return Size in bytes (sizeof(ClassDefinition)).
    size_t gcSize() const override { return sizeof(ClassDefinition); }
};

/// @brief AST-to-bytecode compiler for the Vora language.
///
/// Walks the AST produced by the parser and emits bytecode instructions into a
/// Chunk. Implements the templated visitor interfaces so that compilation is a
/// side-effectful traversal — each visit* method emits the corresponding
/// bytecode sequence.
///
/// The compiler manages:
/// - Global variable interning (name → slot mapping)
/// - Local variable scoping (block-scoped, with depth tracking)
/// - Loop context stack (for break/continue back-patching)
/// - Try/catch/finally context (for handler registration and non-local exit routing)
/// - Upvalue tracking (for closure variable capture)
/// - Defer tracking (per-function LIFO cleanup at scope exit)
/// - Recursion depth guarding (prevents C++ stack overflow on deeply nested ASTs)
///
/// Phase 1: expressions, global variables, if/else, while, ternary,
///          arrays, indexing, native function calls.
/// Phase 2: local variables, break/continue, for-in, functions/closures,
///          try/catch/throw.
/// Phase 3: objects (Obj), property access, inheritance.
class Compiler : public ExprVisitor<void>,
                 public StmtVisitor<void>,
                 public ProgramVisitor<void> {
public:
    /// @brief Construct a compiler with the given error reporter.
    /// @param reporter Error reporter for logging compilation errors.
    explicit Compiler(ErrorReporter& reporter) : errorReporter_(reporter) {}

    /// @brief Compile an AST program into a bytecode Chunk.
    /// @param program The parsed AST program to compile.
    /// @return The compiled Chunk containing bytecode and constants.
    Chunk compile(const Program* program);

    /// @brief When true, expression statements print their value (if non-null)
    /// instead of discarding it. Used by the REPL.
    bool replMode = false;

    /// @brief True if any compilation error occurred (constant pool overflow,
    /// jump offset overflow, etc.). The caller must check this before using
    /// the compiled chunk — the bytecode may be incomplete.
    bool hadError = false;

    // --- ProgramVisitor ---

    /// @brief Compile the top-level program (all statements in order).
    /// @param program The root AST node.
    void visitProgram(const Program& program) override;

    // --- ExprVisitor ---

    /// @brief Compile a literal value expression (null, bool, number, string).
    /// @param expr The literal expression node.
    void visitLiteralExpr(const LiteralExpr& expr) override;

    /// @brief Compile a binary operator expression (+, -, *, /, ==, etc.).
    /// @param expr The binary expression node.
    void visitBinaryExpr(const BinaryExpr& expr) override;

    /// @brief Compile a parenthesized grouping expression.
    /// @param expr The grouping expression node.
    void visitGroupingExpr(const GroupingExpr& expr) override;

    /// @brief Compile a unary operator expression (!, -, ~).
    /// @param expr The unary expression node.
    void visitUnaryExpr(const UnaryExpr& expr) override;

    /// @brief Compile a variable reference (global or local lookup).
    /// @param expr The variable expression node.
    void visitVariableExpr(const VariableExpr& expr) override;

    /// @brief Compile a simple assignment expression (a = b).
    /// @param expr The assignment expression node.
    void visitAssignmentExpr(const AssignmentExpr& expr) override;

    /// @brief Compile a compound assignment expression (a += b, a -= b, etc.).
    /// @param expr The compound assignment expression node.
    void visitCompoundAssignmentExpr(const CompoundAssignmentExpr& expr) override;

    /// @brief Compile a function call expression.
    /// @param expr The call expression node.
    void visitCallExpr(const CallExpr& expr) override;

    /// @brief Compile an array literal expression ([...]).
    /// @param expr The array expression node.
    void visitArrayExpr(const ArrayExpr& expr) override;

    /// @brief Compile a dictionary literal expression ({key: value, ...}).
    /// @param expr The dict expression node.
    void visitDictExpr(const DictExpr& expr) override;

    /// @brief Compile an index expression (target[index]).
    /// @param expr The index expression node.
    void visitIndexExpr(const IndexExpr& expr) override;

    /// @brief Compile a property access expression (obj.prop).
    /// @param expr The property expression node.
    void visitPropertyExpr(const PropertyExpr& expr) override;

    /// @brief Compile a property assignment expression (obj.prop = value).
    /// @param expr The property assignment expression node.
    void visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) override;

    /// @brief Compile an index assignment expression (target[index] = value).
    /// @param expr The index assignment expression node.
    void visitIndexAssignmentExpr(const IndexAssignmentExpr& expr) override;

    /// @brief Compile a `this` expression (references the current instance).
    /// @param expr The this expression node.
    void visitThisExpr(const ThisExpr& expr) override;

    /// @brief Compile a `super` expression (parent method/property access).
    /// @param expr The super expression node.
    void visitSuperExpr(const SuperExpr& expr) override;

    /// @brief Compile an increment/decrement expression (++x, --x, x++, x--).
    /// @param expr The inc/dec expression node.
    void visitIncDecExpr(const IncDecExpr& expr) override;

    /// @brief Compile a ternary conditional expression (cond ? then : else).
    /// @param expr The ternary expression node.
    void visitTernaryExpr(const TernaryExpr& expr) override;

    /// @brief Compile a match expression (pattern matching).
    /// @param expr The match expression node.
    void visitMatchExpr(const MatchExpr& expr) override;

    /// @brief Compile the body of a single match case arm.
    /// @param case_ The match case AST node containing the pattern and body.
    void compileMatchCaseBody(const struct MatchCase& case_);

    /// @brief Compile a function expression (lambda / anonymous function).
    /// @param expr The function expression node.
    void visitFuncExpr(const FuncExpr& expr) override;

    /// @brief Compile a yield expression (generator suspension point).
    /// @param expr The yield expression node.
    void visitYieldExpr(const YieldExpr& expr) override;

    /// @brief Compile a destructuring assignment expression ([a, b] = expr).
    /// @param expr The destructure assignment expression node.
    void visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) override;

    /// @brief Compile a destructured binding (let/const [a, b] = expr).
    /// @param pattern The binding pattern (array or object destructure).
    /// @param initializer The right-hand-side expression, or nullptr if none.
    /// @param isConst True if the binding uses `const` (vs `let`).
    /// @param typeAnnotation Optional type annotation string (empty if none).
    void compileDestructuredBinding(const BindingPattern& pattern,
                                    const Expr* initializer, bool isConst,
                                    const std::string& typeAnnotation = "");

    /// @brief Emit bytecode to bind a destructuring pattern to a source value.
    /// @param pattern The binding pattern to compile.
    /// @param sourceSlot The local stack slot holding the source value.
    /// @param isConst True if the bound variables should be const.
    /// @param typeAnnotation Optional type annotation string.
    void compileBindPattern(const BindingPattern& pattern,
                            int sourceSlot, bool isConst,
                            const std::string& typeAnnotation = "");

    /// @brief Compile a bare destructuring assignment ([a, b] = expr) — no let/const.
    /// @param pattern The assignment pattern to compile.
    void compileAssignPattern(const BindingPattern& pattern);

    /// @brief Compile a spread expression (...expr).
    /// @param expr The spread expression node.
    void visitSpreadExpr(const SpreadExpr& expr) override;

    /// @brief Compile a list comprehension expression ([x for x in iter]).
    /// @param expr The list comprehension expression node.
    void visitListCompExpr(const ListCompExpr& expr) override;

    /// @brief Compile a dict comprehension expression ({k: v for k, v in iter}).
    /// @param expr The dict comprehension expression node.
    void visitDictCompExpr(const DictCompExpr& expr) override;

    /// @brief Compile an optional chaining expression (obj?.prop, obj?.[idx], obj?.()).
    /// @param expr The optional chain expression node.
    void visitOptionalChainExpr(const OptionalChainExpr& expr) override;

    /// @brief Compile an error expression (placeholder for parse errors).
    /// Emits OP_NULL so that surrounding valid code can still be compiled.
    /// @param expr The error expression node.
    void visitErrorExpr(const ErrorExpr& expr) override;

    // --- StmtVisitor ---

    /// @brief Compile an expression statement (expression followed by semicolon).
    /// If replMode is true and the value is non-null, emits OP_PRINT.
    /// @param stmt The expression statement node.
    void visitExprStmt(const ExprStmt& stmt) override;

    /// @brief Compile a let/const variable declaration statement.
    /// @param stmt The let statement node.
    void visitLetStmt(const LetStmt& stmt) override;

    /// @brief Compile a block statement ({ ... }), opening a new scope.
    /// @param stmt The block statement node.
    void visitBlockStmt(const BlockStmt& stmt) override;

    /// @brief Compile a return statement, routing through deferred finally blocks if needed.
    /// @param stmt The return statement node.
    void visitReturnStmt(const ReturnStmt& stmt) override;

    /// @brief Compile an if/elif/else statement with conditional jumps.
    /// @param stmt The if statement node.
    void visitIfStmt(const IfStmt& stmt) override;

    /// @brief Compile a while loop statement.
    /// @param stmt The while statement node.
    void visitWhileStmt(const WhileStmt& stmt) override;

    /// @brief Compile a do-while loop statement.
    /// @param stmt The do-while statement node.
    void visitDoWhileStmt(const DoWhileStmt& stmt) override;

    /// @brief Compile a for-in loop statement (desugared into a while loop).
    /// @param stmt The for-in statement node.
    void visitForStmt(const ForStmt& stmt) override;

    /// @brief Compile a C-style for(;;) statement.
    /// @param stmt The C-style for statement node.
    void visitCForStmt(const CForStmt& stmt) override;

    /// @brief Compile a named function declaration statement.
    /// @param stmt The function statement node.
    void visitFuncStmt(const FuncStmt& stmt) override;

    /// @brief Compile an object/class definition statement.
    /// @param stmt The object statement node.
    void visitObjStmt(const ObjStmt& stmt) override;

    /// @brief Compile a break statement, routing through finally blocks and
    /// cleaning up try handlers at each nesting level.
    /// @param stmt The break statement node.
    void visitBreakStmt(const BreakStmt& stmt) override;

    /// @brief Compile a continue statement, routing through finally blocks
    /// and cleaning up try handlers at each nesting level.
    /// @param stmt The continue statement node.
    void visitContinueStmt(const ContinueStmt& stmt) override;

    /// @brief Compile a try/catch/finally statement with handler registration
    /// and non-local exit routing.
    /// @param stmt The try statement node.
    void visitTryStmt(const TryStmt& stmt) override;

    /// @brief Compile a throw statement.
    /// @param stmt The throw statement node.
    void visitThrowStmt(const ThrowStmt& stmt) override;

    /// @brief Compile an import statement (loads a module and binds its exports).
    /// @param stmt The import statement node.
    void visitImportStmt(const ImportStmt& stmt) override;

    /// @brief Compile an export statement (marks names for module export).
    /// @param stmt The export statement node.
    void visitExportStmt(const ExportStmt& stmt) override;

    /// @brief Compile a defer statement (schedules a closure for LIFO execution at scope exit).
    /// @param stmt The defer statement node.
    void visitDeferStmt(const DeferStmt& stmt) override;

    /// @brief Compile an error statement (placeholder for parse errors).
    /// Emits a no-op so that surrounding valid code can still be compiled.
    /// @param stmt The error statement node.
    void visitErrorStmt(const ErrorStmt& stmt) override;

    /// @brief Get the list of exported names (for modules compiled as import targets).
    /// @return Vector of export name strings.
    const std::vector<std::string>& getExportNames() const { return exportNames; }

    /// @brief Get a const reference to the compiled chunk.
    /// @return The compiled Chunk.
    const Chunk& getChunk() const { return chunk; }

    /// @brief Set the source text for error display (compiler errors show source snippets).
    /// @param source The original source code string.
    void setSource(const std::string& source) { chunk.source = source; }

    /// @brief Get the global variable name table (maps slot index → name).
    /// @return Vector of global variable names in slot order.
    const std::vector<std::string>& getGlobalNames() const { return globalNames; }

    /// @brief Pre-populate the global interning table so that slot assignments
    /// match an existing VM (e.g. REPL). All seeded names are marked
    /// "defined" so they are treated as already-existing globals.
    /// @param names List of global variable names to seed.
    /// @param defined If true (default), mark names as already defined.
    ///        Set to false when compiling import targets — modules need to
    ///        be able to shadow builtins (e.g. `export let abs = abs`).
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

    /// @brief Report a compilation error at the current source position.
    /// Sets hadError = true and delegates to the error reporter.
    /// @param message Human-readable error message.
    void error(const std::string& message) {
        hadError = true;
        errorReporter_.error(currentLine, currentColumn, 1, message);
    }

    /// @brief Report a compilation error at a specific source position.
    /// Sets hadError = true and delegates to the error reporter.
    /// @param line Source line number (1-based).
    /// @param column Source column number (1-based).
    /// @param length Length of the erroneous token in characters.
    /// @param message Human-readable error message.
    void errorAt(int line, int column, int length, const std::string& message) {
        hadError = true;
        errorReporter_.error(line, column, length, message);
    }

    // =========================================================================
    // Global variable interning
    // =========================================================================

    std::vector<std::string> globalNames;    ///< Global variable names indexed by slot number.
    std::vector<bool> globalDefined;         ///< Parallel: true if OP_DEFINE_GLOBAL was emitted for the slot.
    std::vector<bool> globalIsConst;         ///< Parallel: true if the global was declared with 'const'.

    /// @brief Resolve a global variable name to a slot index, allocating a new slot if needed.
    /// @param name The global variable name.
    /// @return The slot index for the global (0-based).
    int resolveGlobal(const std::string& name);

    /// @brief Define a new global variable, erroring on redefinition.
    /// @param name The global variable name to define.
    /// @return The slot index for the new global.
    int defineGlobal(const std::string& name);

    /// @brief Define a new const global variable, erroring on redefinition.
    /// @param name The global variable name to define.
    /// @return The slot index for the new const global.
    int defineGlobalConst(const std::string& name);

    // =========================================================================
    // Local variable tracking
    // =========================================================================

    /// @brief Tracks a local variable's name, scope depth, capture status, and const-ness.
    struct Local {
        std::string name;    ///< Variable name.
        int depth;           ///< Scope depth where the variable was declared.
        bool captured;       ///< True if referenced by a nested closure (forces heap allocation).
        bool isConst;        ///< True if declared with the 'const' keyword.
    };

    std::vector<Local> locals;               ///< All local variables in the current function.
    int scopeDepth = 0;                      ///< Current nesting depth (0 = global scope).
    std::vector<int> scopeLocalCounts;       ///< Number of locals added per scope (parallel to scopes).

    int destructureTempCounter = 0;          ///< Counter for unique temp names in destructuring.
    int listCompCounter = 0;                 ///< Counter for unique _lcN result-array names in list comprehensions.
    int dictCompCounter = 0;                 ///< Counter for unique _dcN result-dict names in dict comprehensions.
    int matchCounter = 0;                    ///< Counter for unique __mN scrutinee temp names in match expressions.

    /// @brief Maximum allowed compilation recursion depth to prevent C++ stack overflow
    /// when compiling deeply nested ASTs (e.g. thousands of nested expressions).
    static constexpr int MAX_COMPILE_DEPTH = 10000;
    int recursionDepth_ = 0;                 ///< Current recursion depth counter.

    /// @brief Begin a new lexical scope (increments scopeDepth, records local count).
    void beginScope();

    /// @brief End the current lexical scope (pops locals declared in this scope).
    void endScope();

    /// @brief Add a local variable to the current scope.
    /// @param name The variable name.
    /// @param isConst True if declared with 'const'.
    void addLocal(const std::string& name, bool isConst = false);

    /// @brief Resolve a local variable name by walking the locals vector in reverse.
    /// @param name The variable name to look up.
    /// @return The local slot index, or -1 if not found.
    int resolveLocal(const std::string& name) const;

    /// @brief Check whether a local variable was declared const.
    /// @param name The variable name to check.
    /// @return True if the named local exists and is const.
    bool isLocalConst(const std::string& name) const;

    /// @brief Get the current number of active local variables.
    /// @return Count of locals currently in scope.
    int currentLocalCount() const;

    // =========================================================================
    // Loop context (break/continue back-patching)
    // =========================================================================

    /// @brief Tracks loop state for break/continue jump back-patching.
    ///
    /// Pushed onto loopStack when entering a loop body and popped on exit.
    /// Collects placeholder jump offsets that are patched once the loop end
    /// is known. Also tracks auto-generated locals (e.g. for-in iterator
    /// variables) that must be cleaned up on break/continue.
    struct LoopContext {
        size_t loopStart;                    ///< Bytecode offset of the loop condition (for OP_LOOP back-jump).
        size_t continueTarget;               ///< Bytecode offset of the increment section (for continue jumps).
        std::vector<size_t> breakJumps;      ///< OP_JUMP placeholder offsets to patch with the loop exit address.
        std::vector<size_t> continueJumps;   ///< OP_JUMP placeholder offsets to patch with the continue target.
        int enclosingScopeDepth;             ///< Scope depth at loop entry (for local cleanup on break).
        int extraLocalsToPopOnBreak = 0;     ///< Auto-generated locals to pop when breaking out.
        int extraLocalsToPopOnContinue = 0;  ///< Auto-generated locals to pop when continuing.
    };

    std::vector<LoopContext> loopStack;      ///< Stack of active loop contexts (innermost at back).

    // =========================================================================
    // Try context
    // =========================================================================

    int tryNesting = 0;    ///< Depth of active try blocks (for emitting OP_POP_CATCH cleanup on non-local exit).
    bool isGenerator = false;  ///< True when a yield expression is encountered in the function body.

    // =========================================================================
    // Finally context (for routing break/continue/return through finally)
    // =========================================================================

    int finallyNesting = 0;                              ///< Count of active try blocks that have a finally clause.
    std::vector<std::vector<uint8_t>> finallyBytecodeStack;  ///< Recorded bytecode per finally block, replayed on non-local exit.
    std::vector<size_t> pendingReturnJumps;               ///< Return jump offsets to route through finally blocks.

    // =========================================================================
    // Function context
    // =========================================================================

    Compiler* enclosing = nullptr;  ///< Pointer to the enclosing function's compiler (null for top-level).

    // =========================================================================
    // Export tracking (for modules compiled as import targets)
    // =========================================================================

    std::vector<std::string> exportNames;  ///< Names exported by this module (populated by visitExportStmt).

    // =========================================================================
    // Upvalue tracking (closures)
    // =========================================================================

    std::vector<UpvalueDescriptor> upvalues;  ///< Upvalue descriptors for the current function.

    /// @brief Resolve a captured variable, walking enclosing compilers up the chain.
    /// @param compiler The compiler to start searching from (typically the enclosing compiler).
    /// @param name The variable name to capture.
    /// @return The upvalue index in the current function's upvalues vector.
    int resolveUpvalue(Compiler* compiler, const std::string& name);

    /// @brief Check whether a captured upvalue was declared const.
    /// @param upvalueIdx The upvalue index to check.
    /// @return True if the captured variable at the given upvalue index is const.
    bool isUpvalueConst(int upvalueIdx) const;

    // =========================================================================
    // Defer tracking (per-function LIFO execution at return)
    // Defers are compiled to closures at declaration time (OP_CLOSURE + OP_DEFER_PUSH)
    // and flushed at exit points (OP_DEFER_FLUSH) or during throwException unwind.
    // =========================================================================

    int deferCount_ = 0;  ///< Number of defer statements in the current function.

    /// @brief Emit OP_DEFER_FLUSH if the current function has any defer statements.
    void emitDeferFlush();

    // =========================================================================
    // Bytecode emission
    // =========================================================================

    /// @brief Emit a single byte to the chunk.
    /// @param byte The opcode or operand byte to emit.
    void emitByte(uint8_t byte);

    /// @brief Emit two bytes to the chunk.
    /// @param a First byte.
    /// @param b Second byte.
    void emitBytes(uint8_t a, uint8_t b);

    /// @brief Emit a 16-bit value in little-endian byte order.
    /// @param value The 16-bit unsigned integer to emit.
    void emitShort(uint16_t value);

    /// @brief Emit OP_GET_GLOBAL or OP_GET_GLOBAL_WIDE depending on the slot index.
    /// @param slot The global variable slot index.
    void emitGetGlobal(int slot);

    /// @brief Emit OP_SET_GLOBAL or OP_SET_GLOBAL_WIDE depending on the slot index.
    /// @param slot The global variable slot index.
    void emitSetGlobal(int slot);

    /// @brief Emit OP_DEFINE_GLOBAL or OP_DEFINE_GLOBAL_WIDE depending on the slot index.
    /// @param slot The global variable slot index.
    void emitDefineGlobal(int slot);

    /// @brief Emit an OP_CONVERT instruction for a type annotation.
    /// @param typeAnnotation The type name string (e.g. "int", "string").
    void emitConvert(const std::string& typeAnnotation);

    /// @brief Emit an instruction to push a constant value onto the stack.
    /// Adds the value to the constant pool if needed, then emits OP_CONSTANT[_WIDE].
    /// @param value The constant value to emit.
    void emitConstant(Value value);

    /// @brief Emit a jump instruction with placeholder operands for later back-patching.
    /// @param instruction The jump opcode (OP_JUMP, OP_JUMP_IF_FALSE, etc.).
    /// @return The bytecode offset of the first placeholder operand byte.
    size_t emitJump(OpCode instruction);

    /// @brief Back-patch a previously emitted jump to point to the current code end.
    /// @param operandOffset The offset returned by emitJump().
    void patchJump(size_t operandOffset);

    /// @brief Emit an OP_LOOP instruction that jumps backward to the given offset.
    /// @param loopStart The bytecode offset to jump back to.
    void emitLoop(size_t loopStart);

    /// @brief Add a constant value to the chunk's constant pool and return its index.
    /// @param value The constant value to add.
    /// @return The constant pool index.
    size_t makeConstant(Value value);

    /// @brief Add a string identifier to the constant pool (deduplicated) and return its index.
    /// @param name The identifier string.
    /// @return The constant pool index.
    size_t identifierConstant(const std::string& name);

    /// @brief Add a function prototype to the constant pool and return its index.
    /// @param proto The function prototype to add.
    /// @return The constant pool index.
    size_t addFunctionPrototype(FunctionPrototype proto);

    // --- Emitter helpers that auto-select narrow/wide based on index ---

    /// @brief Emit OP_GET_PROPERTY or OP_GET_PROPERTY_WIDE.
    /// @param nameIndex Constant pool index of the property name.
    void emitGetProperty(size_t nameIndex);

    /// @brief Emit OP_GET_PROPERTY_SAFE or OP_GET_PROPERTY_SAFE_WIDE (optional chaining).
    /// @param nameIndex Constant pool index of the property name.
    void emitGetPropertySafe(size_t nameIndex);

    /// @brief Emit OP_SET_PROPERTY or OP_SET_PROPERTY_WIDE.
    /// @param nameIndex Constant pool index of the property name.
    void emitSetProperty(size_t nameIndex);

    /// @brief Emit OP_GET_LOCAL_PROP or OP_GET_LOCAL_PROP_WIDE.
    /// @param localSlot The local variable slot index.
    /// @param nameIndex Constant pool index of the property name.
    void emitGetLocalProp(int localSlot, size_t nameIndex);

    /// @brief Emit OP_GET_GLOBAL_PROP or OP_GET_GLOBAL_PROP_WIDE.
    /// @param globalSlot The global variable slot index.
    /// @param nameIndex Constant pool index of the property name.
    void emitGetGlobalProp(int globalSlot, size_t nameIndex);

    /// @brief Emit OP_GET_SUPER or OP_GET_SUPER_WIDE.
    /// @param nameIndex Constant pool index of the method/property name.
    void emitGetSuper(size_t nameIndex);

    /// @brief Emit OP_CLOSURE or OP_CLOSURE_WIDE to instantiate a VoraFunction at runtime.
    /// @param protoIndex Constant pool index of the FunctionPrototype.
    /// @param upvalues Upvalue descriptors for captured variables.
    void emitClosure(size_t protoIndex,
                     const std::vector<UpvalueDescriptor>& upvalues);

    /// @brief Emit OP_CLASS or OP_CLASS_WIDE to resolve and finalize a ClassDefinition at runtime.
    /// @param classIndex Constant pool index of the ClassDefinition.
    /// @param methodCount Number of methods in the class.
    void emitClass(size_t classIndex,
                   uint8_t methodCount);

    /// @brief Emit OP_IMPORT or OP_IMPORT_WIDE to load a module at runtime.
    /// @param pathIndex Constant pool index of the module path string.
    /// @param nameIndex Constant pool index of the binding name.
    void emitImport(size_t pathIndex,
                    size_t nameIndex);

    /// @brief Emit OP_GET_GLOBAL_SAFE or OP_GET_GLOBAL_SAFE_WIDE (returns null if undefined).
    /// @param slot The global variable slot index.
    /// @param fallbackIndex Constant pool index of the fallback value name.
    void emitGetGlobalSafe(int slot,
                           size_t fallbackIndex);

    /// @brief Emit OP_CALL_KW or OP_CALL_KW_WIDE for calls with keyword arguments.
    /// @param posCount Number of positional arguments.
    /// @param kwCount Number of keyword arguments.
    /// @param kwNameIndices Constant pool indices of keyword argument names.
    void emitCallKw(uint8_t posCount, uint8_t kwCount,
                    const std::vector<size_t>& kwNameIndices);

    /// @brief Emit default-parameter preamble for a function/method/constructor.
    ///
    /// For each parameter with a default value, emits:
    ///   OP_DEFAULT_PARAM <slot> <skipOffset16>
    ///   ... compiled default expression ...
    ///   OP_SET_LOCAL <slot>
    ///   OP_POP
    ///
    /// @param child     The compiler for the function/method body.
    /// @param params    Parameter declarations.
    /// @param slotBase  Local slot for param 0 (0 for functions, 1 for
    ///                  methods/constructors where 'this' occupies slot 0).
    /// @param requiredArity  Number of mandatory (non-default) parameters.
    /// @param totalArity     Total number of fixed (non-rest) parameters.
    void compileDefaultParamPreamble(Compiler& child,
                                     const std::vector<ParamDecl>& params,
                                     int slotBase, int requiredArity,
                                     int totalArity);

    /// @brief Convenience method to compile an expression by calling its accept().
    /// @param expr The expression to compile.
    void compileExpr(const Expr& expr);

    /// @brief Compile a string literal with optional ${...} interpolation.
    /// @param str The string containing interpolation markers.
    void compileInterpolatedString(const std::string& str);

    /// @brief Compile a reference to a variable or property (used in interpolation).
    /// @param name The variable or property name.
    void compileVariableOrPropertyRef(const std::string& name);
};

} // namespace vora
