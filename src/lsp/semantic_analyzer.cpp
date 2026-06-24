/**
 * Semantic Analyzer implementation.
 *
 * Walks the AST with scope awareness to build a symbol table, collect
 * references, and detect semantic diagnostics (unused vars, unreachable
 * code, shadowed declarations).
 */

#include "lsp/semantic_analyzer.h"

#include <algorithm>
#include <cctype>

namespace vora {

// ═══════════════════════════════════════════════════════════════════════════
// SymbolKind → string
// ═══════════════════════════════════════════════════════════════════════════

const char* symbolKindToString(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Variable:  return "variable";
        case SymbolKind::Constant:  return "constant";
        case SymbolKind::Function:  return "function";
        case SymbolKind::Parameter: return "parameter";
        case SymbolKind::Object:    return "object";
        case SymbolKind::Method:    return "method";
        case SymbolKind::Import:    return "import";
        case SymbolKind::ForVar:    return "for-var";
        case SymbolKind::CatchVar:  return "catch-var";
    }
    return "unknown";
}

// ═══════════════════════════════════════════════════════════════════════════
// SymbolTable
// ═══════════════════════════════════════════════════════════════════════════

void SymbolTable::pushScope() {
    Scope scope;
    scope.level = currentLevel_;
    scope.parentIndex = static_cast<int>(scopes_.size()) - 1;  // previous scope
    scopes_.push_back(scope);
    scopeHistory_.push_back(scope);  // persistent copy for post-analysis queries
    currentLevel_++;
}

int SymbolTable::addSymbol(SymbolInfo sym,
                           std::vector<std::pair<const SymbolInfo*, const SymbolInfo*>>* shadows) {
    // Set scope level from the current scope (before pushing new symbol).
    sym.scopeLevel = scopes_.empty() ? currentLevel_ : scopes_.back().level;
    sym.scopeIndex = static_cast<int>(scopes_.size()) - 1;

    // Check for shadowing — does the name exist in any outer scope?
    // Do this BEFORE push_back so we compare against existing symbols.
    if (shadows) {
        const int newScopeLevel = sym.scopeLevel;
        for (int si = static_cast<int>(scopes_.size()) - 1; si >= 0; si--) {
            // Only check outer scopes (not the current one).
            if (scopes_[si].level >= newScopeLevel) continue;
            for (int symIdx : scopes_[si].symbolIndices) {
                if (symbols_[symIdx].name == sym.name) {
                    // Found an outer-scope symbol with the same name.
                    // Push back the new symbol first, then capture its address.
                    int newIdx = static_cast<int>(symbols_.size());
                    symbols_.push_back(sym);
                    if (!scopes_.empty()) {
                        scopes_.back().symbolIndices.push_back(newIdx);
                    }
                    shadows->emplace_back(&symbols_[newIdx], &symbols_[symIdx]);
                    return newIdx;
                }
            }
        }
    }

    int index = static_cast<int>(symbols_.size());
    symbols_.push_back(std::move(sym));

    if (!scopes_.empty()) {
        int scopeIdx = static_cast<int>(scopes_.size()) - 1;
        scopes_.back().symbolIndices.push_back(index);
        // Track scope line range on both active and history scopes.
        const auto& s = symbols_.back();
        if (s.declToken.line > 0) {
            auto updateRange = [&](Scope& scope) {
                if (s.declToken.line < scope.startLine) scope.startLine = s.declToken.line;
                if (s.declToken.line > scope.endLine) scope.endLine = s.declToken.line;
            };
            updateRange(scopes_.back());
            if (scopeIdx < static_cast<int>(scopeHistory_.size())) {
                updateRange(scopeHistory_[scopeIdx]);
            }
        }
    }

    return index;
}

void SymbolTable::popScope() {
    if (!scopes_.empty()) {
        // Propagate endLine to parent scope before popping.
        int endingLine = scopes_.back().endLine;
        int parentIdx = scopes_.back().parentIndex;
        scopes_.pop_back();
        if (parentIdx >= 0 && parentIdx < static_cast<int>(scopes_.size())) {
            if (endingLine > scopes_[parentIdx].endLine) {
                scopes_[parentIdx].endLine = endingLine;
            }
        }
    }
    if (currentLevel_ > 0) {
        currentLevel_--;
    }
    // Ensure there's always at least the global scope.
    if (scopes_.empty()) {
        pushScope();
    }
}

const SymbolInfo* SymbolTable::resolve(const std::string& name) const {
    // Walk scopes from innermost to outermost.
    for (int si = static_cast<int>(scopes_.size()) - 1; si >= 0; si--) {
        // Search symbols in reverse order within each scope
        // (most recent declaration first).
        const auto& indices = scopes_[si].symbolIndices;
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
            if (symbols_[*it].name == name) {
                return &symbols_[*it];
            }
        }
    }
    return nullptr;
}

const SymbolInfo* SymbolTable::resolveInCurrentScope(const std::string& name) const {
    if (scopes_.empty()) return nullptr;
    const auto& indices = scopes_.back().symbolIndices;
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        if (symbols_[*it].name == name) {
            return &symbols_[*it];
        }
    }
    return nullptr;
}

std::vector<const SymbolInfo*> SymbolTable::visibleSymbols() const {
    std::vector<const SymbolInfo*> result;
    // Walk scopes from innermost to outermost.
    for (int si = static_cast<int>(scopes_.size()) - 1; si >= 0; si--) {
        for (int symIdx : scopes_[si].symbolIndices) {
            result.push_back(&symbols_[symIdx]);
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// SemanticAnalyzer — helpers
// ═══════════════════════════════════════════════════════════════════════════

bool SemanticAnalyzer::tokenContains(const Token& tok, int line, int col) {
    if (tok.line != line) return false;
    int tokCol = tok.column;
    int tokEnd = tokCol + static_cast<int>(tok.lexeme.size());
    return col >= tokCol && col < tokEnd;
}

void SemanticAnalyzer::addReference(const std::string& name, const Token& token, bool isWrite) {
    references_.push_back({name, token, isWrite});
}

void SemanticAnalyzer::markReferenced(const std::string& name) {
    // Find the symbol this name resolves to and mark its index.
    const auto* sym = table_.resolve(name);
    if (sym) {
        // Find the master index of this symbol.
        const auto& allSyms = table_.allSymbols();
        for (size_t i = 0; i < allSyms.size(); i++) {
            if (&allSyms[i] == sym) {
                referencedSymbolIndices_.insert(static_cast<int>(i));
                break;
            }
        }
    }
}

void SemanticAnalyzer::markUnreachable(const Token& token) {
    unreachableTokens_.push_back(token);
}

bool SemanticAnalyzer::pushScope() {
    table_.pushScope();
    bool prev = reachable_;
    reachable_ = true;  // new scope starts reachable
    return prev;
}

void SemanticAnalyzer::popScope() {
    // Before popping, collect unused locals in this scope.
    if (!table_.allScopes().empty()) {
        const auto& scope = table_.allScopes().back();
        const auto& allSyms = table_.allSymbols();
        for (int symIdx : scope.symbolIndices) {
            const auto& sym = allSyms[symIdx];
            // Skip non-local symbols (global scope vars aren't "unused" in
            // the same sense — they could be used by importers).
            if (sym.scopeLevel == 0) continue;
            // Skip exported symbols, catch vars, and _-prefixed names.
            if (sym.isExported) continue;
            if (sym.kind == SymbolKind::CatchVar) continue;
            if (!sym.name.empty() && sym.name[0] == '_') continue;
            // Skip parameters (they're part of the public API).
            if (sym.kind == SymbolKind::Parameter) continue;

            if (referencedSymbolIndices_.count(symIdx) == 0) {
                unusedSymbols_.push_back(&sym);
            }
        }
    }
    table_.popScope();
}

void SemanticAnalyzer::visitStatements(const std::vector<std::unique_ptr<Stmt>>& statements) {
    for (auto& stmt : statements) {
        if (!stmt) continue;
        if (!reachable_ && !dynamic_cast<const FuncStmt*>(stmt.get())
                        && !dynamic_cast<const ObjStmt*>(stmt.get())
                        && !dynamic_cast<const ImportStmt*>(stmt.get())
                        && !dynamic_cast<const ExportStmt*>(stmt.get())) {
            // Mark unreachable code. We still visit FuncStmt/ObjStmt/ImportStmt/
            // ExportStmt because they're declarations that may be referenced.
            if (auto* let = dynamic_cast<const LetStmt*>(stmt.get())) {
                markUnreachable(let->nameToken);
            } else if (auto* exprStmt = dynamic_cast<const ExprStmt*>(stmt.get())) {
                // Try to get a token from the expression.
                stmt->accept(*this);  // still visit for reference tracking
                continue;
            }
        }
        stmt->accept(*this);
    }
}

void SemanticAnalyzer::visitExpr(const Expr& expr) {
    expr.accept(*this);
}

int SemanticAnalyzer::addDecl(const SymbolInfo& sym) {
    return table_.addSymbol(sym, &shadowedSymbols_);
}

// ═══════════════════════════════════════════════════════════════════════════
// SemanticAnalyzer — entry point
// ═══════════════════════════════════════════════════════════════════════════

void SemanticAnalyzer::analyze(const Program& program) {
    // Reset state for a fresh analysis.
    table_ = SymbolTable();
    references_.clear();
    exportedSymbols_.clear();
    unusedSymbols_.clear();
    unreachableTokens_.clear();
    shadowedSymbols_.clear();
    referencedSymbolIndices_.clear();
    reachable_ = true;

    // Create the global scope.
    table_.pushScope();

    program.accept(*this);
}

// ═══════════════════════════════════════════════════════════════════════════
// SemanticAnalyzer — Program visitor
// ═══════════════════════════════════════════════════════════════════════════

void SemanticAnalyzer::visitProgram(const Program& program) {
    visitStatements(program.statements);

    // Post-pass: collect unused global symbols.
    // Keep the global scope alive for query API after analysis.
    const auto& allSyms = table_.allSymbols();
    for (auto& sym : allSyms) {
        if (sym.scopeLevel != 0) continue;  // only check globals
        if (sym.isExported) continue;
        if (!sym.name.empty() && sym.name[0] == '_') continue;
        if (sym.kind == SymbolKind::Import) continue;
        if (sym.kind == SymbolKind::Function) continue;
        if (sym.kind == SymbolKind::Object) continue;

        // Find the master index.
        for (size_t i = 0; i < allSyms.size(); i++) {
            if (&allSyms[i] == &sym) {
                if (referencedSymbolIndices_.count(static_cast<int>(i)) == 0) {
                    unusedSymbols_.push_back(&sym);
                }
                break;
            }
        }
    }
    // NOTE: do NOT pop the global scope — query API needs it alive.
}

// ═══════════════════════════════════════════════════════════════════════════
// SemanticAnalyzer — Stmt visitors
// ═══════════════════════════════════════════════════════════════════════════

void SemanticAnalyzer::visitExprStmt(const ExprStmt& stmt) {
    if (stmt.expression) {
        visitExpr(*stmt.expression);
    }
}

void SemanticAnalyzer::visitLetStmt(const LetStmt& stmt) {
    if (stmt.binding) {
        // Destructured binding — register symbols for all bound names.
        auto names = stmt.binding->getBoundNames();
        for (const auto& name : names) {
            SymbolInfo sym;
            sym.name = name;
            sym.kind = stmt.isConst ? SymbolKind::Constant : SymbolKind::Variable;
            sym.declToken = stmt.binding->startToken;
            sym.typeHint = stmt.typeAnnotation;
            sym.isExported = false;
            addDecl(sym);
        }
    } else {
        SymbolInfo sym;
        sym.name = stmt.name;
        sym.kind = stmt.isConst ? SymbolKind::Constant : SymbolKind::Variable;
        sym.declToken = stmt.nameToken;
        sym.typeHint = stmt.typeAnnotation;
        sym.isExported = false;  // ExportStmt sets this via visitExportStmt
        addDecl(sym);
    }

    if (stmt.initializer) {
        visitExpr(*stmt.initializer);
    }
}

void SemanticAnalyzer::visitBlockStmt(const BlockStmt& stmt) {
    pushScope();
    visitStatements(stmt.statements);
    popScope();
}

void SemanticAnalyzer::visitReturnStmt(const ReturnStmt& stmt) {
    if (stmt.value) {
        visitExpr(*stmt.value);
    }
    reachable_ = false;
}

void SemanticAnalyzer::visitIfStmt(const IfStmt& stmt) {
    if (stmt.condition) {
        visitExpr(*stmt.condition);
    }

    bool thenReachable = reachable_;
    if (stmt.thenBranch) {
        reachable_ = true;
        stmt.thenBranch->accept(*this);
        thenReachable = reachable_;
    }

    bool elseReachable = reachable_;
    if (stmt.elseBranch) {
        reachable_ = true;
        stmt.elseBranch->accept(*this);
        elseReachable = reachable_;
    }

    // After if/else: reachable if either branch is reachable.
    reachable_ = thenReachable || elseReachable;
}

void SemanticAnalyzer::visitWhileStmt(const WhileStmt& stmt) {
    if (stmt.condition) {
        visitExpr(*stmt.condition);
    }

    if (stmt.body) {
        bool prev = pushScope();
        reachable_ = true;
        stmt.body->accept(*this);
        popScope();
        reachable_ = prev;  // restore outer reachability
    }

    // While body may not execute, so code after is reachable.
    reachable_ = true;
}

void SemanticAnalyzer::visitDoWhileStmt(const DoWhileStmt& stmt) {
    // Body always executes at least once in do-while.
    // Push a new scope for the body.
    bool prev = pushScope();
    reachable_ = true;  // body is always reachable
    if (stmt.body) {
        stmt.body->accept(*this);
    }
    popScope();
    reachable_ = prev;  // restore outer reachability

    // Visit condition (in outer scope)
    if (stmt.condition) {
        visitExpr(*stmt.condition);
    }

    // After do-while: code after loop is reachable.
    reachable_ = true;
}

void SemanticAnalyzer::visitForStmt(const ForStmt& stmt) {
    if (stmt.iterable) {
        visitExpr(*stmt.iterable);
    }

    if (stmt.body) {
        bool prev = pushScope();

        // Add the loop variable to the for-body scope.
        SymbolInfo forVar;
        forVar.name = stmt.variable;
        forVar.kind = SymbolKind::ForVar;
        forVar.declToken = stmt.forToken;  // approximate position
        addDecl(forVar);

        reachable_ = true;
        stmt.body->accept(*this);
        popScope();
        reachable_ = prev;
    }

    reachable_ = true;
}

void SemanticAnalyzer::visitCForStmt(const CForStmt& stmt) {
    bool prev = pushScope();

    if (stmt.initializer) {
        reachable_ = true;
        stmt.initializer->accept(*this);
    }
    if (stmt.condition) {
        visitExpr(*stmt.condition);
    }
    if (stmt.increment) {
        visitExpr(*stmt.increment);
    }

    if (stmt.body) {
        reachable_ = true;
        stmt.body->accept(*this);
    }

    popScope();
    reachable_ = prev;
    reachable_ = true;  // for loop may not execute
}

void SemanticAnalyzer::visitFuncStmt(const FuncStmt& stmt) {
    // Declare the function in the current scope FIRST
    // (so it's visible to code before its definition).
    SymbolInfo funcSym;
    funcSym.name = stmt.name;
    funcSym.kind = SymbolKind::Function;
    funcSym.declToken = stmt.nameToken;
    for (auto& p : stmt.params) {
        funcSym.paramNames.push_back(p.name);
    }
    funcSym.hasRestParam = stmt.params.empty() ? false : stmt.params.back().isRest;
    addDecl(funcSym);

    // Enter function body scope.
    pushScope();

    // Add parameters to the function scope.
    for (auto& p : stmt.params) {
        SymbolInfo param;
        param.name = p.name;
        param.kind = SymbolKind::Parameter;
        param.declToken = stmt.nameToken;  // approximate — params don't have their own tokens
        addDecl(param);
    }

    reachable_ = true;
    if (stmt.body) {
        visitStatements(stmt.body->statements);
    }

    popScope();
    reachable_ = true;  // function declaration doesn't affect reachability
}

void SemanticAnalyzer::visitObjStmt(const ObjStmt& stmt) {
    // Declare the object in the current scope.
    SymbolInfo objSym;
    objSym.name = stmt.name;
    objSym.kind = SymbolKind::Object;
    objSym.declToken = stmt.nameToken;
    objSym.parentNames = stmt.parentNames;
    for (auto& p : stmt.params) {
        objSym.paramNames.push_back(p.name);
    }
    objSym.hasRestParam = stmt.params.empty() ? false : stmt.params.back().isRest;
    addDecl(objSym);

    // Enter constructor body scope.
    pushScope();

    // Add constructor params.
    for (auto& p : stmt.params) {
        SymbolInfo param;
        param.name = p.name;
        param.kind = SymbolKind::Parameter;
        param.declToken = stmt.nameToken;
        addDecl(param);
    }

    // Process methods — each FuncStmt in the methods vector.
    for (auto& m : stmt.methods) {
        if (auto* methodFunc = dynamic_cast<const FuncStmt*>(m.get())) {
            // Record method name on the object symbol in the symbol table.
            const auto& allSyms = table_.allSymbols();
            for (size_t i = allSyms.size(); i > 0; i--) {
                auto& s = const_cast<SymbolInfo&>(allSyms[i - 1]);
                if (s.name == stmt.name && s.kind == SymbolKind::Object) {
                    s.methodNames.push_back(methodFunc->name);
                    break;
                }
            }

            SymbolInfo methodSym;
            methodSym.name = methodFunc->name;
            methodSym.kind = SymbolKind::Method;
            methodSym.declToken = methodFunc->nameToken;
            methodSym.isExported = true;  // methods are part of the object's public interface
            for (auto& p : methodFunc->params) {
                methodSym.paramNames.push_back(p.name);
            }
            methodSym.hasRestParam = methodFunc->params.empty() ? false : methodFunc->params.back().isRest;
            addDecl(methodSym);

            // Enter method body scope.
            pushScope();

            // Add method params.
            for (auto& p : methodFunc->params) {
                SymbolInfo mp;
                mp.name = p.name;
                mp.kind = SymbolKind::Parameter;
                mp.declToken = methodFunc->nameToken;
                addDecl(mp);
            }

            reachable_ = true;
            if (methodFunc->body) {
                visitStatements(methodFunc->body->statements);
            }

            popScope();
        }
    }

    // Visit constructor body.
    reachable_ = true;
    if (stmt.body) {
        visitStatements(stmt.body->statements);
    }

    popScope();
    reachable_ = true;
}

void SemanticAnalyzer::visitBreakStmt(const BreakStmt& /*stmt*/) {
    reachable_ = false;
}

void SemanticAnalyzer::visitContinueStmt(const ContinueStmt& /*stmt*/) {
    reachable_ = false;
}

void SemanticAnalyzer::visitTryStmt(const TryStmt& stmt) {
    // Try block
    if (stmt.tryBlock) {
        reachable_ = true;
        stmt.tryBlock->accept(*this);
    }

    // Catch block — introduces a new scope for the catch variable.
    if (stmt.catchBlock) {
        pushScope();
        if (!stmt.catchVar.empty()) {
            SymbolInfo cv;
            cv.name = stmt.catchVar;
            cv.kind = SymbolKind::CatchVar;
            cv.declToken = Token();  // no specific token for catch var
            addDecl(cv);
        }
        reachable_ = true;
        stmt.catchBlock->accept(*this);
        popScope();
    }

    // Finally block
    if (stmt.finallyBlock) {
        reachable_ = true;
        stmt.finallyBlock->accept(*this);
    }

    reachable_ = true;
}

void SemanticAnalyzer::visitThrowStmt(const ThrowStmt& stmt) {
    if (stmt.value) {
        visitExpr(*stmt.value);
    }
    reachable_ = false;
}

void SemanticAnalyzer::visitImportStmt(const ImportStmt& stmt) {
    if (!stmt.importNames.empty()) {
        // from "path" import a, b, c — each name is a separate binding.
        for (auto& name : stmt.importNames) {
            SymbolInfo sym;
            sym.name = name;
            sym.kind = SymbolKind::Import;
            sym.declToken = stmt.keyword;
            sym.importPath = stmt.modulePath;
            addDecl(sym);
        }
    } else {
        // import "path" or import "path" as alias
        std::string bindingName = stmt.alias.empty()
            ? stmt.modulePath   // derive name from path (simplified)
            : stmt.alias;
        SymbolInfo sym;
        sym.name = bindingName;
        sym.kind = SymbolKind::Import;
        sym.declToken = stmt.keyword;
        sym.importPath = stmt.modulePath;
        addDecl(sym);
    }
}

void SemanticAnalyzer::visitExportStmt(const ExportStmt& stmt) {
    if (stmt.declaration) {
        stmt.declaration->accept(*this);

        // Mark the most recently added symbol as exported.
        const auto& allSyms = table_.allSymbols();
        if (!allSyms.empty()) {
            // The declaration we just visited added symbols to the current scope.
            // Find them and mark as exported.
            const auto& currentScopeSymbols = table_.allScopes().back().symbolIndices;
            // Mark all symbols added by this export's declaration.
            // The simplest heuristic: mark the symbol with the matching name
            // from the wrapped declaration.
            // For destructured bindings, collect all bound names.
            std::vector<std::string> exportedNames;
            if (auto* func = dynamic_cast<const FuncStmt*>(stmt.declaration.get())) {
                exportedNames.push_back(func->name);
            } else if (auto* let = dynamic_cast<const LetStmt*>(stmt.declaration.get())) {
                if (let->binding) {
                    exportedNames = let->binding->getBoundNames();
                } else {
                    exportedNames.push_back(let->name);
                }
            } else if (auto* obj = dynamic_cast<const ObjStmt*>(stmt.declaration.get())) {
                exportedNames.push_back(obj->name);
            }

            for (const auto& exportedName : exportedNames) {
                if (!exportedName.empty()) {
                    for (int idx : currentScopeSymbols) {
                        auto& s = const_cast<SymbolInfo&>(allSyms[idx]);
                        if (s.name == exportedName && !s.isExported) {
                            s.isExported = true;
                            exportedSymbols_.push_back(&s);
                            break;
                        }
                    }
                }
            }
        }
    }
}

void SemanticAnalyzer::visitDeferStmt(const DeferStmt& stmt) {
    // Analyze the deferred expression
    if (stmt.expression) {
        visitExpr(*stmt.expression);
    }
}

void SemanticAnalyzer::visitErrorStmt(const ErrorStmt& /*stmt*/) {
    // ErrorStmt is a no-op placeholder — nothing to analyze.
}

// ═══════════════════════════════════════════════════════════════════════════
// SemanticAnalyzer — Expr visitors
// ═══════════════════════════════════════════════════════════════════════════

void SemanticAnalyzer::visitLiteralExpr(const LiteralExpr& /*expr*/) {
    // No symbols or references.
}

void SemanticAnalyzer::visitBinaryExpr(const BinaryExpr& expr) {
    if (expr.left) visitExpr(*expr.left);
    if (expr.right) visitExpr(*expr.right);
}

void SemanticAnalyzer::visitGroupingExpr(const GroupingExpr& expr) {
    if (expr.expression) visitExpr(*expr.expression);
}

void SemanticAnalyzer::visitUnaryExpr(const UnaryExpr& expr) {
    if (expr.right) visitExpr(*expr.right);
}

void SemanticAnalyzer::visitVariableExpr(const VariableExpr& expr) {
    addReference(expr.name, expr.nameToken, false);
    markReferenced(expr.name);
}

void SemanticAnalyzer::visitAssignmentExpr(const AssignmentExpr& expr) {
    addReference(expr.name, expr.nameToken, true);
    markReferenced(expr.name);
    if (expr.value) visitExpr(*expr.value);
}

void SemanticAnalyzer::visitCompoundAssignmentExpr(const CompoundAssignmentExpr& expr) {
    // Track the target as both a read and write.
    if (auto* var = dynamic_cast<const VariableExpr*>(expr.target.get())) {
        addReference(var->name, var->nameToken, true);
        markReferenced(var->name);
    } else if (auto* prop = dynamic_cast<const PropertyExpr*>(expr.target.get())) {
        // obj.prop += ... — visit the object part.
        if (prop->object) visitExpr(*prop->object);
    } else if (auto* idx = dynamic_cast<const IndexExpr*>(expr.target.get())) {
        if (idx->array) visitExpr(*idx->array);
        if (idx->index) visitExpr(*idx->index);
    } else {
        // Fallback: visit target generically.
        if (expr.target) visitExpr(*expr.target);
    }
    if (expr.value) visitExpr(*expr.value);
}

void SemanticAnalyzer::visitCallExpr(const CallExpr& expr) {
    if (expr.callee) visitExpr(*expr.callee);
    for (auto& arg : expr.arguments) {
        if (arg) visitExpr(*arg);
    }
}

void SemanticAnalyzer::visitArrayExpr(const ArrayExpr& expr) {
    for (auto& elem : expr.elements) {
        if (elem) visitExpr(*elem);
    }
}

void SemanticAnalyzer::visitDictExpr(const DictExpr& expr) {
    for (auto& [key, value] : expr.pairs) {
        if (key) visitExpr(*key);
        if (value) visitExpr(*value);
    }
}

void SemanticAnalyzer::visitIndexExpr(const IndexExpr& expr) {
    if (expr.array) visitExpr(*expr.array);
    if (expr.index) visitExpr(*expr.index);
}

void SemanticAnalyzer::visitPropertyExpr(const PropertyExpr& expr) {
    if (expr.object) visitExpr(*expr.object);
    // The property name itself is not a variable reference — it's a field access.
}

void SemanticAnalyzer::visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) {
    if (expr.object) visitExpr(*expr.object);
    if (expr.value) visitExpr(*expr.value);
}

void SemanticAnalyzer::visitIndexAssignmentExpr(const IndexAssignmentExpr& expr) {
    if (expr.object) visitExpr(*expr.object);
    if (expr.index) visitExpr(*expr.index);
    if (expr.value) visitExpr(*expr.value);
}

void SemanticAnalyzer::visitThisExpr(const ThisExpr& /*expr*/) {
    // 'this' is a keyword, not a declared symbol.
}

void SemanticAnalyzer::visitSuperExpr(const SuperExpr& /*expr*/) {
    // 'super' is a keyword, not a declared symbol.
}

void SemanticAnalyzer::visitIncDecExpr(const IncDecExpr& expr) {
    // Track the target as both read and write.
    if (auto* var = dynamic_cast<const VariableExpr*>(expr.target.get())) {
        addReference(var->name, var->nameToken, true);
        markReferenced(var->name);
    } else if (auto* prop = dynamic_cast<const PropertyExpr*>(expr.target.get())) {
        if (prop->object) visitExpr(*prop->object);
    } else if (expr.target) {
        visitExpr(*expr.target);
    }
}

void SemanticAnalyzer::visitTernaryExpr(const TernaryExpr& expr) {
    if (expr.condition) visitExpr(*expr.condition);
    if (expr.thenBranch) visitExpr(*expr.thenBranch);
    if (expr.elseBranch) visitExpr(*expr.elseBranch);
}

void SemanticAnalyzer::visitMatchExpr(const MatchExpr& expr) {
    if (expr.scrutinee) visitExpr(*expr.scrutinee);
    for (const auto& c : expr.cases) {
        if (c.body) visitExpr(*c.body);
        if (c.blockBody) c.blockBody->accept(*this);
    }
}

void SemanticAnalyzer::visitFuncExpr(const FuncExpr& expr) {
    // Anonymous function (lambda) — create a scope for its body.
    pushScope();

    // Add parameters.
    for (auto& p : expr.params) {
        SymbolInfo param;
        param.name = p.name;
        param.kind = SymbolKind::Parameter;
        // No specific token for lambda params.
        addDecl(param);
    }

    reachable_ = true;
    if (expr.body) {
        visitStatements(expr.body->statements);
    }

    popScope();
}

void SemanticAnalyzer::visitYieldExpr(const YieldExpr& expr) {
    if (expr.value) {
        visitExpr(*expr.value);
    }
    // yield doesn't terminate reachability (generator can resume).
}

void SemanticAnalyzer::visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) {
    // Register write references for each bound name.
    auto names = expr.binding->getBoundNames();
    for (const auto& name : names) {
        addReference(name, expr.binding->startToken, /*isWrite=*/true);
        markReferenced(name);
    }
    if (expr.value) {
        visitExpr(*expr.value);
    }
}

void SemanticAnalyzer::visitSpreadExpr(const SpreadExpr& expr) {
    if (expr.expr) visitExpr(*expr.expr);
}

void SemanticAnalyzer::visitListCompExpr(const ListCompExpr& expr) {
    // Push a scope for the loop variable
    bool prev = pushScope();

    // Register the loop variable
    SymbolInfo forVar;
    forVar.name = expr.variable;
    forVar.kind = SymbolKind::ForVar;
    forVar.declToken = expr.leftBracket;
    addDecl(forVar);

    // Visit inner expressions
    if (expr.iterable) visitExpr(*expr.iterable);
    if (expr.condition) visitExpr(*expr.condition);
    if (expr.resultExpr) visitExpr(*expr.resultExpr);

    popScope();
}

void SemanticAnalyzer::visitDictCompExpr(const DictCompExpr& expr) {
    bool prev = pushScope();

    SymbolInfo forVar;
    forVar.name = expr.variable;
    forVar.kind = SymbolKind::ForVar;
    forVar.declToken = expr.leftBrace;
    addDecl(forVar);

    if (expr.iterable) visitExpr(*expr.iterable);
    if (expr.condition) visitExpr(*expr.condition);
    if (expr.keyExpr) visitExpr(*expr.keyExpr);
    if (expr.valueExpr) visitExpr(*expr.valueExpr);

    popScope();
}

void SemanticAnalyzer::visitOptionalChainExpr(const OptionalChainExpr& expr) {
    if (expr.object) visitExpr(*expr.object);
    if (expr.kind == OptionalChainExpr::Kind::INDEX && expr.index) {
        visitExpr(*expr.index);
    }
    for (const auto& arg : expr.arguments) {
        if (arg) visitExpr(*arg);
    }
}

void SemanticAnalyzer::visitErrorExpr(const ErrorExpr& /*expr*/) {
    // ErrorExpr is a no-op placeholder.
}

// ═══════════════════════════════════════════════════════════════════════════
// SemanticAnalyzer — query API
// ═══════════════════════════════════════════════════════════════════════════

const SymbolInfo* SemanticAnalyzer::findDeclarationAt(int line, int col) const {
    // Strategy:
    // 1. Check if the position is on a reference → resolve to declaration.
    // 2. Check if the position is on a declaration token → return that symbol.
    // 3. Return nullptr if neither.

    // Step 1: Check references first (most common for go-to-def).
    for (auto& ref : references_) {
        if (tokenContains(ref.token, line, col)) {
            return table_.resolve(ref.name);
        }
    }

    // Step 2: Check declaration tokens.
    const auto& allSyms = table_.allSymbols();
    for (auto& sym : allSyms) {
        if (tokenContains(sym.declToken, line, col)) {
            return &sym;
        }
    }

    return nullptr;
}

std::vector<SymbolRef> SemanticAnalyzer::findReferencesTo(int line, int col) const {
    // Step 1: Find which symbol this position refers to.
    const SymbolInfo* targetSym = findDeclarationAt(line, col);
    if (!targetSym) {
        // Maybe the position is directly on a declaration.
        const auto& allSyms = table_.allSymbols();
        for (auto& sym : allSyms) {
            if (tokenContains(sym.declToken, line, col)) {
                targetSym = &sym;
                break;
            }
        }
    }
    if (!targetSym) return {};

    // Step 2: Collect all references that resolve to the same declaration.
    std::vector<SymbolRef> result;
    for (auto& ref : references_) {
        if (ref.name == targetSym->name) {
            // Verify this reference resolves to the same symbol.
            const auto* resolved = table_.resolve(ref.name);
            if (resolved == targetSym) {
                result.push_back(ref);
            }
        }
    }

    // Also include the declaration token itself as a "reference"
    // (LSP clients expect to see the declaration in the references list).
    SymbolRef declRef;
    declRef.name = targetSym->name;
    declRef.token = targetSym->declToken;
    declRef.isWrite = false;
    result.push_back(std::move(declRef));

    // Sort by position.
    std::sort(result.begin(), result.end(),
        [](const SymbolRef& a, const SymbolRef& b) {
            if (a.token.line != b.token.line)
                return a.token.line < b.token.line;
            return a.token.column < b.token.column;
        });

    return result;
}

std::vector<const SymbolInfo*> SemanticAnalyzer::getVisibleSymbols(int line, int col) const {
    // Return symbols visible at the cursor position, respecting scope boundaries.
    //
    // Strategy: determine which scope chain the cursor belongs to by finding
    // the innermost scope whose startLine ≤ cursor line. Then include only
    // symbols whose scope is in that chain (or global).
    //
    // This correctly excludes symbols from closed scopes (e.g., variables
    // declared inside a different function body that has already ended).

    const auto& allSyms = table_.allSymbols();
    // Use scopeHistory_ (never popped) instead of scopes_ (popped during analysis).
    const auto& allScopes = table_.allScopesHistory();
    std::vector<const SymbolInfo*> result;
    result.reserve(allSyms.size());

    // ── Determine the cursor's scope chain ────────────────────────────────
    int cursorScopeIdx = 0;  // default to global (scope 0)
    for (size_t si = 0; si < allScopes.size(); si++) {
        const auto& scope = allScopes[si];
        if (scope.startLine > 0 && scope.startLine <= line) {
            // This scope starts at or before the cursor — it's a candidate.
            // Keep the deepest (innermost) one.
            cursorScopeIdx = static_cast<int>(si);
        }
    }

    // Walk up from cursorScopeIdx to global, collecting visible scope indices.
    std::vector<bool> visibleScopeChain(allScopes.size(), false);
    int idx = cursorScopeIdx;
    while (idx >= 0 && idx < static_cast<int>(allScopes.size())) {
        visibleScopeChain[idx] = true;
        idx = allScopes[idx].parentIndex;
    }

    // ── Filter symbols ────────────────────────────────────────────────────
    for (auto& sym : allSyms) {
        // No position info (e.g., catch var, lambda param) — include.
        if (sym.declToken.line == 0) {
            result.push_back(&sym);
            continue;
        }

        // Declared after cursor — exclude.
        if (sym.declToken.line > line) continue;
        if (sym.declToken.line == line && sym.declToken.column > col) continue;

        // Global scope (level 0) — always visible.
        if (sym.scopeLevel == 0) {
            result.push_back(&sym);
            continue;
        }

        // Non-global: only visible if in the cursor's scope chain.
        if (sym.scopeIndex >= 0 &&
            sym.scopeIndex < static_cast<int>(visibleScopeChain.size()) &&
            visibleScopeChain[sym.scopeIndex]) {
            result.push_back(&sym);
        }
    }

    return result;
}

const SymbolInfo* SemanticAnalyzer::findSymbolAt(int line, int col) const {
    // Step 1: Check if the position is on a reference — resolve to decl.
    for (auto& ref : references_) {
        if (tokenContains(ref.token, line, col)) {
            return table_.resolve(ref.name);
        }
    }

    // Step 2: Check if the position is on a declaration token.
    const auto& allSyms = table_.allSymbols();
    for (auto& sym : allSyms) {
        if (tokenContains(sym.declToken, line, col)) {
            return &sym;
        }
    }

    return nullptr;
}

}  // namespace vora
