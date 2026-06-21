/**
 * Semantic Analyzer — scope-aware AST analysis pass for LSP.
 *
 * Walks the AST (read-only) and builds a scope-annotated symbol table.
 * No bytecode emission, no VM dependency — purely structural analysis.
 *
 * Implements ExprVisitor<void>, StmtVisitor<void>, ProgramVisitor<void>
 * (no new accept() overloads required since void is already wired).
 *
 * Query API:
 *   findDeclarationAt(line, col)  — go-to-definition source
 *   findReferencesTo(line, col)   — find-references source
 *   getVisibleSymbols(line, col)  — completion source
 *   findSymbolAt(line, col)       — hover source
 *
 * Also collects semantic diagnostics:
 *   - Unused local variables
 *   - Unreachable code after return/throw/break/continue
 *   - Shadowed variable declarations
 *
 * Position convention: ALL positions are 1-based line/column
 * (matching Token storage). Conversion to 0-based LSP format
 * happens in the LSP server layer (tokenToLspRange).
 */

#ifndef VORA_LSP_SEMANTIC_ANALYZER_H
#define VORA_LSP_SEMANTIC_ANALYZER_H

#include "ast/expr.h"
#include "ast/stmt.h"
#include "ast/program.h"
#include "ast/expr_visitor.h"
#include "ast/stmt_visitor.h"
#include "lexer/token.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vora {

// ═══════════════════════════════════════════════════════════════════════════
// Symbol classification
// ═══════════════════════════════════════════════════════════════════════════

enum class SymbolKind : uint8_t {
    Variable,   // let
    Constant,   // const
    Function,   // func name (named function)
    Parameter,  // function / object constructor parameter
    Object,     // Obj name (class-like)
    Method,     // method inside an Obj
    Import,     // import binding (module or from-import name)
    ForVar,     // for-in loop variable
    CatchVar,   // catch(e) variable
};

const char* symbolKindToString(SymbolKind kind);

// ═══════════════════════════════════════════════════════════════════════════
// SymbolInfo — one declared symbol
// ═══════════════════════════════════════════════════════════════════════════

struct SymbolInfo {
    std::string name;
    SymbolKind kind;
    Token declToken;            // position of the name token (1-based)
    int scopeLevel = 0;         // 0 = global, 1+ = nested depth
    int scopeIndex = -1;        // index into the scope stack

    // Type hint from annotation (let x: Type) — empty if none.
    std::string typeHint;

    // True if wrapped in an export statement.
    bool isExported = false;

    // ── Callable details (Function / Method / Object) ─────────────────
    std::vector<std::string> paramNames;
    bool hasRestParam = false;

    // ── Object details ────────────────────────────────────────────────
    std::vector<std::string> parentNames;   // superclass names (may be empty)
    std::vector<std::string> methodNames;   // method names declared in the Obj

    // ── Import details ────────────────────────────────────────────────
    std::string importPath;  // raw module path string (empty for non-imports)
};

// ═══════════════════════════════════════════════════════════════════════════
// SymbolRef — one usage site (VariableExpr, AssignmentExpr, etc.)
// ═══════════════════════════════════════════════════════════════════════════

struct SymbolRef {
    std::string name;
    Token token;        // position of the reference (1-based)
    bool isWrite;       // true if this is an assignment / l-value
};

// ═══════════════════════════════════════════════════════════════════════════
// Scope — one level of the scope stack
// ═══════════════════════════════════════════════════════════════════════════

struct Scope {
    int level = 0;                       // nesting depth (0 = global)
    int parentIndex = -1;                // index of enclosing scope (-1 for global)
    std::vector<int> symbolIndices;      // indices into the master symbol list
};

// ═══════════════════════════════════════════════════════════════════════════
// SymbolTable — scope-aware symbol storage and lookup
// ═══════════════════════════════════════════════════════════════════════════

class SymbolTable {
public:
    // ── Scope management ──────────────────────────────────────────────
    void pushScope();
    void popScope();

    // ── Symbol registration ───────────────────────────────────────────
    // Adds a symbol to the current scope. Returns its master index.
    // Emits shadowing warnings into the provided vector (if not null).
    int addSymbol(SymbolInfo sym,
                  std::vector<std::pair<const SymbolInfo*, const SymbolInfo*>>* shadows = nullptr);

    // ── Lookup ────────────────────────────────────────────────────────
    // Resolve a name to its innermost declaration (bottom-up).
    const SymbolInfo* resolve(const std::string& name) const;

    // Check if name already exists in the current scope only.
    const SymbolInfo* resolveInCurrentScope(const std::string& name) const;

    // Return all symbols visible from the current scope (including enclosing).
    std::vector<const SymbolInfo*> visibleSymbols() const;

    // ── Accessors ─────────────────────────────────────────────────────
    const std::vector<SymbolInfo>& allSymbols() const { return symbols_; }
    const std::vector<Scope>& allScopes() const { return scopes_; }
    int currentLevel() const { return currentLevel_; }
    int currentScopeIndex() const { return static_cast<int>(scopes_.size()) - 1; }

private:
    std::vector<SymbolInfo> symbols_;
    std::vector<Scope> scopes_;
    int currentLevel_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// SemanticAnalyzer — the main analysis pass
// ═══════════════════════════════════════════════════════════════════════════

class SemanticAnalyzer : public ExprVisitor<void>,
                         public StmtVisitor<void>,
                         public ProgramVisitor<void> {
public:
    SemanticAnalyzer() = default;

    // ── Entry point ───────────────────────────────────────────────────
    void analyze(const Program& program);

    // ── Query API (positions are 1-based line/column) ─────────────────

    /// Find the declaration that defines the symbol at (line, col).
    /// Returns nullptr if no declaration found at that position.
    const SymbolInfo* findDeclarationAt(int line, int col) const;

    /// Find all reference sites for the symbol at (line, col).
    /// If the position is on a declaration, finds all references to it.
    /// If the position is on a reference, resolves to declaration first.
    std::vector<SymbolRef> findReferencesTo(int line, int col) const;

    /// Return all symbols visible at the given cursor position.
    /// Results are ordered: same-scope locals first, then enclosing scopes,
    /// then globals; within each group, declaration-order.
    std::vector<const SymbolInfo*> getVisibleSymbols(int line, int col) const;

    /// Find symbol info for hover at (line, col).
    /// Returns declaration info (preferred) or reference-to-declaration.
    const SymbolInfo* findSymbolAt(int line, int col) const;

    // ── Export access (for cross-file resolution) ─────────────────────
    const std::vector<const SymbolInfo*>& getExportedSymbols() const {
        return exportedSymbols_;
    }

    // ── Diagnostic data ───────────────────────────────────────────────
    const std::vector<const SymbolInfo*>& getUnusedSymbols() const {
        return unusedSymbols_;
    }
    const std::vector<Token>& getUnreachableTokens() const {
        return unreachableTokens_;
    }
    const std::vector<std::pair<const SymbolInfo*, const SymbolInfo*>>&
    getShadowedSymbols() const {
        return shadowedSymbols_;
    }

    // ── Visitor dispatch ──────────────────────────────────────────────

    // ProgramVisitor
    void visitProgram(const Program& program) override;

    // StmtVisitor
    void visitExprStmt(const ExprStmt& stmt) override;
    void visitLetStmt(const LetStmt& stmt) override;
    void visitBlockStmt(const BlockStmt& stmt) override;
    void visitReturnStmt(const ReturnStmt& stmt) override;
    void visitIfStmt(const IfStmt& stmt) override;
    void visitWhileStmt(const WhileStmt& stmt) override;
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
    void visitErrorStmt(const ErrorStmt& stmt) override;

    // ExprVisitor
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
    void visitFuncExpr(const FuncExpr& expr) override;
    void visitYieldExpr(const YieldExpr& expr) override;
    void visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) override;
    void visitErrorExpr(const ErrorExpr& expr) override;

private:
    // ── State ─────────────────────────────────────────────────────────
    SymbolTable table_;
    std::vector<SymbolRef> references_;
    std::vector<const SymbolInfo*> exportedSymbols_;

    // Post-pass diagnostic collection
    std::vector<const SymbolInfo*> unusedSymbols_;
    std::vector<Token> unreachableTokens_;
    std::vector<std::pair<const SymbolInfo*, const SymbolInfo*>> shadowedSymbols_;

    // Reachability tracking
    bool reachable_ = true;

    // Set of symbol indices that have been referenced at least once
    std::unordered_set<int> referencedSymbolIndices_;

    // Counter for generating unique scope indices
    int nextScopeIndex_ = 0;

    // ── Helpers ───────────────────────────────────────────────────────

    /// Check if a token's position range contains the given line/col.
    static bool tokenContains(const Token& tok, int line, int col);

    /// Register a variable reference at the given token position.
    void addReference(const std::string& name, const Token& token, bool isWrite);

    /// Mark the symbol with the given name as referenced (for unused detection).
    /// Looks up innermost scope first.
    void markReferenced(const std::string& name);

    /// Mark a token position as unreachable code.
    void markUnreachable(const Token& token);

    /// Push a new scope and return the previous reachability state.
    bool pushScope();

    /// Pop the current scope, checking for unused locals.
    void popScope();

    /// Visit a statement list (shared by BlockStmt, function bodies, etc.)
    void visitStatements(const std::vector<std::unique_ptr<Stmt>>& statements);

    /// Visit an expression, returning to outer scope after.
    void visitExpr(const Expr& expr);

    /// Add a declaration symbol and return its master index.
    int addDecl(const SymbolInfo& sym);
};

}  // namespace vora

#endif  // VORA_LSP_SEMANTIC_ANALYZER_H
