/**
 * @file src/lsp/semantic_analyzer.h
 * @brief Scope-aware AST analysis pass for the Vora LSP server.
 *
 * SemanticAnalyzer walks the AST (read-only) and builds a scope-annotated
 * symbol table.  It performs no bytecode emission and has no VM dependency --
 * the analysis is purely structural.  This makes it suitable for use in the
 * LSP server, which needs fast, incremental re-analysis on every keystroke.
 *
 * The analyzer implements ExprVisitor<void>, StmtVisitor<void>, and
 * ProgramVisitor<void> -- no new accept() overloads are required since void
 * is already wired in the AST node dispatch.
 *
 * Query API provided for LSP feature implementations:
 *   - findDeclarationAt(line, col)  -- go-to-definition
 *   - findReferencesTo(line, col)   -- find-references
 *   - getVisibleSymbols(line, col)  -- completion candidates
 *   - findSymbolAt(line, col)       -- hover information
 *
 * Semantic diagnostics collected during analysis:
 *   - Unused local variables
 *   - Unreachable code after return/throw/break/continue
 *   - Shadowed variable declarations
 *
 * <b>Position convention:</b> ALL positions are 1-based line/column
 * (matching Token storage).  Conversion to 0-based LSP format happens in
 * the LSP server layer (tokenToLspRange).
 */

#ifndef VORA_LSP_SEMANTIC_ANALYZER_H
#define VORA_LSP_SEMANTIC_ANALYZER_H

#include "ast/expr.h"
#include "ast/stmt.h"
#include "ast/program.h"
#include "ast/expr_visitor.h"
#include "ast/stmt_visitor.h"
#include "lexer/token.h"

#include <climits>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vora {

// ═══════════════════════════════════════════════════════════════════════════
// Symbol classification
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Classification of a declared symbol in the Vora language.
 *
 * Used by SymbolInfo to tag declarations so the LSP can provide
 * appropriate icons, detail strings, and filtering in the UI.
 */
enum class SymbolKind : uint8_t {
    Variable,   ///< `let` declaration.
    Constant,   ///< `const` declaration.
    Function,   ///< Named `func` declaration.
    Parameter,  ///< Function or object constructor parameter.
    Object,     ///< `Obj` name (class-like).
    Method,     ///< Method declared inside an Obj.
    Import,     ///< Import binding (module namespace or from-import name).
    ForVar,     ///< For-in loop iteration variable.
    CatchVar,   ///< catch(e) exception variable.
};

/**
 * @brief Convert a SymbolKind to its human-readable string representation.
 *
 * @param kind The symbol kind to convert.
 * @return A C-string such as "Variable", "Function", "Object", etc.
 */
const char* symbolKindToString(SymbolKind kind);

// ═══════════════════════════════════════════════════════════════════════════
// SymbolInfo — one declared symbol
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Metadata for a single declared symbol in the symbol table.
 *
 * Stores the name, kind, declaration position, scope nesting, and
 * kind-specific details (parameters for functions, parents/methods for
 * objects, import path for imports).  SymbolInfo instances are owned by
 * SymbolTable and referenced by pointer throughout the analyzer.
 */
struct SymbolInfo {
    std::string name;          ///< The declared name.
    SymbolKind kind;           ///< What kind of symbol this is.
    Token declToken;           ///< Position of the name token (1-based line/col).
    int scopeLevel = 0;        ///< Nesting depth: 0 = global, 1+ = nested.
    int scopeIndex = -1;       ///< Index into SymbolTable::scopes_.

    /// @brief Type hint from annotation (e.g., `let x: Type`). Empty if none.
    std::string typeHint;

    /// @brief True if the symbol is wrapped in an `export` statement.
    bool isExported = false;

    // ── Callable details (Function / Method / Object) ─────────────────

    /// @brief Parameter names for functions, methods, and object constructors.
    std::vector<std::string> paramNames;

    /// @brief True if the callable has a rest parameter (`...args`).
    bool hasRestParam = false;

    // ── Object details ────────────────────────────────────────────────

    /// @brief Superclass names for Obj declarations (may be empty).
    std::vector<std::string> parentNames;

    /// @brief Method names declared inside an Obj body.
    std::vector<std::string> methodNames;

    // ── Import details ────────────────────────────────────────────────

    /// @brief Raw module path string for import bindings (empty for non-imports).
    std::string importPath;
};

// ═══════════════════════════════════════════════════════════════════════════
// SymbolRef — one usage site (VariableExpr, AssignmentExpr, etc.)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief A single reference (usage site) of a symbol in source code.
 *
 * Collected during AST traversal whenever a variable expression, assignment,
 * or other name-bearing node is visited.  Used by findReferencesTo() and
 * for unused-variable detection.
 */
struct SymbolRef {
    std::string name;   ///< The name being referenced.
    Token token;        ///< Position of the reference (1-based line/col).
    bool isWrite;       ///< true if this is an assignment / l-value write.
};

// ═══════════════════════════════════════════════════════════════════════════
// Scope — one level of the scope stack
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Describes one level of the scope stack.
 *
 * Each scope tracks its nesting depth, parent scope index, the symbols
 * declared within it, and the source range it covers.  When a scope is
 * popped, its endLine is set; unused-symbol detection runs at that point.
 */
struct Scope {
    int level = 0;                  ///< Nesting depth (0 = global).
    int parentIndex = -1;           ///< Index of enclosing scope (-1 for global).
    std::vector<int> symbolIndices; ///< Indices into SymbolTable::symbols_.
    int startLine = INT_MAX;        ///< Earliest declaration line in this scope.
    int endLine = 0;                ///< Latest line covered (set when scope is popped).
};

// ═══════════════════════════════════════════════════════════════════════════
// SymbolTable — scope-aware symbol storage and lookup
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Scope-aware symbol table with nested-scope resolution.
 *
 * Maintains a flat master list of all SymbolInfo records and a scope stack
 * that maps each scope to its declared symbols by index.  Lookup walks the
 * scope chain bottom-up (innermost to global) so that shadowed variables in
 * outer scopes are correctly hidden by inner declarations.
 *
 * The scopeHistory_ vector retains every scope that was ever pushed (even
 * after being popped) so that the full scope tree is available for queries
 * at any cursor position, not just the current scope.
 */
class SymbolTable {
public:
    // ── Scope management ──────────────────────────────────────────────

    /**
     * @brief Push a new nested scope onto the scope stack.
     *
     * The new scope's parent is the previously current scope.  Its level is
     * one greater than the previous scope's level.
     */
    void pushScope();

    /**
     * @brief Pop the current scope from the scope stack.
     *
     * Sets the popped scope's endLine to the latest line encountered.
     * The scope is preserved in scopeHistory_ for post-hoc queries.
     */
    void popScope();

    // ── Symbol registration ───────────────────────────────────────────

    /**
     * @brief Add a symbol to the current scope.
     *
     * If a symbol with the same name exists in an enclosing scope, a
     * shadowing warning is recorded in the provided vector.
     *
     * @param sym     The symbol metadata to register.
     * @param shadows Optional output vector for shadowing warnings.
     *                Each entry is (shadowed_symbol, shadowing_symbol).
     * @return The master index of the newly added symbol.
     */
    int addSymbol(SymbolInfo sym,
                  std::vector<std::pair<const SymbolInfo*, const SymbolInfo*>>* shadows = nullptr);

    // ── Lookup ────────────────────────────────────────────────────────

    /**
     * @brief Resolve a name to its innermost declaration (bottom-up).
     *
     * Walks the scope chain from innermost to global, returning the first
     * match.  Handles shadowing correctly: if an inner scope declares the
     * same name, the outer declaration is hidden.
     *
     * @param name The name to look up.
     * @return Pointer to the SymbolInfo, or nullptr if not found.
     */
    const SymbolInfo* resolve(const std::string& name) const;

    /**
     * @brief Check whether a name is already declared in the current scope.
     *
     * Does NOT walk enclosing scopes -- only checks the innermost scope.
     * Used for detecting duplicate declarations within the same scope.
     *
     * @param name The name to check.
     * @return Pointer to the SymbolInfo if found in current scope, nullptr otherwise.
     */
    const SymbolInfo* resolveInCurrentScope(const std::string& name) const;

    /**
     * @brief Return all symbols visible from the current scope.
     *
     * Results are ordered: current-scope locals first, then enclosing scopes
     * bottom-up, then globals.  Within each scope, symbols are in declaration
     * order.
     *
     * @return Vector of pointers to visible SymbolInfo records.
     */
    std::vector<const SymbolInfo*> visibleSymbols() const;

    // ── Accessors ─────────────────────────────────────────────────────

    /// @return Const reference to the master symbol list (all scopes).
    const std::vector<SymbolInfo>& allSymbols() const { return symbols_; }

    /// @return Const reference to the current scope stack.
    const std::vector<Scope>& allScopes() const { return scopes_; }

    /// @return Const reference to the full scope tree history (never popped).
    const std::vector<Scope>& allScopesHistory() const { return scopeHistory_; }

    /// @return Current nesting level (0 = global).
    int currentLevel() const { return currentLevel_; }

    /// @return Index of the current scope within the scopes_ vector.
    int currentScopeIndex() const { return static_cast<int>(scopes_.size()) - 1; }

private:
    std::vector<SymbolInfo> symbols_;   ///< Master symbol list (all scopes).
    std::vector<Scope> scopes_;         ///< Current scope stack (pushed/popped).
    std::vector<Scope> scopeHistory_;   ///< Full scope tree -- never popped.
    int currentLevel_ = 0;              ///< Current nesting depth.
};

// ═══════════════════════════════════════════════════════════════════════════
// SemanticAnalyzer — the main analysis pass
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Scope-aware, read-only AST analysis pass for the Vora LSP server.
 *
 * Walks a Program AST without emitting bytecode, building a SymbolTable and
 * collecting symbol references, exports, and diagnostics (unused locals,
 * unreachable code, shadowed variables).  Implements ExprVisitor<void>,
 * StmtVisitor<void>, and ProgramVisitor<void> -- the void return type is
 * already wired in the AST node dispatch, so no new accept() overloads are
 * needed.
 *
 * Query methods (findDeclarationAt, findReferencesTo, getVisibleSymbols,
 * findSymbolAt) use 1-based line/column positions matching Token storage;
 * conversion to 0-based LSP format is the caller's responsibility.
 *
 * Thread-safety: Instances are not thread-safe.  Each analysis session
 * should create its own SemanticAnalyzer.
 */
class SemanticAnalyzer : public ExprVisitor<void>,
                         public StmtVisitor<void>,
                         public ProgramVisitor<void> {
public:
    SemanticAnalyzer() = default;

    // ── Entry point ───────────────────────────────────────────────────

    /**
     * @brief Run the full semantic analysis pass on a parsed Program.
     *
     * Populates the symbol table, reference list, and diagnostic collections.
     * Must be called before any query methods.
     *
     * @param program The root AST node from the parser.
     */
    void analyze(const Program& program);

    // ── Query API (positions are 1-based line/column) ─────────────────

    /**
     * @brief Find the declaration that defines the symbol at (line, col).
     *
     * Searches the symbol table for a declaration whose token range contains
     * the given position.
     *
     * @param line 1-based line number.
     * @param col  1-based column number.
     * @return Pointer to the SymbolInfo, or nullptr if no declaration found.
     */
    const SymbolInfo* findDeclarationAt(int line, int col) const;

    /**
     * @brief Find all reference sites for the symbol at (line, col).
     *
     * If the position falls on a declaration, finds all references to that
     * declaration.  If the position falls on a reference, resolves to the
     * declaration first, then returns all references.
     *
     * @param line 1-based line number.
     * @param col  1-based column number.
     * @return Vector of SymbolRef entries (may be empty).
     */
    std::vector<SymbolRef> findReferencesTo(int line, int col) const;

    /**
     * @brief Return all symbols visible at the given cursor position.
     *
     * Walks the scope chain at the position and returns symbols ordered from
     * innermost scope to global, with same-scope locals first, then enclosing
     * scopes, then globals.  Within each group, declaration order is preserved.
     *
     * @param line 1-based line number.
     * @param col  1-based column number.
     * @return Vector of pointers to visible SymbolInfo records.
     */
    std::vector<const SymbolInfo*> getVisibleSymbols(int line, int col) const;

    /**
     * @brief Find symbol information for hover display at (line, col).
     *
     * Prefers returning declaration info when the cursor is on a declaration;
     * otherwise resolves a reference to its declaration.
     *
     * @param line 1-based line number.
     * @param col  1-based column number.
     * @return Pointer to the SymbolInfo, or nullptr if nothing is at that position.
     */
    const SymbolInfo* findSymbolAt(int line, int col) const;

    // ── Export access (for cross-file resolution) ─────────────────────
    /// @brief Symbols marked with `export` (for cross-file import resolution).
    const std::vector<const SymbolInfo*>& getExportedSymbols() const {
        return exportedSymbols_;
    }

    // ── Diagnostic data ───────────────────────────────────────────────
    /// @brief Symbols that were declared but never referenced.
    const std::vector<const SymbolInfo*>& getUnusedSymbols() const {
        return unusedSymbols_;
    }
    /// @brief Tokens positioned after an unconditional control-flow break
    ///        (return, throw, break, continue) and thus unreachable.
    const std::vector<Token>& getUnreachableTokens() const {
        return unreachableTokens_;
    }
    /// @brief Pairs of (shadowed_symbol, shadowing_symbol) from inner-scope
    ///        declarations that hide outer-scope names.
    const std::vector<std::pair<const SymbolInfo*, const SymbolInfo*>>&
    getShadowedSymbols() const {
        return shadowedSymbols_;
    }

    // ── Visitor dispatch ──────────────────────────────────────────────

    // ProgramVisitor

    /// @brief Analyze the root Program node: enter global scope, walk all statements.
    void visitProgram(const Program& program) override;

    // StmtVisitor

    /// @brief Visit an expression statement: evaluate the inner expression.
    void visitExprStmt(const ExprStmt& stmt) override;
    /// @brief Register a `let` variable declaration in the current scope.
    void visitLetStmt(const LetStmt& stmt) override;
    /// @brief Enter a new scope for a block statement, then restore on exit.
    void visitBlockStmt(const BlockStmt& stmt) override;
    /// @brief Mark subsequent code unreachable after a return statement.
    void visitReturnStmt(const ReturnStmt& stmt) override;
    /// @brief Visit an if/else-if/else chain, tracking reachability in each branch.
    void visitIfStmt(const IfStmt& stmt) override;
    /// @brief Visit a while-loop body; mark code after an infinite loop unreachable.
    void visitWhileStmt(const WhileStmt& stmt) override;
    /// @brief Visit a do-while loop body and condition.
    void visitDoWhileStmt(const DoWhileStmt& stmt) override;
    /// @brief Visit a for-in loop: registers the iteration variable in a nested scope.
    void visitForStmt(const ForStmt& stmt) override;
    /// @brief Visit a C-style for-loop: init, condition, increment, body.
    void visitCForStmt(const CForStmt& stmt) override;
    /// @brief Register a named function declaration with parameters.
    void visitFuncStmt(const FuncStmt& stmt) override;
    /// @brief Register an Obj (class) declaration with parents and methods.
    void visitObjStmt(const ObjStmt& stmt) override;
    /// @brief Mark code after break unreachable within the current loop.
    void visitBreakStmt(const BreakStmt& stmt) override;
    /// @brief Mark code after continue unreachable within the current loop.
    void visitContinueStmt(const ContinueStmt& stmt) override;
    /// @brief Visit try/catch/finally: registers catch variable, tracks control flow.
    void visitTryStmt(const TryStmt& stmt) override;
    /// @brief Mark code after throw unreachable.
    void visitThrowStmt(const ThrowStmt& stmt) override;
    /// @brief Register an import binding (module namespace or from-import name).
    void visitImportStmt(const ImportStmt& stmt) override;
    /// @brief Mark the inner declaration as exported.
    void visitExportStmt(const ExportStmt& stmt) override;
    /// @brief Visit a defer statement: the deferred expression/block is analyzed
    ///        but not marked for immediate execution.
    void visitDeferStmt(const DeferStmt& stmt) override;
    /// @brief Visit an error statement: no-op for semantic analysis (skipped).
    void visitErrorStmt(const ErrorStmt& stmt) override;

    // ExprVisitor

    /// @brief Visit a literal expression: no semantic side effects.
    void visitLiteralExpr(const LiteralExpr& expr) override;
    /// @brief Visit a binary operator expression: analyze left and right operands.
    void visitBinaryExpr(const BinaryExpr& expr) override;
    /// @brief Visit a parenthesized grouping expression.
    void visitGroupingExpr(const GroupingExpr& expr) override;
    /// @brief Visit a unary operator expression.
    void visitUnaryExpr(const UnaryExpr& expr) override;
    /// @brief Record a variable reference (read) at this name token.
    void visitVariableExpr(const VariableExpr& expr) override;
    /// @brief Record a variable write (assignment) at this name token.
    void visitAssignmentExpr(const AssignmentExpr& expr) override;
    /// @brief Record a variable write via compound assignment (+=, -=, etc.).
    void visitCompoundAssignmentExpr(const CompoundAssignmentExpr& expr) override;
    /// @brief Visit a function call expression: analyze callee and arguments.
    void visitCallExpr(const CallExpr& expr) override;
    /// @brief Visit an array literal: analyze each element expression.
    void visitArrayExpr(const ArrayExpr& expr) override;
    /// @brief Visit a dict literal: analyze each key and value expression.
    void visitDictExpr(const DictExpr& expr) override;
    /// @brief Visit an index expression (e.g., `arr[i]`).
    void visitIndexExpr(const IndexExpr& expr) override;
    /// @brief Visit a property access expression (e.g., `obj.prop`).
    void visitPropertyExpr(const PropertyExpr& expr) override;
    /// @brief Visit a property assignment (e.g., `obj.prop = val`).
    void visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) override;
    /// @brief Visit an index assignment (e.g., `arr[i] = val`).
    void visitIndexAssignmentExpr(const IndexAssignmentExpr& expr) override;
    /// @brief Visit a `this` expression: valid only inside an Obj method.
    void visitThisExpr(const ThisExpr& expr) override;
    /// @brief Visit a `super` expression: valid only inside an Obj method with parent.
    void visitSuperExpr(const SuperExpr& expr) override;
    /// @brief Visit an increment/decrement expression (prefix or postfix).
    void visitIncDecExpr(const IncDecExpr& expr) override;
    /// @brief Visit a ternary conditional expression (`cond ? then : else`).
    void visitTernaryExpr(const TernaryExpr& expr) override;
    /// @brief Visit a match expression: analyze the scrutinee and each case arm.
    void visitMatchExpr(const MatchExpr& expr) override;
    /// @brief Visit a function expression (anonymous function literal).
    void visitFuncExpr(const FuncExpr& expr) override;
    /// @brief Visit a yield expression: valid only inside a generator function.
    void visitYieldExpr(const YieldExpr& expr) override;
    /// @brief Visit a destructuring assignment (array or object pattern).
    void visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) override;
    /// @brief Visit a spread expression (`...expr`).
    void visitSpreadExpr(const SpreadExpr& expr) override;
    /// @brief Visit a list comprehension expression.
    void visitListCompExpr(const ListCompExpr& expr) override;
    /// @brief Visit a dict comprehension expression.
    void visitDictCompExpr(const DictCompExpr& expr) override;
    /// @brief Visit an optional chaining expression (`?.`, `?[`).
    void visitOptionalChainExpr(const OptionalChainExpr& expr) override;
    /// @brief Visit an error expression: no-op for semantic analysis (skipped).
    void visitErrorExpr(const ErrorExpr& expr) override;

private:
    // ── State ─────────────────────────────────────────────────────────

    /// @brief The scope-aware symbol table built during analysis.
    SymbolTable table_;
    /// @brief All reference sites collected during traversal (name + position + read/write).
    std::vector<SymbolRef> references_;
    /// @brief Pointers to symbols marked with `export` for cross-file resolution.
    std::vector<const SymbolInfo*> exportedSymbols_;

    // Post-pass diagnostic collection
    /// @brief Symbols that were declared but never referenced (populated after analysis).
    std::vector<const SymbolInfo*> unusedSymbols_;
    /// @brief Token positions of unreachable code (after return/throw/break/continue).
    std::vector<Token> unreachableTokens_;
    /// @brief Pairs of (shadowed, shadowing) symbols from inner-scope re-declarations.
    std::vector<std::pair<const SymbolInfo*, const SymbolInfo*>> shadowedSymbols_;

    // Reachability tracking
    /// @brief Whether code at the current traversal point is reachable.
    ///        Set to false after return, throw, break, continue; restored on scope pop.
    bool reachable_ = true;

    /// @brief Set of SymbolInfo master indices that have been referenced at least once.
    ///        Used at scope-pop time to detect unused locals.
    std::unordered_set<int> referencedSymbolIndices_;

    /// @brief Monotonically increasing counter for assigning unique scope indices.
    int nextScopeIndex_ = 0;

    // ── Helpers ───────────────────────────────────────────────────────

    /**
     * @brief Check whether a token's source range contains a given position.
     *
     * Accounts for multi-line tokens by comparing line/column ranges.
     *
     * @param tok  The token to check.
     * @param line 1-based line number.
     * @param col  1-based column number.
     * @return true if the position falls within the token's range.
     */
    static bool tokenContains(const Token& tok, int line, int col);

    /**
     * @brief Record a variable reference (read or write) at a token position.
     *
     * Appends to the references_ vector and marks the symbol as referenced
     * for unused-variable detection.
     *
     * @param name    The name being referenced.
     * @param token   The token at the reference site.
     * @param isWrite true for assignments / l-value writes, false for reads.
     */
    void addReference(const std::string& name, const Token& token, bool isWrite);

    /**
     * @brief Mark a symbol as referenced for unused-variable detection.
     *
     * Looks up the innermost declaration of @p name and adds its master
     * index to referencedSymbolIndices_.  A no-op if the name is not found.
     *
     * @param name The symbol name to mark as used.
     */
    void markReferenced(const std::string& name);

    /**
     * @brief Record a token position as unreachable code.
     *
     * Only records the position if the current reachability flag is false
     * (i.e., we are after a return, throw, break, or continue).
     *
     * @param token The token at the unreachable position.
     */
    void markUnreachable(const Token& token);

    /**
     * @brief Push a new nested scope and return the previous reachability state.
     *
     * Resets reachability to true for the new scope.
     *
     * @return The reachability state before the push (for restoration on pop).
     */
    bool pushScope();

    /**
     * @brief Pop the current scope and restore the previous reachability state.
     *
     * After popping, checks for unused local symbols in the closed scope and
     * adds them to unusedSymbols_.
     */
    void popScope();

    /**
     * @brief Visit a sequence of statements.
     *
     * Shared by BlockStmt, function bodies, and other multi-statement contexts.
     * Each statement is visited sequentially with reachability tracking.
     *
     * @param statements The statement list to analyze.
     */
    void visitStatements(const std::vector<std::unique_ptr<Stmt>>& statements);

    /**
     * @brief Visit an expression, returning to outer scope after traversal.
     *
     * A thin wrapper that delegates to expr.accept(*this).
     *
     * @param expr The expression to analyze.
     */
    void visitExpr(const Expr& expr);

    /**
     * @brief Register a declaration symbol in the current scope.
     *
     * Delegates to table_.addSymbol() and records shadowing warnings.
     *
     * @param sym The symbol metadata to register.
     * @return The master index of the newly added symbol.
     */
    int addDecl(const SymbolInfo& sym);
};

}  // namespace vora

#endif  // VORA_LSP_SEMANTIC_ANALYZER_H
