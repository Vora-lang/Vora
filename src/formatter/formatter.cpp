#include "formatter.h"

#include "../lexer/token.h"
#include "../lexer/lexer.h"
#include "../runtime/value.h"

#include <charconv>
#include <cmath>
#include <sstream>
#include <string>
#include <system_error>

namespace vora {

// Defined below, next to the other literal rendering.
static std::string formatFloatLiteral(double d);

// =========================================================================
// Statement separation (ASI safety)
//
// Vora terminates statements by newline (ASI) when the next token cannot
// continue the expression.  The formatter therefore drops redundant semicolons,
// but that is only safe while the following statement cannot be absorbed:
//
//     let b1 = 0
//     [a1, b1] = [10, 20]      // parsed as 0[a1, b1] = ... without a ';'
//
// so a separator is emitted exactly where ASI would not supply one.
// =========================================================================

/// @brief First character of the first line of code, skipping comments.
static char firstCodeChar(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' ||
                                   text[i] == '\n' || text[i] == '\r')) {
            i++;
        }
        if (i + 1 < text.size() && text[i] == '/' &&
            (text[i + 1] == '/' || text[i + 1] == '*')) {
            if (text[i + 1] == '/') {
                while (i < text.size() && text[i] != '\n') i++;
            } else {
                const size_t end = text.find("*/", i + 2);
                i = (end == std::string::npos) ? text.size() : end + 2;
            }
            continue;
        }
        break;
    }
    return i < text.size() ? text[i] : '\0';
}

/// @brief True when @p next must be preceded by a ';' to keep it separate.
///
/// Any statement that begins with a token able to continue the previous
/// expression would otherwise be absorbed into it by ASI.
static bool needsStatementSeparator(const std::string& next) {
    switch (firstCodeChar(next)) {
        case '[':   // indexing / destructuring assignment
        case '(':   // call on the previous value
        case '.':   // property access
        case '+':   // binary plus, or prefix ++
        case '-':   // binary minus, or prefix --
        case '*':
        case '/':
        case '%':
        case '<':
        case '>':
        case '&':
        case '|':
        case '^':
        case '!':
        case '~':
        case '?':
            return true;
        default:
            return false;
    }
}
// =========================================================================
// Source literals
//
// valueToString() renders a value for *display*: a string comes out as its bare
// content, with no quotes.  Emitting that into source is wrong.  A dict key or a
// match pattern that was a string literal came back unquoted, so
// `"never-matches-this" => 1` became `never-matches-this => 1` (an arithmetic
// expression) and `{"a b": 1}` became `{a b: 1}`.  These helpers render a value
// the way it has to appear in *source*.
// =========================================================================

/// @brief Render a string as a quoted, escaped source literal.
static std::string quoteString(const std::string& text) {
    std::string out(1, '"');
    size_t i = 0;
    while (i < text.size()) {
        // An interpolation region is *code*, not string content: copy it through
        // verbatim, tracking brace depth and skipping nested string literals so an
        // inner quote or brace cannot end it early.  This mirrors the lexer, and it
        // is why regions must not be escaped: escaping the inner quotes of
        // `"${"inner ${x}"}"` produced text the lexer could not read back, which is
        // what made the formatter emit an unparseable file for interpolated strings.
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{') {
            size_t j = i + 2;
            int depth = 1;
            while (j < text.size() && depth > 0) {
                const char c = text[j];
                if (c == '"' || c == '\'') {
                    j++;
                    while (j < text.size()) {
                        if (text[j] == '\\' && j + 1 < text.size()) { j += 2; continue; }
                        if (text[j] == c) { j++; break; }
                        j++;
                    }
                    continue;
                }
                if (c == '{') depth++;
                else if (c == '}') depth--;
                j++;
            }
            out += text.substr(i, j - i);
            i = j;
            continue;
        }
        // Ordinary content: escapes and quotes have to be put back.
        const char c = text[i];
        if (c == '\"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == kEscapedDollar) {
            out += "\\$";
        } else {
            out += c;
        }
        i++;
    }
    out += '"';
    return out;
}

/// @brief True when a dict key can be written unquoted (`{k: 1}`).
///
/// A bare key must scan as a single identifier and must not be a reserved word:
/// `{"if": 1}` has to keep its quotes because `if` cannot start an expression,
/// and `{"a b": 1}` obviously cannot either.  Anything else is quoted, which is
/// always equivalent since a bare key and a string key denote the same entry.
static bool isBareDictKey(const std::string& name) {
    if (name.empty() || Lexer::isReservedWord(name)) return false;
    for (size_t i = 0; i < name.size(); i++) {
        const unsigned char u = static_cast<unsigned char>(name[i]);
        const bool ok = std::isalpha(u) != 0 || name[i] == '_' || u > 127 ||
                        (i > 0 && std::isdigit(u) != 0);
        if (!ok) return false;
    }
    return true;
}

/// @brief Render a constant value as it must appear in source.
static std::string valueToSourceLiteral(const Value& v) {
    if (v.isGcString()) return quoteString(v.asGcString()->value);
    if (v.isBool()) return v.asBool() ? "true" : "false";
    if (v.isNull()) return "null";
    if (v.isInt()) return std::to_string(v.asInt());
    if (v.isBigInt()) return v.asBigInt()->value.toDecimal();
    if (v.isDouble()) return formatFloatLiteral(v.asDouble());
    // Nothing else can appear as a literal in source; fall back to display.
    return valueToString(v);
}
// =========================================================================
// Token juxtaposition
//
// The formatter renders fragments and concatenates them.  Concatenation is
// only safe when the result re-lexes into the same two tokens: `not` followed
// directly by its operand produces `nota`, a single identifier, which silently
// rewrites the program (and `-` before a negative operand produces `--5`,
// which reads as a decrement).  Rather than patch individual call sites, the
// rule below decides from the characters alone whether a space is required.
// =========================================================================

/// @brief True for characters that continue an identifier or a number.
static bool isWordChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || u > 127;
}

/// @brief True when the lexer would fuse @p a and @p b into one longer token.
static bool isFusingPair(char a, char b) {
    switch (a) {
        case '?': return b == '?' || b == '.';
        case '+': return b == '+' || b == '=';
        case '-': return b == '-' || b == '=';
        case '*': return b == '*' || b == '=';
        // `//` and `/*` would start a comment.
        case '/': return b == '/' || b == '*' || b == '=';
        case '%': return b == '=';
        case '=': return b == '=' || b == '>';
        case '<': return b == '<' || b == '=';
        case '>': return b == '>' || b == '=';
        case '!': return b == '=';
        case '&': return b == '&' || b == '=';
        case '|': return b == '|' || b == '=';
        case '^': return b == '=';
        default:  return false;
    }
}

/// @brief See SourceFormatter::juxtapositionFuses, which wraps this.
static bool lexemesFuse(const std::string& left, const std::string& right) {
    if (left.empty() || right.empty()) return false;
    const char a = left.back();
    const char b = right.front();
    // Two word characters run together: `not`+`a` is `nota`, and `x`+`1` is `x1`.
    if (isWordChar(a) && isWordChar(b)) return true;
    // A number beside a '.' becomes one float literal (`1`+`.5`).
    if (a == '.' && std::isdigit(static_cast<unsigned char>(b)) != 0) return true;
    if (std::isdigit(static_cast<unsigned char>(a)) != 0 && b == '.') return true;
    return isFusingPair(a, b);
}

bool SourceFormatter::juxtapositionFuses(const std::string& left, const std::string& right) {
    return lexemesFuse(left, right);
}

/// @brief Juxtapose two fragments, inserting a space only where needed.
static std::string fuseLexemes(const std::string& left, const std::string& right) {
    return lexemesFuse(left, right)
               ? left + " " + right
               : left + right;
}
// =========================================================================
// Float literal rendering
// =========================================================================

/**
 * @brief Render a double as a Vora float literal that is lossless and re-parseable.
 *
 * Two properties matter, and the previous implementation had neither:
 *
 *   1. **Lossless.** The value must survive a format → parse round trip
 *      bit-for-bit. `std::to_chars` with `chars_format::fixed` and no precision
 *      emits the *shortest* decimal that round-trips, so `1234.5678` stays
 *      `1234.5678` instead of collapsing to six significant digits.
 *
 *   2. **Re-parseable, and still a float.** The lexer accepts only
 *      `digit+ [ "." digit+ ]`, so exponent notation is unusable — emitting
 *      `1.23457e+08` produced a token stream of `1.23457`, `e`, `+`, `08`,
 *      which ASI then split into two statements. When a variable named `e`
 *      happened to be in scope that silently evaluated to a *different number*.
 *      Fixed notation avoids this, and because a lexeme without a `.` parses as
 *      an **int**, a decimal point is always appended — otherwise `42.0`
 *      round-tripped to the integer `42`, silently changing the type.
 *
 * Infinities and NaNs have no Vora literal spelling and cannot arise from a
 * literal either (an out-of-range float literal fails at parse time), so the
 * fallback below is defensive only.
 *
 * @param d The double to render.
 * @return A decimal literal with a `.`, parseable back to exactly @p d.
 */
static std::string formatFloatLiteral(double d) {
    if (std::isnan(d)) return "0.0 / 0.0";          // not a Vora literal; see above
    if (std::isinf(d)) return d < 0 ? "-1.0 / 0.0" : "1.0 / 0.0";

    auto render = [](double v, std::chars_format fmt) {
        char buf[512];
        auto res = std::to_chars(buf, buf + sizeof(buf), v, fmt);
        if (res.ec != std::errc()) return std::string();
        return std::string(buf, res.ptr);
    };

    // Both layouts are shortest-round-trip; they differ only in shape, so emit
    // whichever is shorter and let fixed notation win ties (it reads better).
    // This matters most at the extremes: `1e300` is 6 characters where the
    // equivalent fixed form is 301.
    const std::string fixed = render(d, std::chars_format::fixed);
    const std::string general = render(d, std::chars_format::general);
    std::string out;
    if (fixed.empty()) out = general;
    else if (general.empty()) out = fixed;
    else out = (general.size() < fixed.size()) ? general : fixed;

    // Whatever was chosen must re-lex as a *float*: a lexeme carrying neither a
    // '.' nor an exponent is an integer literal, which would silently change
    // the value's type on the next parse.
    const bool readsAsFloat =
        out.find('.') != std::string::npos ||
        out.find('e') != std::string::npos ||
        out.find('E') != std::string::npos;
    if (!readsAsFloat) out += ".0";
    return out;
}

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
    depth_ = 0;  // reset depth guard for each format invocation
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
        case TokenType::QUESTION_QUESTION:
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

    // Call, index, property, optional chain — highest precedence before atoms
    if (dynamic_cast<const CallExpr*>(&expr) ||
        dynamic_cast<const IndexExpr*>(&expr) ||
        dynamic_cast<const PropertyExpr*>(&expr) ||
        dynamic_cast<const OptionalChainExpr*>(&expr)) {
        return PREC_CALL;
    }

    // Atoms: literal, variable, this, super, array, dict, grouping
    return PREC_PRIMARY;
}

// =========================================================================
// formatExpr — precedence-aware expression formatting
// =========================================================================

std::string SourceFormatter::formatExpr(const Expr& expr, int minPrec) {
    if (++depth_ > MAX_FORMAT_DEPTH) {
        return "(too deep)";
    }
    int ownPrec = exprPrecedence(expr);

    std::string result;
    if (ownPrec < minPrec) {
        result = "(" + expr.accept(*this) + ")";
    } else {
        result = expr.accept(*this);
    }
    depth_--;
    return result;
}

// =========================================================================
// Statement formatting helpers
// =========================================================================

std::string SourceFormatter::formatBlockBody(const Stmt& stmt) {
    if (++depth_ > MAX_FORMAT_DEPTH) {
        depth_--;
        return " { (too deep) }";
    }
    // If the statement is already a BlockStmt, format it with a leading space
    // for "if (...) {" / "while (...) {" / etc.
    std::string result;
    if (auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
        result = " " + visitBlockStmt(*block);
    } else {
        // Otherwise wrap a single statement in braces.
        std::stringstream ss;
        ss << " {";
        incIndent();
        ss << nl();
        ss << formatStmtWithComments(stmt);
        decIndent();
        ss << nl() << "}";
        result = ss.str();
    }
    depth_--;
    return result;
}

// Render a comment for the current line.  A block comment may span lines; its
// continuation lines are re-indented rather than emitted flush left, so nesting
// survives reformatting.
//
// Note the indentation convention, which matches statements: the text carries
// no leading indent of its own, because the caller has already positioned the
// line with nl().  Adding it here would double the indent.
std::string SourceFormatter::formatComment(const Comment& comment, bool leading) {
    std::string out = leading ? std::string() : " ";
    const std::string& text = comment.text;
    for (size_t i = 0; i < text.size(); ++i) {
        out += text[i];
        if (text[i] == '\n' && i + 1 < text.size()) out += indentStr();
    }
    return out;
}

// Render one statement together with its comment trivia.
//
// Every statement-emission site routes through here, which is what makes the
// "no comment is ever dropped" property hold: a comment the parser cannot
// attach to a specific statement lands on the enclosing block or program,
// and all of those are printed.
std::string SourceFormatter::formatStmtWithComments(const Stmt& stmt) {
    std::string out;
    for (const Comment& c : stmt.leadingComments) {
        out += formatComment(c, true);
        out += nl();
    }
    out += stmt.accept(*this);
    for (const Comment& c : stmt.trailingComments) {
        out += formatComment(c, false);
    }
    return out;
}

std::string SourceFormatter::formatStatements(
    const std::vector<std::unique_ptr<Stmt>>& stmts
) {
    // Render first so the separator decision can look ahead without formatting
    // the next statement twice.
    std::vector<std::string> rendered;
    rendered.reserve(stmts.size());
    for (const auto& stmt : stmts) {
        rendered.push_back(formatStmtWithComments(*stmt));
    }
    std::stringstream ss;
    for (size_t i = 0; i < rendered.size(); ++i) {
        if (i > 0) {
            ss << nl();
        }
        ss << rendered[i];
        if (i + 1 < rendered.size() && needsStatementSeparator(rendered[i + 1])) {
            ss << ";";
        }
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
        // A rest parameter is spelled `...name`; dropping the marker turned
        // `func f(a, ...rest)` into `func f(a, rest)`, which silently changes
        // the function's arity and what `rest` receives.
        if (params[i].pattern) {
            ss << (params[i].isRest ? "..." : "");
            ss << formatBindingPattern(*params[i].pattern);
        } else {
            ss << (params[i].isRest ? "..." : "");
            ss << params[i].name;
        }
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
    const auto& v = expr.value;
    if (v.isNull()) return "null";
    if (v.isBool()) return v.asBool() ? "true" : "false";
    // A quoted string literal: the stored value is the decoded content, so the
    // quotes and escapes have to be put back (see quoteString).
    if (v.isGcString()) return quoteString(v.asGcString()->value);
    if (v.isInt()) return std::to_string(v.asInt());
    // Big integers keep their exact digits: the formatter has to round-trip an
    // oversized literal unchanged, and decimal is the only lossless way to write
    // one back out.
    if (v.isBigInt()) return v.asBigInt()->value.toDecimal();
    if (v.isDouble()) return formatFloatLiteral(v.asDouble());
    // All other Value types (GcPtr<Array>, GcPtr<Dict>, etc.)
    // should not appear as literal expressions at format time.
    return "<value>";
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
    // Usually no space (`-x`, `!x`), but one is required when juxtaposition
    // would fuse: `not` before its operand (`not a`), or `-` before a negative
    // operand (`- -x`, since `--x` would lex as a decrement).
    return fuseLexemes(expr.op.lexeme, formatExpr(*expr.right, PREC_UNARY));
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
        if (!expr.argumentNames.empty() && !expr.argumentNames[i].empty()) {
            ss << expr.argumentNames[i] << " = ";
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
        // `{k: 1}` stores a bare identifier, `{"k": 1}` a string literal, and
        // `{[expr]: 1}` anything else.  Only the first may be written unquoted:
        // writing a string key bare silently changes the key, and writing a
        // computed key bare turns an evaluated key into a literal one.
        // A string key is written bare only when that is equivalent: a bare key
        // has to scan as one non-reserved identifier, so `{k: 1}` keeps its
        // spelling while `{"a b": 1}` and `{"if": 1}` keep their quotes.
        // Any other key expression is emitted as-is — `{[x]: 1}` stores an array
        // key and formatExpr already produces the brackets.
        const Expr* key = expr.pairs[i].first.get();
        if (const auto* lit = dynamic_cast<const LiteralExpr*>(key)) {
            if (lit->value.isGcString() && isBareDictKey(lit->value.asGcString()->value)) {
                ss << lit->value.asGcString()->value;
            } else {
                ss << valueToSourceLiteral(lit->value);
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
        ss << fuseLexemes(expr.op.lexeme, formatExpr(*expr.target, PREC_UNARY));
    } else {
        ss << fuseLexemes(formatExpr(*expr.target, PREC_UNARY), expr.op.lexeme);
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

std::string SourceFormatter::visitMatchExpr(const MatchExpr& expr) {
    std::stringstream ss;
    ss << "match ";
    ss << formatExpr(*expr.scrutinee, 0);
    ss << " {";
    incIndent();

    for (size_t i = 0; i < expr.cases.size(); i++) {
        const auto& c = expr.cases[i];
        ss << nl();

        // Patterns
        for (size_t j = 0; j < c.patterns.size(); j++) {
            if (j > 0) ss << " | ";
            const auto& p = c.patterns[j];
            if (p.kind == PatternKind::Wildcard) {
                ss << "_";
            } else if (p.kind == PatternKind::Literal) {
                ss << valueToSourceLiteral(p.literal);
            } else if (p.kind == PatternKind::Range) {
                ss << valueToSourceLiteral(p.rangeLow);
                ss << (p.rangeInclusive ? "..=" : "..");
                ss << valueToSourceLiteral(p.rangeHigh);
            }
        }

        ss << " => ";

        if (c.blockBody) {
            // Format block body
            ss << "{";
            incIndent();
            const auto& stmts = c.blockBody->statements;
            for (size_t si = 0; si < stmts.size(); si++) {
                ss << nl();
                ss << formatStmtWithComments(*stmts[si]);
                if (si + 1 < stmts.size()) {
                    const std::string next = formatStmtWithComments(*stmts[si + 1]);
                    if (needsStatementSeparator(next)) ss << ";";
                }
            }
            decIndent();
            ss << nl() << "}";
        } else if (c.body) {
            ss << formatExpr(*c.body, 0);
        } else {
            ss << "null";
        }

        if (i + 1 < expr.cases.size()) {
            ss << ",";
        }
    }

    decIndent();
    ss << nl() << "}";
    return ss.str();
}

std::string SourceFormatter::visitFuncExpr(const FuncExpr& expr) {
    std::stringstream ss;
    if (expr.isAsync) ss << "async ";
    ss << "func";
    ss << formatParams(expr.params);
    ss << " ";

    // Format body as a block
    incIndent();
    ss << "{" << nl();
    const auto& stmts = expr.body->statements;
    for (size_t i = 0; i < stmts.size(); i++) {
        ss << formatStmtWithComments(*stmts[i]);
        if (i + 1 < stmts.size()) {
            const std::string next = formatStmtWithComments(*stmts[i + 1]);
            if (needsStatementSeparator(next)) ss << ";";
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

std::string SourceFormatter::visitAwaitExpr(const AwaitExpr& expr) {
    if (expr.value) {
        return "await " + formatExpr(*expr.value, 0);
    }
    return "await";
}

std::string SourceFormatter::visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) {
    std::string result = formatBindingPattern(*expr.binding);
    result += " = ";
    result += formatExpr(*expr.value, PREC_ASSIGNMENT);
    return result;
}

std::string SourceFormatter::visitSpreadExpr(const SpreadExpr& expr) {
    return "..." + formatExpr(*expr.expr, PREC_CALL);
}

std::string SourceFormatter::visitListCompExpr(const ListCompExpr& expr) {
    std::stringstream ss;
    ss << "[" << formatExpr(*expr.resultExpr, 0);
    ss << " for " << expr.variable << " in ";
    ss << formatExpr(*expr.iterable, 0);
    if (expr.condition) {
        ss << " if " << formatExpr(*expr.condition, 0);
    }
    ss << "]";
    return ss.str();
}

std::string SourceFormatter::visitDictCompExpr(const DictCompExpr& expr) {
    std::stringstream ss;
    ss << "{" << formatExpr(*expr.keyExpr, 0);
    ss << ": " << formatExpr(*expr.valueExpr, 0);
    ss << " for " << expr.variable << " in ";
    ss << formatExpr(*expr.iterable, 0);
    if (expr.condition) {
        ss << " if " << formatExpr(*expr.condition, 0);
    }
    ss << "}";
    return ss.str();
}

std::string SourceFormatter::visitOptionalChainExpr(const OptionalChainExpr& expr) {
    std::stringstream ss;
    ss << formatExpr(*expr.object, PREC_CALL);
    ss << "?.";
    switch (expr.kind) {
        case OptionalChainExpr::Kind::PROPERTY:
            ss << expr.property;
            break;
        case OptionalChainExpr::Kind::CALL: {
            ss << "(";
            for (size_t i = 0; i < expr.arguments.size(); ++i) {
                if (i > 0) ss << ", ";
                if (!expr.argumentNames.empty() && !expr.argumentNames[i].empty()) {
                    ss << expr.argumentNames[i] << " = ";
                }
                ss << formatExpr(*expr.arguments[i], 0);
            }
            ss << ")";
            break;
        }
        case OptionalChainExpr::Kind::INDEX:
            ss << "[" << formatExpr(*expr.index, 0) << "]";
            break;
    }
    return ss.str();
}

std::string SourceFormatter::visitErrorExpr(const ErrorExpr& expr) {
    // Emit a placeholder that is valid Vora syntax (null) so formatted
    // output remains syntactically valid even for partial/error ASTs.
    return "null /* ERROR: " + expr.message + " */";
}

std::string SourceFormatter::formatBindingPattern(const BindingPattern& pattern) {
    switch (pattern.kind()) {
        case BindingKind::Identifier: {
            const auto& id = static_cast<const IdentifierBinding&>(pattern);
            if (id.defaultValue) {
                return id.name + " = " + formatExpr(*id.defaultValue, PREC_ASSIGNMENT);
            }
            return id.name;
        }
        case BindingKind::Array: {
            const auto& arr = static_cast<const ArrayBinding&>(pattern);
            std::string result = "[";
            for (size_t i = 0; i < arr.elements.size(); i++) {
                if (i > 0) result += ", ";
                result += formatBindingPattern(*arr.elements[i]);
            }
            if (arr.rest) {
                if (!arr.elements.empty()) result += ", ";
                result += "..." + formatBindingPattern(*arr.rest);
            }
            result += "]";
            return result;
        }
        case BindingKind::Object: {
            const auto& obj = static_cast<const ObjectBinding&>(pattern);
            std::string result = "{";
            for (size_t i = 0; i < obj.properties.size(); i++) {
                if (i > 0) result += ", ";
                const auto& prop = obj.properties[i];
                if (prop.isShorthand) {
                    // `{y = 5}` is shorthand *with* a default: the default lives
                    // on the sub-pattern, so printing only the key dropped it and
                    // silently changed what the destructuring binds.
                    result += formatBindingPattern(*prop.pattern);
                } else {
                    result += prop.key + ": " + formatBindingPattern(*prop.pattern);
                }
            }
            if (obj.rest) {
                if (!obj.properties.empty()) result += ", ";
                result += "..." + formatBindingPattern(*obj.rest);
            }
            result += "}";
            return result;
        }
    }
    return "?pattern?";
}

// =====================================================================
// StmtVisitor<std::string> — statement formatting
// =====================================================================

std::string SourceFormatter::visitExprStmt(const ExprStmt& stmt) {
    return formatExpr(*stmt.expression, 0);
}

std::string SourceFormatter::visitLetStmt(const LetStmt& stmt) {
    std::stringstream ss;
    ss << (stmt.isConst ? "const " : "let ");

    if (stmt.binding) {
        ss << formatBindingPattern(*stmt.binding);
    } else {
        ss << stmt.name;
    }

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
    // Comments that sat before this block's closing brace.
    for (const Comment& c : stmt.trailingComments) {
        ss << nl() << formatComment(c, true);
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
    if (!stmt.label.empty()) ss << stmt.label << ": ";
    ss << "while (";
    ss << formatExpr(*stmt.condition, 0);
    ss << ")";
    ss << formatBlockBody(*stmt.body);
    return ss.str();
}

std::string SourceFormatter::visitDoWhileStmt(const DoWhileStmt& stmt) {
    std::stringstream ss;
    if (!stmt.label.empty()) ss << stmt.label << ": ";
    ss << "do";
    ss << formatBlockBody(*stmt.body);
    ss << " while (";
    ss << formatExpr(*stmt.condition, 0);
    ss << ")";
    return ss.str();
}

std::string SourceFormatter::visitForStmt(const ForStmt& stmt) {
    // Vora syntax: for var in expr { ... }  (no parentheses)
    std::stringstream ss;
    if (!stmt.label.empty()) ss << stmt.label << ": ";
    ss << "for ";
    if (stmt.variablePattern) {
        ss << formatBindingPattern(*stmt.variablePattern);
    } else {
        ss << "?";
    }
    ss << " in ";
    ss << formatExpr(*stmt.iterable, 0);
    ss << formatBlockBody(*stmt.body);
    return ss.str();
}

std::string SourceFormatter::visitCForStmt(const CForStmt& stmt) {
    // Vora syntax: for (init; cond; incr) { ... }
    std::stringstream ss;
    if (!stmt.label.empty()) ss << stmt.label << ": ";
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
    // `async` must be reproduced: without it the body's `await` becomes illegal
    // and the program stops compiling.  The class-body parser has no async form,
    // so `isStatic` and `isAsync` cannot both be set here.
    if (stmt.isAsync) ss << "async ";
    if (stmt.isStatic) ss << "this.";
    ss << "func " << stmt.name;
    ss << formatParams(stmt.params);
    ss << " ";
    ss << visitBlockStmt(*stmt.body);
    return ss.str();
}

std::string SourceFormatter::visitObjStmt(const ClassStmt& stmt) {
    std::stringstream ss;
    ss << "class " << stmt.name;

    // Parent class names (syntax: class Child : Parent1, Parent2 (params) { ... })
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
        ss << formatStmtWithComments(*stmt.methods[i]);
    }

    decIndent();
    ss << nl() << "}";

    return ss.str();
}

std::string SourceFormatter::visitBreakStmt(const BreakStmt& stmt) {
    // `break outer` — the label has to survive formatting, or the round-trip
    // would silently change which loop is exited.
    if (stmt.targetLabel.empty()) return "break";
    return "break " + stmt.targetLabel;
}

std::string SourceFormatter::visitContinueStmt(const ContinueStmt& stmt) {
    if (stmt.targetLabel.empty()) return "continue";
    return "continue " + stmt.targetLabel;
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
    return "export " + formatStmtWithComments(*stmt.declaration);
}

std::string SourceFormatter::visitDeferStmt(const DeferStmt& stmt) {
    return "defer " + formatExpr(*stmt.expression, 0) + ";";
}

std::string SourceFormatter::visitErrorStmt(const ErrorStmt& stmt) {
    // Emit as a comment so formatted output remains valid Vora.
    return "/* ERROR: " + stmt.message + " */";
}

// =====================================================================
// ProgramVisitor<std::string>
// =====================================================================

std::string SourceFormatter::visitProgram(const Program& program) {
    std::stringstream ss;
    ss << formatStatements(program.statements);
    // Comments after the last statement (or a file of nothing but comments).
    for (const Comment& c : program.trailingComments) {
        if (!program.statements.empty()) ss << nl();
        ss << formatComment(c, true);
    }
    ss << "\n";
    return ss.str();
}

}  // namespace vora
