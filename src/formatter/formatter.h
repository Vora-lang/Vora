/**
 * @file src/formatter/formatter.h
 * @brief AST-based source code formatter for the Vora language (vora fmt).
 *
 * @details
 * SourceFormatter walks a parsed AST and produces consistently formatted
 * Vora source code.  It implements all three templated visitor interfaces
 * (ExprVisitor, StmtVisitor, ProgramVisitor) with a std::string return type,
 * building the output through recursive descent without side-channel state.
 *
 * **Formatting rules applied:**
 *   - 4-space indentation, no tabs
 *   - Spaces around binary operators and after keywords (if, while, for, etc.)
 *   - No space before `(` in calls; space before `{` in blocks
 *   - Braced blocks for all control-flow bodies (always-brace style)
 *   - `else` / `else if` on the same line as the preceding closing brace
 *   - Precedence-aware parenthesization (removes redundant parens, adds
 *     necessary ones for correctness)
 *
 * **Usage** (entry point):
 *   @code
 *   SourceFormatter fmt;
 *   std::string formatted = fmt.format(&program);
 *   @endcode
 *
 * @note The formatter requires a valid, error-free AST.  Pass the result of
 *       a successful parse — formatting an AST containing `ErrorExpr` or
 *       `ErrorStmt` nodes produces incomplete output.
 *
 * @see ExprVisitor, StmtVisitor, ProgramVisitor
 */

#pragma once

#include <string>

#include "../ast/expr.h"
#include "../ast/expr_visitor.h"
#include "../ast/stmt.h"
#include "../ast/stmt_visitor.h"
#include "../ast/program.h"

namespace vora {

/**
 * @brief AST-based source code formatter (vora fmt command).
 *
 * @details
 * Implements ExprVisitor<std::string>, StmtVisitor<std::string>, and
 * ProgramVisitor<std::string> to produce consistently formatted Vora source
 * code from any valid AST.  The formatter uses recursive descent through
 * the visitor pattern, building the output string as it traverses the tree.
 *
 * **Key design points:**
 *   - **Indentation state** is tracked via an integer counter, not a string
 *     accumulator, so nested formatting calls automatically receive the
 *     correct indent level.
 *   - **Recursion depth guard** (`MAX_FORMAT_DEPTH`) prevents stack overflow
 *     on deeply nested but syntactically valid ASTs (e.g., thousands of
 *     nested binary expressions).
 *   - **Precedence-aware parenthesization**: the formatter removes redundant
 *     parentheses and only adds them when required for correct operator
 *     associativity or precedence.  This is implemented via `formatExpr()`
 *     which compares the sub-expression's effective precedence against a
 *     required minimum.
 *
 * **Thread safety:** Each `SourceFormatter` instance is self-contained
 * (only `indent_` and `depth_` integer state).  Create one instance per
 * format operation; the class is not designed for concurrent access to a
 * single instance, but multiple instances can format independently in
 * parallel.
 *
 * **Error handling:** The formatter assumes a valid AST.  If the AST
 * contains `ErrorExpr` or `ErrorStmt` nodes (from error-tolerant parsing),
 * those nodes produce empty or placeholder output — the result will be
 * invalid Vora source.  Always check `Parser::hasError()` before formatting.
 *
 * @see format() for the main entry point
 * @see formatExpr() for precedence-aware expression formatting
 */
class SourceFormatter : public ExprVisitor<std::string>,
                        public StmtVisitor<std::string>,
                        public ProgramVisitor<std::string> {
public:
    /**
     * @brief Default constructor.
     *
     * Initializes indentation and recursion depth to zero.  A single
     * `SourceFormatter` instance can be reused across multiple `format()`
     * calls — `format()` resets all mutable state at the start of each call.
     *
     * @see format()
     */
    SourceFormatter() : indent_(0) {}

    /**
     * @brief Entry point — format an entire program.
     *
     * Resets internal state (indentation, depth counter) and formats the
     * top-level statements by delegating to `visitProgram()`.  This is the
     * only public method needed for typical usage.
     *
     * @param program The parsed AST to format.  Must not be `nullptr` and
     *                must not contain `ErrorExpr`/`ErrorStmt` nodes for
     *                correct output.
     * @return The formatted source code as a single string, with a trailing
     *         newline.
     */
    std::string format(const Program* program);

private:
    /**
     * @brief Maximum recursion depth to prevent stack overflow on deeply
     *        nested ASTs.
     *
     * Every visitor increments `depth_` on entry and decrements on exit.
     * If `depth_` exceeds this limit, the formatter returns a placeholder
     * string (`"...too deep..."`) instead of recursing further.  This
     * prevents crashes on pathological inputs like 10,000 nested binary
     * expressions while still producing partial, recognizable output.
     */
    static constexpr int MAX_FORMAT_DEPTH = 10000;

    /**
     * @brief Current indentation depth in levels.
     *
     * `0` = top-level (no indentation).  Each level adds 4 spaces.
     * Managed by `incIndent()` / `decIndent()`.  This counter is the sole
     * source of truth for indentation — no string-accumulator indentation
     * state exists, so nested calls naturally inherit the correct level.
     */
    int indent_ = 0;

    /**
     * @brief Recursion depth counter for stack-overflow protection.
     *
     * Incremented at the top of every `visit*()` method (via a RAII guard
     * or explicit increment/decrement) and checked against
     * `MAX_FORMAT_DEPTH`.  Reset to 0 at the start of each `format()` call.
     */
    int depth_ = 0;

    // --- indentation helpers ---

    /**
     * @brief Build a string of spaces for the current indentation level.
     *
     * Each indentation level contributes 4 space characters.
     *
     * @return A string containing `indent_ * 4` space characters.
     *         Returns an empty string when `indent_` is 0.
     */
    std::string indentStr() const;

    /**
     * @brief Convenience: newline character followed by current indentation.
     *
     * Equivalent to `"\n" + indentStr()`.  Used at the start of each
     * statement line to ensure proper alignment.
     *
     * @return A string starting with `'\n'` followed by the current
     *         indentation spaces.
     */
    std::string nl() const;

    /**
     * @brief Increment the indentation level by one.
     *
     * Called when entering a new block (e.g., function body, loop body,
     * if/else block).  Each increment adds 4 spaces to subsequent output.
     */
    void incIndent() { indent_++; }

    /**
     * @brief Decrement the indentation level by one (clamped at zero).
     *
     * Called when leaving a block.  The clamp to zero is a safety measure —
     * under normal operation `decIndent()` is always paired with a prior
     * `incIndent()`, so the clamp should never trigger.
     */
    void decIndent() { if (indent_ > 0) indent_--; }

    // --- expression formatting with precedence ---

    /**
     * @brief Format an expression, wrapping it in parentheses if its effective
     *        precedence is lower than the required minimum.
     *
     * This is the core of precedence-aware formatting.  When a sub-expression
     * is used as an operand of a higher-precedence outer operator, it must be
     * parenthesized to preserve the intended evaluation order.  Conversely,
     * parenthesized expressions whose inner precedence is already high enough
     * are "unwrapped" (redundant parens removed).
     *
     * **Example:**
     *   - `a + b * c`    → `a + b * c`  (no parens needed; `*` binds tighter)
     *   - `(a + b) * c`  → `(a + b) * c`  (parens required; `+` is lower than `*`)
     *   - `((a + b))`    → `(a + b)`  (redundant outer parens stripped)
     *
     * @param expr    The expression to format.
     * @param minPrec The minimum precedence required to avoid wrapping in
     *                parentheses.  Higher values make parenthesization more
     *                likely.
     * @return The formatted expression string, possibly wrapped in `(...)`.
     */
    std::string formatExpr(const Expr& expr, int minPrec);

    /**
     * @brief Get the effective precedence of an expression's outermost operator.
     *
     * Returns a high value for atoms (literals, variables, function calls,
     * property/index access) and the operator precedence for compound
     * expressions.  Used by `formatExpr()` to decide whether parens are needed.
     *
     * @param expr The expression to inspect.
     * @return The effective precedence level.  Higher values indicate tighter
     *         binding.  Atom-like expressions return a very high precedence
     *         (effectively "never needs parens").
     */
    static int exprPrecedence(const Expr& expr);

    /**
     * @brief Get the precedence of a token type.
     *
     * Uses the same precedence table as `Parser::getPrecedence()` to ensure
     * consistent operator binding rules between parsing and formatting.
     *
     * @param type The token type to look up (e.g., `PLUS`, `STAR`).
     * @return The precedence level for that token type, or a low sentinel
     *         value for non-operator tokens.
     */
    static int tokenPrecedence(TokenType type);

    /**
     * @brief Check whether a binary token type is right-associative.
     *
     * Right-associative operators (e.g., assignment `=`, compound assignment
     * `+=`, ternary `?`) require different parenthesization rules than
     * left-associative ones.  For left-associative operators, a sub-expression
     * with *equal* precedence needs parens when on the right; for
     * right-associative operators, it needs parens when on the left.
     *
     * @param type The token type to check.
     * @return `true` if the operator is right-associative, `false` otherwise.
     */
    static bool isRightAssoc(TokenType type);

    // --- statement body helpers ---

    /**
     * @brief Format a statement as a braced block body.
     *
     * Implements the **always-brace** formatting rule: every control-flow
     * body is wrapped in braces.  If @p stmt is already a `BlockStmt`, the
     * formatter indents and formats its contents (the outer braces are
     * emitted by the caller, e.g., `visitIfStmt()`).  Otherwise, the single
     * statement is wrapped in `{ ... }` with proper indentation.
     *
     * @param stmt The statement to format as a block body (may be a
     *             `BlockStmt` or any single statement).
     * @return The formatted body content, without the control structure's
     *         own outer braces.
     */
    std::string formatBlockBody(const Stmt& stmt);

    /**
     * @brief Format a list of statements as a block body.
     *
     * Used when we already have a statement vector (e.g., `BlockStmt`
     * contents, `Program` top-level statements).  Increments indentation,
     * formats each statement on its own line, then decrements.
     *
     * @param stmts The vector of statements to format.
     * @return The formatted statement list, with each statement starting on
     *         a new indented line.
     */
    std::string formatStatements(const std::vector<std::unique_ptr<Stmt>>& stmts);

    /**
     * @brief Format a function-like parameter list.
     *
     * Produces comma-separated parameter declarations including optional
     * type annotations and default values.
     *
     * @param params The vector of parameter declarations (name, optional type,
     *               optional default value).
     * @return A string of the form `"(p1, p2, ...)"` or `"()"` for empty
     *         parameter lists.
     */
    std::string formatParams(const std::vector<ParamDecl>& params);

    /**
     * @brief Format a destructuring binding pattern.
     *
     * Supports both array destructuring (`[a, b, ...rest]`) and object
     * destructuring (`{x, y}`).  Used by `let` declarations and destructuring
     * assignment expressions.
     *
     * @param pattern The binding pattern to format (array or object
     *                destructuring with optional nested patterns).
     * @return A string like `"[a, b]"` or `"{x, y}"`.
     */
    std::string formatBindingPattern(const BindingPattern& pattern);

    // =====================================================================
    // ExprVisitor<std::string> overrides
    // =====================================================================

    /**
     * @brief Format a literal expression (number, string, bool, null).
     *
     * Outputs the literal in its canonical Vora source representation.
     * Numbers use their string representation from the token; strings
     * include surrounding quotes.
     *
     * @param expr The literal expression node.
     * @return The formatted literal value as a string.
     */
    std::string visitLiteralExpr(const LiteralExpr& expr) override;

    /**
     * @brief Format a binary operator expression with precedence awareness.
     *
     * Formats `lhs op rhs`, applying parentheses to either operand if their
     * precedence is lower than the operator's precedence.  Respects operator
     * associativity (left vs. right) when deciding which side needs parens.
     *
     * @param expr The binary expression node (contains left operand, operator
     *             token, and right operand).
     * @return The formatted binary expression string.
     */
    std::string visitBinaryExpr(const BinaryExpr& expr) override;

    /**
     * @brief Format a parenthesized grouping — may remove redundant parens.
     *
     * If the inner expression's precedence is already high enough to stand
     * alone, the parentheses are stripped.  Otherwise they are preserved.
     *
     * @param expr The grouping expression node.
     * @return The formatted expression string, possibly without outer parens.
     */
    std::string visitGroupingExpr(const GroupingExpr& expr) override;

    /**
     * @brief Format a unary operator expression (`!x`, `-x`, `~x`, etc.).
     *
     * Unlike binary operators, no spacing is added between the operator
     * and the operand.  Prefix operators are placed before the operand;
     * postfix operators (if any) are placed after.
     *
     * @param expr The unary expression node.
     * @return The formatted unary expression string.
     */
    std::string visitUnaryExpr(const UnaryExpr& expr) override;

    /**
     * @brief Format a variable reference (just the identifier name).
     *
     * @param expr The variable expression node.
     * @return The variable name as a string.
     */
    std::string visitVariableExpr(const VariableExpr& expr) override;

    /**
     * @brief Format an assignment expression (`x = value`).
     *
     * Assignment is right-associative; the right-hand side may need
     * parentheses if its precedence is lower than assignment.
     *
     * @param expr The assignment expression node.
     * @return The formatted assignment string.
     */
    std::string visitAssignmentExpr(const AssignmentExpr& expr) override;

    /**
     * @brief Format a compound assignment (`x += value`, `x -= value`, etc.).
     *
     * Same precedence/associativity rules as plain assignment.
     *
     * @param expr The compound assignment expression node.
     * @return The formatted compound assignment string.
     */
    std::string visitCompoundAssignmentExpr(const CompoundAssignmentExpr& expr) override;

    /**
     * @brief Format a function or method call expression.
     *
     * Produces `callee(arg1, arg2, ...)` with no space before `(`.
     *
     * @param expr The call expression node (callee + argument list).
     * @return The formatted call expression string.
     */
    std::string visitCallExpr(const CallExpr& expr) override;

    /**
     * @brief Format an array literal `[elem1, elem2, ...]`.
     *
     * @param expr The array expression node.
     * @return The formatted array literal string.
     */
    std::string visitArrayExpr(const ArrayExpr& expr) override;

    /**
     * @brief Format a dictionary literal `{key: val, ...}`.
     *
     * @param expr The dictionary expression node.
     * @return The formatted dictionary literal string.
     */
    std::string visitDictExpr(const DictExpr& expr) override;

    /**
     * @brief Format an index expression (`expr[index]`).
     *
     * @param expr The index expression node (target object + index).
     * @return The formatted index expression string.
     */
    std::string visitIndexExpr(const IndexExpr& expr) override;

    /**
     * @brief Format a property access (`expr.name`).
     *
     * @param expr The property expression node (object + property name).
     * @return The formatted property access string.
     */
    std::string visitPropertyExpr(const PropertyExpr& expr) override;

    /**
     * @brief Format a property assignment (`expr.name = value`).
     *
     * @param expr The property assignment expression node.
     * @return The formatted property assignment string.
     */
    std::string visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) override;

    /**
     * @brief Format an index assignment (`expr[index] = value`).
     *
     * @param expr The index assignment expression node.
     * @return The formatted index assignment string.
     */
    std::string visitIndexAssignmentExpr(const IndexAssignmentExpr& expr) override;

    /**
     * @brief Format a `this` expression.
     *
     * Simply outputs the keyword `this`.  Used inside object methods to
     * refer to the current instance.
     *
     * @param expr The this expression node.
     * @return The string `"this"`.
     */
    std::string visitThisExpr(const ThisExpr& expr) override;

    /**
     * @brief Format a `super` expression.
     *
     * Simply outputs the keyword `super`.  Used inside subclass methods to
     * reference the parent class.
     *
     * @param expr The super expression node.
     * @return The string `"super"`.
     */
    std::string visitSuperExpr(const SuperExpr& expr) override;

    /**
     * @brief Format an increment/decrement expression (`x++`, `--x`, `++x`, `x--`).
     *
     * Handles both prefix and postfix forms.  No space between operator
     * and operand.
     *
     * @param expr The inc/dec expression node (contains the operand, the
     *             operator token, and a prefix/postfix flag).
     * @return The formatted inc/dec expression string.
     */
    std::string visitIncDecExpr(const IncDecExpr& expr) override;

    /**
     * @brief Format a ternary conditional (`cond ? thenExpr : elseExpr`).
     *
     * The ternary operator is right-associative.  Spaces are added around
     * `?` and `:`.
     *
     * @param expr The ternary expression node.
     * @return The formatted ternary expression string.
     */
    std::string visitTernaryExpr(const TernaryExpr& expr) override;

    /**
     * @brief Format a match expression with all its cases.
     *
     * Produces a `match (expr) { case ... => ... ; default => ... }` block.
     * Each case arm is indented and separated.
     *
     * @param expr The match expression node.
     * @return The formatted match expression string.
     */
    std::string visitMatchExpr(const MatchExpr& expr) override;

    /**
     * @brief Format a function expression (anonymous function literal).
     *
     * Produces `func(params) { body }` or `func(params) => expr` for
     * single-expression arrow functions.
     *
     * @param expr The function expression node.
     * @return The formatted anonymous function string.
     */
    std::string visitFuncExpr(const FuncExpr& expr) override;

    /**
     * @brief Format a yield expression.
     *
     * Produces `yield` or `yield expr` depending on whether a value is
     * being yielded.
     *
     * @param expr The yield expression node.
     * @return The formatted yield expression string.
     */
    std::string visitYieldExpr(const YieldExpr& expr) override;

    /**
     * @brief Format an await expression.
     *
     * Formats `await <expr>`. If no value is present, formats `await`.
     *
     * @param expr The AwaitExpr to format.
     * @return The formatted await expression string.
     */
    std::string visitAwaitExpr(const AwaitExpr& expr) override;

    /**
     * @brief Format a destructuring assignment.
     *
     * Produces a pattern binding on the left-hand side followed by `=`
     * and the right-hand side value.
     *
     * @param expr The destructuring assignment expression node.
     * @return The formatted destructuring assignment string.
     */
    std::string visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) override;

    /**
     * @brief Format a spread expression (`...expr`).
     *
     * Used in function calls, array literals, and destructuring patterns.
     *
     * @param expr The spread expression node.
     * @return The string `"..."` followed by the formatted inner expression.
     */
    std::string visitSpreadExpr(const SpreadExpr& expr) override;

    /**
     * @brief Format a list comprehension.
     *
     * Produces `[expr for (item in iterable)]` with optional filter clauses.
     *
     * @param expr The list comprehension expression node.
     * @return The formatted list comprehension string.
     */
    std::string visitListCompExpr(const ListCompExpr& expr) override;

    /**
     * @brief Format a dictionary comprehension.
     *
     * Produces `{keyExpr: valExpr for (item in iterable)}` with optional
     * filter clauses.
     *
     * @param expr The dictionary comprehension expression node.
     * @return The formatted dictionary comprehension string.
     */
    std::string visitDictCompExpr(const DictCompExpr& expr) override;

    /**
     * @brief Format an optional chain expression (`expr?.prop`, `expr?.[idx]`,
     *        `expr?.(args)`).
     *
     * Produces the `?.` operator between the object and the property/index/call.
     *
     * @param expr The optional chain expression node.
     * @return The formatted optional chain expression string.
     */
    std::string visitOptionalChainExpr(const OptionalChainExpr& expr) override;

    /**
     * @brief Format an error placeholder expression (from error-tolerant parsing).
     *
     * Produces a minimal placeholder string — the formatter assumes valid ASTs.
     * Error nodes indicate that parsing failed for this sub-expression, so the
     * output will be incomplete.
     *
     * @param expr The error expression node.
     * @return A placeholder string representing the unrecoverable parse error.
     */
    std::string visitErrorExpr(const ErrorExpr& expr) override;

    // =====================================================================
    // StmtVisitor<std::string> overrides
    // =====================================================================

    /**
     * @brief Format an expression statement (expression followed by semicolon).
     *
     * @param stmt The expression statement node.
     * @return The formatted expression with a trailing `;` and newline.
     */
    std::string visitExprStmt(const ExprStmt& stmt) override;

    /**
     * @brief Format a `let` variable declaration.
     *
     * Produces `let name = value;` or `let name: Type = value;` for typed
     * declarations.  Supports destructuring patterns on the left-hand side.
     *
     * @param stmt The let statement node.
     * @return The formatted `let` declaration string.
     */
    std::string visitLetStmt(const LetStmt& stmt) override;

    /**
     * @brief Format a block statement `{ ... }`.
     *
     * Increments indentation, formats each inner statement, then decrements.
     * Empty blocks produce `{ }` on one line.
     *
     * @param stmt The block statement node.
     * @return The formatted block string including outer braces.
     */
    std::string visitBlockStmt(const BlockStmt& stmt) override;

    /**
     * @brief Format a `return` statement.
     *
     * Produces `return;` or `return expr;` depending on whether a return
     * value is present.
     *
     * @param stmt The return statement node.
     * @return The formatted return statement string.
     */
    std::string visitReturnStmt(const ReturnStmt& stmt) override;

    /**
     * @brief Format an `if`/`else if`/`else` chain.
     *
     * Produces `if (cond) { ... }` with the **always-brace** rule.  Chains
     * `else if` and `else` on the same line as the preceding `}`.
     *
     * @param stmt The if statement node (contains condition, then-branch,
     *             and optional else-branch).
     * @return The formatted if/else chain string.
     */
    std::string visitIfStmt(const IfStmt& stmt) override;

    /**
     * @brief Format a `while` loop.
     *
     * Produces `while (cond) { ... }` with always-brace formatting.
     *
     * @param stmt The while statement node.
     * @return The formatted while loop string.
     */
    std::string visitWhileStmt(const WhileStmt& stmt) override;

    /**
     * @brief Format a `do`/`while` loop.
     *
     * Produces `do { ... } while (cond);`.
     *
     * @param stmt The do-while statement node.
     * @return The formatted do-while loop string.
     */
    std::string visitDoWhileStmt(const DoWhileStmt& stmt) override;

    /**
     * @brief Format a `for`-`in` loop.
     *
     * Produces `for (var in iterable) { ... }` with always-brace formatting.
     * Supports destructuring in the loop variable position.
     *
     * @param stmt The for-in statement node.
     * @return The formatted for-in loop string.
     */
    std::string visitForStmt(const ForStmt& stmt) override;

    /**
     * @brief Format a C-style `for` loop.
     *
     * Produces `for (init; cond; update) { ... }` with always-brace formatting.
     * Each clause (init, cond, update) may be empty.
     *
     * @param stmt The C-style for statement node.
     * @return The formatted C-style for loop string.
     */
    std::string visitCForStmt(const CForStmt& stmt) override;

    /**
     * @brief Format a named function declaration.
     *
     * Produces `func name(params) { body }` or `func name(params) => expr`
     * for single-expression arrow functions.
     *
     * @param stmt The function statement node.
     * @return The formatted function declaration string.
     */
    std::string visitFuncStmt(const FuncStmt& stmt) override;

    /**
     * @brief Format an object/class declaration.
     *
     * Produces `obj Name { ... }` or `obj Name : Parent { ... }` with
     * inheritance.  Methods are indented inside the body; constructor
     * code is emitted at the top of the class body.
     *
     * @param stmt The object/class statement node.
     * @return The formatted class declaration string.
     */
    std::string visitObjStmt(const ObjStmt& stmt) override;

    /**
     * @brief Format a `break` statement.
     *
     * Produces `break;` on its own line.
     *
     * @param stmt The break statement node.
     * @return The string `"break;\n"`.
     */
    std::string visitBreakStmt(const BreakStmt& stmt) override;

    /**
     * @brief Format a `continue` statement.
     *
     * Produces `continue;` on its own line.
     *
     * @param stmt The continue statement node.
     * @return The string `"continue;\n"`.
     */
    std::string visitContinueStmt(const ContinueStmt& stmt) override;

    /**
     * @brief Format a `try`/`catch`/`finally` block.
     *
     * Produces `try { ... } catch (e) { ... } finally { ... }` with proper
     * indentation.  The `catch` and `finally` clauses are optional.
     *
     * @param stmt The try statement node.
     * @return The formatted try/catch/finally block string.
     */
    std::string visitTryStmt(const TryStmt& stmt) override;

    /**
     * @brief Format a `throw` statement.
     *
     * Produces `throw expr;` on its own line.
     *
     * @param stmt The throw statement node.
     * @return The formatted throw statement string.
     */
    std::string visitThrowStmt(const ThrowStmt& stmt) override;

    /**
     * @brief Format an `import` statement.
     *
     * Produces `import "module/path";` for standard imports.  Handles
     * named imports and import-aliasing syntax if present.
     *
     * @param stmt The import statement node.
     * @return The formatted import statement string.
     */
    std::string visitImportStmt(const ImportStmt& stmt) override;

    /**
     * @brief Format an `export` statement.
     *
     * Produces `export func/let/obj ...` by formatting the exported
     * declaration with the `export` keyword prefix.
     *
     * @param stmt The export statement node.
     * @return The formatted export statement string.
     */
    std::string visitExportStmt(const ExportStmt& stmt) override;

    /**
     * @brief Format a `defer` statement.
     *
     * Produces `defer { ... }` or `defer stmt;` — the deferred code is
     * formatted with always-brace semantics.
     *
     * @param stmt The defer statement node.
     * @return The formatted defer statement string.
     */
    std::string visitDeferStmt(const DeferStmt& stmt) override;

    /**
     * @brief Format an error placeholder statement (from error-tolerant parsing).
     *
     * Produces a minimal placeholder — the formatter assumes valid ASTs.
     * Error nodes indicate the parser could not recover for this statement,
     * so output will be incomplete.
     *
     * @param stmt The error statement node.
     * @return A placeholder string representing the unrecoverable parse error.
     */
    std::string visitErrorStmt(const ErrorStmt& stmt) override;

    // =====================================================================
    // ProgramVisitor<std::string> override
    // =====================================================================

    /**
     * @brief Format the top-level program: visits all statements in order.
     *
     * This is called by `format()` after resetting internal state.  It
     * delegates to `formatStatements()` for the actual formatting logic.
     *
     * @param program The root Program node containing top-level statements.
     * @return The formatted source code with a trailing newline.
     */
    std::string visitProgram(const Program& program) override;
};

}  // namespace vora
