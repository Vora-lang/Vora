#include "formatter.h"

#include "../lexer/token.h"
#include "../runtime/value.h"

#include <sstream>

namespace vora {

// =========================================================================
// Precedence constants (mirrors parser's getPrecedence table)
// =========================================================================

// PREC_NONE = 0 (not used as an operator precedence — indicates no context)
enum {
    PREC_ASSIGNMENT  = 1,   // =, +=, -=, etc., OR
    PREC_AND_TERNARY = 2,   // &&, ?:
    PREC_EQUALITY    = 3,   // ==, !=
    PREC_COMPARISON  = 4,   // <, <=, >, >=
    PREC_TERM        = 5,   // +, -
    PREC_FACTOR      = 6,   // *, /, %
    PREC_POWER       = 7,   // **
    PREC_UNARY       = 8,   // !, -, ++, --
    PREC_CALL        = 9,   // function call, index, property
    PREC_PRIMARY     = 100  // literal, variable, grouping, array, dict
};

// =========================================================================
// Entry points
// =========================================================================

std::string SourceFormatter::format(const Program* program) {
    return program->accept(*this);
}

// =========================================================================
// Indentation helpers
// =========================================================================

std::string SourceFormatter::indentStr() const {
    // 4 spaces per indent level
    return std::string(static_cast<size_t>(indent_) * 4, ' ');
}

std::string SourceFormatter::nl() const {
    return "\n" + indentStr();
}

// =========================================================================
// Precedence helpers
// =========================================================================

int SourceFormatter::tokenPrecedence(TokenType type) {
    switch (type) {
        case TokenType::EQUAL:
        case TokenType::PLUS_EQUAL:
        case TokenType::MINUS_EQUAL:
        case TokenType::MULTIPLY_EQUAL:
        case TokenType::DIVIDE_EQUAL:
        case TokenType::MODULO_EQUAL:
            return PREC_ASSIGNMENT;

        case TokenType::OR:
            return PREC_ASSIGNMENT;

        case TokenType::AND:
            return PREC_AND_TERNARY;

        case TokenType::QUESTION:
            return PREC_AND_TERNARY;

        case TokenType::EQUAL_EQUAL:
        case TokenType::NOT_EQUAL:
            return PREC_EQUALITY;

        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
            return PREC_COMPARISON;

        case TokenType::PLUS:
        case TokenType::MINUS:
            return PREC_TERM;

        case TokenType::MULTIPLY:
        case TokenType::DIVIDE:
        case TokenType::MODULO:
            return PREC_FACTOR;

        case TokenType::POWER:
            return PREC_POWER;

        default:
            return PREC_PRIMARY;
    }
}

bool SourceFormatter::isRightAssoc(TokenType type) {
    switch (type) {
        case TokenType::POWER:
        case TokenType::EQUAL:
        case TokenType::PLUS_EQUAL:
        case TokenType::MINUS_EQUAL:
        case TokenType::MULTIPLY_EQUAL:
        case TokenType::DIVIDE_EQUAL:
        case TokenType::MODULO_EQUAL:
            return true;
        default:
            return false;
    }
}

int SourceFormatter::exprPrecedence(const Expr& expr) {
    // BinaryExpr → operator precedence
    if (auto* b = dynamic_cast<const BinaryExpr*>(&expr)) {
        return tokenPrecedence(b->op.type);
    }

    // Assignment-like expressions
    if (dynamic_cast<const AssignmentExpr*>(&expr) ||
        dynamic_cast<const CompoundAssignmentExpr*>(&expr) ||
        dynamic_cast<const PropertyAssignmentExpr*>(&expr) ||
        dynamic_cast<const IndexAssignmentExpr*>(&expr)) {
        return PREC_ASSIGNMENT;
    }

    // Ternary
    if (dynamic_cast<const TernaryExpr*>(&expr)) {
        return PREC_AND_TERNARY;
    }

    // Unary operators bind tightly
    if (dynamic_cast<const UnaryExpr*>(&expr) ||
        dynamic_cast<const IncDecExpr*>(&expr)) {
        return PREC_UNARY;
    }

    // Call, index, property — highest precedence before atoms
    if (dynamic_cast<const CallExpr*>(&expr) ||
        dynamic_cast<const IndexExpr*>(&expr) ||
        dynamic_cast<const PropertyExpr*>(&expr)) {
        return PREC_CALL;
    }

    // Atoms: literal, variable, this, super, array, dict, grouping
    return PREC_PRIMARY;
}

// =========================================================================
// formatExpr — precedence-aware expression formatting
// =========================================================================

std::string SourceFormatter::formatExpr(const Expr& expr, int minPrec) {
    int ownPrec = exprPrecedence(expr);

    if (ownPrec < minPrec) {
        return "(" + expr.accept(*this) + ")";
    }
    return expr.accept(*this);
}

// =========================================================================
// Statement formatting helpers
// =========================================================================

std::string SourceFormatter::formatBlockBody(const Stmt& stmt) {
    // If the statement is already a BlockStmt, format it with a leading space
    // for "if (...) {" / "while (...) {" / etc.
    if (auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
        return " " + visitBlockStmt(*block);
    }

    // Otherwise wrap a single statement in braces.
    std::stringstream ss;
    ss << " {";
    incIndent();
    ss << nl();
    ss << stmt.accept(*this);
    decIndent();
    ss << nl() << "}";
    return ss.str();
}

std::string SourceFormatter::formatStatements(
    const std::vector<std::unique_ptr<Stmt>>& stmts
) {
    std::stringstream ss;
    for (size_t i = 0; i < stmts.size(); ++i) {
        if (i > 0) {
            ss << nl();
        }
        ss << stmts[i]->accept(*this);
    }
    return ss.str();
}

std::string SourceFormatter::formatParams(const std::vector<ParamDecl>& params) {
    std::stringstream ss;
    ss << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        ss << params[i].name;
        if (params[i].defaultValue) {
            ss << " = " << formatExpr(*params[i].defaultValue, 0);
        }
    }
    ss << ")";
    return ss.str();
}

// =====================================================================
// ExprVisitor<std::string> — expression formatting
// =====================================================================

std::string SourceFormatter::visitLiteralExpr(const LiteralExpr& expr) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, GcPtr<GcString>>) {
            // Output as a quoted string literal. The value stored is the
            // decoded string content (no surrounding quotes), so we add them.
            // Simple escaping: backslash + double-quote.
            std::string out = "\"";
            for (char c : arg->value) {
                if (c == '"') {
                    out += "\\\"";
                } else if (c == '\\') {
                    out += "\\\\";
                } else if (c == '\n') {
                    out += "\\n";
                } else if (c == '\r') {
                    out += "\\r";
                } else if (c == '\t') {
                    out += "\\t";
                } else {
                    out += c;
                }
            }
            out += "\"";
            return out;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            // Format doubles compactly — avoid trailing zeros.
            std::stringstream ss;
            ss << arg;
            std::string s = ss.str();
            // If it has a decimal point and no exponent, strip trailing zeros
            if (s.find('.') != std::string::npos &&
                s.find('e') == std::string::npos &&
                s.find('E') == std::string::npos) {
                while (s.back() == '0') s.pop_back();
                if (s.back() == '.') s.pop_back();
            }
            return s;
        } else {
            // All other Value types (GcPtr<Array>, GcPtr<Dict>, etc.)
            // should not appear as literal expressions at format time.
            return "<value>";
        }
    }, expr.value);
}

std::string SourceFormatter::visitBinaryExpr(const BinaryExpr& expr) {
    int opPrec = tokenPrecedence(expr.op.type);
    bool rightAssoc = isRightAssoc(expr.op.type);

    // Left operand: if right-associative, left needs strictly higher precedence
    // (minPrec = opPrec + 1); otherwise left needs >= opPrec (minPrec = opPrec).
    int leftMinPrec = rightAssoc ? (opPrec + 1) : opPrec;

    // Right operand: if left-associative, right needs strictly higher precedence
    // (minPrec = opPrec + 1); otherwise right needs >= opPrec (minPrec = opPrec).
    int rightMinPrec = rightAssoc ? opPrec : (opPrec + 1);

    std::stringstream ss;
    ss << formatExpr(*expr.left, leftMinPrec);
    ss << " " << expr.op.lexeme << " ";
    ss << formatExpr(*expr.right, rightMinPrec);
    return ss.str();
}

std::string SourceFormatter::visitGroupingExpr(const GroupingExpr& expr) {
    // Always output parentheses for a grouping expression.
    return "(" + formatExpr(*expr.expression, 0) + ")";
}

std::string SourceFormatter::visitUnaryExpr(const UnaryExpr& expr) {
    std::stringstream ss;
    ss << expr.op.lexeme;
    // No space after ! or - unary operator
    ss << formatExpr(*expr.right, PREC_UNARY);
    return ss.str();
}

std::string SourceFormatter::visitVariableExpr(const VariableExpr& expr) {
    return expr.name;
}

std::string SourceFormatter::visitAssignmentExpr(const AssignmentExpr& expr) {
    std::stringstream ss;
    ss << expr.name << " = ";
    // Assignment is right-associative at PREC_ASSIGNMENT.
    // Right side needs minPrec = PREC_ASSIGNMENT (same prec is ok for right side
    // of right-assoc op).
    ss << formatExpr(*expr.value, PREC_ASSIGNMENT);
    return ss.str();
}

std::string SourceFormatter::visitCompoundAssignmentExpr(
    const CompoundAssignmentExpr& expr
) {
    std::stringstream ss;
    // Target has call-level precedence (no parens needed for x, obj.prop, arr[i])
    ss << formatExpr(*expr.target, PREC_CALL);
    ss << " " << expr.op.lexeme << " ";
    // RHS: same precedence rule as assignment
    ss << formatExpr(*expr.value, PREC_ASSIGNMENT);
    return ss.str();
}

std::string SourceFormatter::visitCallExpr(const CallExpr& expr) {
    std::stringstream ss;
    ss << formatExpr(*expr.callee, PREC_CALL);
    ss << "(";
    for (size_t i = 0; i < expr.arguments.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        ss << formatExpr(*expr.arguments[i], 0);
    }
    ss << ")";
    return ss.str();
}

std::string SourceFormatter::visitArrayExpr(const ArrayExpr& expr) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < expr.elements.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        ss << formatExpr(*expr.elements[i], 0);
    }
    ss << "]";
    return ss.str();
}

std::string SourceFormatter::visitDictExpr(const DictExpr& expr) {
    std::stringstream ss;
    ss << "{";
    for (size_t i = 0; i < expr.pairs.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        // Dict keys are identifiers (VariableExpr) or string literals —
        // both represent plain string keys, so output without quotes.
        const Expr* key = expr.pairs[i].first.get();
        if (auto* lit = dynamic_cast<const LiteralExpr*>(key)) {
            if (std::holds_alternative<GcPtr<GcString>>(lit->value)) {
                ss << std::get<GcPtr<GcString>>(lit->value)->value;
            } else {
                ss << formatExpr(*key, 0);
            }
        } else {
            ss << formatExpr(*key, 0);
        }
        ss << ": ";
        ss << formatExpr(*expr.pairs[i].second, 0);
    }
    ss << "}";
    return ss.str();
}

std::string SourceFormatter::visitIndexExpr(const IndexExpr& expr) {
    std::stringstream ss;
    ss << formatExpr(*expr.array, PREC_CALL);
    ss << "[";
    ss << formatExpr(*expr.index, 0);
    ss << "]";
    return ss.str();
}

std::string SourceFormatter::visitPropertyExpr(const PropertyExpr& expr) {
    std::stringstream ss;
    ss << formatExpr(*expr.object, PREC_CALL);
    ss << "." << expr.property;
    return ss.str();
}

std::string SourceFormatter::visitPropertyAssignmentExpr(
    const PropertyAssignmentExpr& expr
) {
    std::stringstream ss;
    ss << formatExpr(*expr.object, PREC_CALL);
    ss << "." << expr.property << " = ";
    ss << formatExpr(*expr.value, PREC_ASSIGNMENT);
    return ss.str();
}

std::string SourceFormatter::visitIndexAssignmentExpr(
    const IndexAssignmentExpr& expr
) {
    std::stringstream ss;
    ss << formatExpr(*expr.object, PREC_CALL);
    ss << "[";
    ss << formatExpr(*expr.index, 0);
    ss << "] = ";
    ss << formatExpr(*expr.value, PREC_ASSIGNMENT);
    return ss.str();
}

std::string SourceFormatter::visitThisExpr(const ThisExpr& /*expr*/) {
    return "this";
}

std::string SourceFormatter::visitSuperExpr(const SuperExpr& /*expr*/) {
    return "super";
}

std::string SourceFormatter::visitIncDecExpr(const IncDecExpr& expr) {
    std::stringstream ss;

    if (expr.isPrefix) {
        ss << expr.op.lexeme;
        ss << formatExpr(*expr.target, PREC_UNARY);
    } else {
        ss << formatExpr(*expr.target, PREC_UNARY);
        ss << expr.op.lexeme;
    }
    return ss.str();
}

std::string SourceFormatter::visitTernaryExpr(const TernaryExpr& expr) {
    std::stringstream ss;

    // Condition: must have precedence strictly greater than ternary (PREC_AND_TERNARY).
    // Since ternary is right-associative, its left side (condition) requires
    // minPrec = PREC_AND_TERNARY + 1 to avoid ambiguity with `&&`.
    ss << formatExpr(*expr.condition, PREC_AND_TERNARY + 1);

    ss << " ? ";

    // Then-branch: the `?` acts as a separator, so any expression is safe here.
    ss << formatExpr(*expr.thenBranch, 0);

    ss << " : ";

    // Else-branch: ternary is right-associative, so the else side accepts
    // same-precedence ternaries (they bind to the right).
    ss << formatExpr(*expr.elseBranch, PREC_AND_TERNARY);

    return ss.str();
}

std::string SourceFormatter::visitFuncExpr(const FuncExpr& expr) {
    std::stringstream ss;
    ss << "func";
    ss << formatParams(expr.params);
    ss << " ";

    // Format body as a block
    incIndent();
    ss << "{" << nl();
    const auto& stmts = expr.body->statements;
    for (size_t i = 0; i < stmts.size(); i++) {
        ss << stmts[i]->accept(*this);
        if (i + 1 < stmts.size()) {
            ss << nl();
        }
    }
    decIndent();
    ss << nl() << "}";

    return ss.str();
}

std::string SourceFormatter::visitYieldExpr(const YieldExpr& expr) {
    if (expr.value) {
        return "yield " + formatExpr(*expr.value, 0);
    }
    return "yield";
}

// =====================================================================
// StmtVisitor<std::string> — statement formatting
// =====================================================================

std::string SourceFormatter::visitExprStmt(const ExprStmt& stmt) {
    return formatExpr(*stmt.expression, 0);
}

std::string SourceFormatter::visitLetStmt(const LetStmt& stmt) {
    std::stringstream ss;
    ss << (stmt.isConst ? "const " : "let ") << stmt.name;

    if (!stmt.typeAnnotation.empty()) {
        ss << ": " << stmt.typeAnnotation;
    }

    if (stmt.initializer) {
        ss << " = " << formatExpr(*stmt.initializer, PREC_ASSIGNMENT);
    }

    return ss.str();
}

std::string SourceFormatter::visitBlockStmt(const BlockStmt& stmt) {
    std::stringstream ss;

    ss << "{";
    incIndent();

    if (!stmt.statements.empty()) {
        ss << nl();
        ss << formatStatements(stmt.statements);
    }

    decIndent();
    ss << nl() << "}";

    return ss.str();
}

std::string SourceFormatter::visitReturnStmt(const ReturnStmt& stmt) {
    std::stringstream ss;
    ss << "return";
    if (stmt.value) {
        ss << " " << formatExpr(*stmt.value, 0);
    }
    return ss.str();
}

std::string SourceFormatter::visitIfStmt(const IfStmt& stmt) {
    std::stringstream ss;

    // if (condition) { ... }
    ss << "if (";
    ss << formatExpr(*stmt.condition, 0);
    ss << ")";
    ss << formatBlockBody(*stmt.thenBranch);

    if (stmt.elseBranch) {
        // Check if the else-branch is another IfStmt → "else if" chain
        if (auto* elseIf = dynamic_cast<const IfStmt*>(stmt.elseBranch.get())) {
            ss << " else ";
            ss << visitIfStmt(*elseIf);
        } else {
            ss << " else";
            ss << formatBlockBody(*stmt.elseBranch);
        }
    }

    return ss.str();
}

std::string SourceFormatter::visitWhileStmt(const WhileStmt& stmt) {
    std::stringstream ss;
    ss << "while (";
    ss << formatExpr(*stmt.condition, 0);
    ss << ")";
    ss << formatBlockBody(*stmt.body);
    return ss.str();
}

std::string SourceFormatter::visitForStmt(const ForStmt& stmt) {
    // Vora syntax: for var in expr { ... }  (no parentheses)
    std::stringstream ss;
    ss << "for " << stmt.variable << " in ";
    ss << formatExpr(*stmt.iterable, 0);
    ss << formatBlockBody(*stmt.body);
    return ss.str();
}

std::string SourceFormatter::visitCForStmt(const CForStmt& stmt) {
    // Vora syntax: for (init; cond; incr) { ... }
    std::stringstream ss;
    ss << "for (";

    // initializer
    if (stmt.initializer) {
        // LetStmt: strip trailing newline/indent from the formatted output
        // ExprStmt: similar
        std::string initStr;
        if (auto* letStmt = dynamic_cast<const LetStmt*>(stmt.initializer.get())) {
            initStr = visitLetStmt(*letStmt);
        } else if (auto* exprStmt = dynamic_cast<const ExprStmt*>(stmt.initializer.get())) {
            initStr = formatExpr(*exprStmt->expression, 0);
        } else {
            initStr = stmt.initializer->accept(*this);
        }
        // Remove trailing semicolons/newlines from the init string
        while (!initStr.empty() && (initStr.back() == ';' || initStr.back() == '\n' || initStr.back() == ' ')) {
            initStr.pop_back();
        }
        ss << initStr;
    }
    ss << "; ";

    // condition
    if (stmt.condition) {
        ss << formatExpr(*stmt.condition, 0);
    }
    ss << "; ";

    // increment
    if (stmt.increment) {
        ss << formatExpr(*stmt.increment, 0);
    }

    ss << ")";
    ss << formatBlockBody(*stmt.body);
    return ss.str();
}

std::string SourceFormatter::visitFuncStmt(const FuncStmt& stmt) {
    std::stringstream ss;
    ss << "func " << stmt.name;
    ss << formatParams(stmt.params);
    ss << " ";
    ss << visitBlockStmt(*stmt.body);
    return ss.str();
}

std::string SourceFormatter::visitObjStmt(const ObjStmt& stmt) {
    std::stringstream ss;
    ss << "Obj " << stmt.name;

    // Parent class names (syntax: Obj Child : Parent1, Parent2 (params) { ... })
    if (!stmt.parentNames.empty()) {
        ss << " : ";
        for (size_t i = 0; i < stmt.parentNames.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << stmt.parentNames[i];
        }
    }

    // Constructor parameters
    ss << formatParams(stmt.params);

    ss << " {";
    incIndent();

    // Constructor body
    if (stmt.body && !stmt.body->statements.empty()) {
        ss << nl();
        ss << formatStatements(stmt.body->statements);
    }

    // Methods — one newline before each, no blank line separation
    for (size_t i = 0; i < stmt.methods.size(); ++i) {
        ss << nl();
        ss << stmt.methods[i]->accept(*this);
    }

    decIndent();
    ss << nl() << "}";

    return ss.str();
}

std::string SourceFormatter::visitBreakStmt(const BreakStmt& /*stmt*/) {
    return "break";
}

std::string SourceFormatter::visitContinueStmt(const ContinueStmt& /*stmt*/) {
    return "continue";
}

std::string SourceFormatter::visitTryStmt(const TryStmt& stmt) {
    std::stringstream ss;

    // try block
    ss << "try";
    ss << formatBlockBody(*stmt.tryBlock);

    // catch block
    if (stmt.catchBlock) {
        ss << " catch (";
        ss << stmt.catchVar;
        ss << ")";
        ss << formatBlockBody(*stmt.catchBlock);
    }

    // finally block
    if (stmt.finallyBlock) {
        ss << " finally";
        ss << formatBlockBody(*stmt.finallyBlock);
    }

    return ss.str();
}

std::string SourceFormatter::visitThrowStmt(const ThrowStmt& stmt) {
    std::stringstream ss;
    ss << "throw";
    if (stmt.value) {
        ss << " " << formatExpr(*stmt.value, 0);
    }
    return ss.str();
}

std::string SourceFormatter::visitImportStmt(const ImportStmt& stmt) {
    if (!stmt.importNames.empty()) {
        std::string result = "from \"" + stmt.modulePath + "\" import";
        for (size_t i = 0; i < stmt.importNames.size(); i++) {
            if (i > 0) result += ",";
            result += " " + stmt.importNames[i];
        }
        return result;
    }
    std::string result = "import \"" + stmt.modulePath + "\"";
    if (!stmt.alias.empty()) result += " as " + stmt.alias;
    return result;
}

std::string SourceFormatter::visitExportStmt(const ExportStmt& stmt) {
    // Visit the inner declaration and prefix with 'export '
    return "export " + stmt.declaration->accept(*this);
}

// =====================================================================
// ProgramVisitor<std::string>
// =====================================================================

std::string SourceFormatter::visitProgram(const Program& program) {
    std::stringstream ss;
    ss << formatStatements(program.statements);
    ss << "\n";
    return ss.str();
}

}  // namespace vora
