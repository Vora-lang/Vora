/**
 * @file parser.h
 * @brief Recursive-descent + Pratt parser for the Vora scripting language.
 *
 * The Parser converts a flat token stream from the Lexer into an AST
 * (Program node containing a list of statements). It implements
 * error-tolerant parsing: on any syntax error the parser inserts
 * placeholder ErrorExpr / ErrorStmt nodes and synchronizes to the next
 * statement boundary, so callers (including the LSP server) always
 * receive a well-formed, structurally complete AST even for malformed
 * source code.
 *
 * Parsing strategy
 * ----------------
 * Statements are handled by recursive descent — the statement()
 * dispatcher routes by leading keyword (if, while, for, func, obj,
 * try, let, const, return, break, continue, throw, import, export,
 * defer) and falls back to expressionStatement() for everything else.
 *
 * Expressions use a Pratt (top-down operator precedence) parser.
 * parsePrecedence() is the core Pratt loop: it consumes a prefix
 * expression via primary() / call() and then enters a while loop
 * consuming infix operators whose precedence exceeds the current
 * minimum. Prefix and infix parselets are dispatched via a static
 * precedence table (getPrecedence()).
 *
 * Error tolerance
 * ---------------
 * The parser never returns nullptr. On error it:
 *   1. Calls error() to report the diagnostic.
 *   2. Calls synchronize() to skip tokens until the next statement
 *      boundary (tracking brace/paren/bracket depth to correctly
 *      cross nested delimiters).
 *   3. Returns an ErrorExpr or ErrorStmt placeholder so the
 *      surrounding AST node remains structurally valid.
 *
 * Statement-level recovery produces partial but meaningful nodes
 * (e.g., an IfStmt whose condition is an ErrorExpr, a FuncStmt
 * whose body is an ErrorStmt) rather than a bare ErrorStmt, so
 * LSP features like document symbols and folding ranges still work.
 *
 * Expression-level recovery inserts ErrorExpr placeholders for
 * missing operands instead of discarding valid sub-expressions.
 * Invalid assignment targets fall back to evaluating the RHS.
 *
 * @see Lexer (src/lexer/lexer.h) for tokenization.
 * @see Compiler (src/vm/compiler.h) for AST → bytecode translation.
 */

#pragma once

#include <vector>
#include <memory>

#include "../lexer/token.h"
#include "../ast/expr.h"
#include "../ast/stmt.h"
#include "../ast/program.h"
#include "../ast/binding_pattern.h"
#include "../common/error_reporter.h"

namespace vora {

/**
 * @class Parser
 * @brief Recursive-descent + Pratt parser that converts tokens into an AST.
 *
 * @details
 * The parser is the second stage of the Vora compilation pipeline
 * (Lexer → Parser → Compiler → VM). It consumes the token stream
 * produced by the Lexer and emits an AST rooted in a Program node.
 *
 * Error-tolerant design
 * ---------------------
 * Unlike a traditional parser that aborts on the first error, this
 * parser always returns a valid Program. On error, placeholder
 * ErrorExpr / ErrorStmt nodes are inserted and the parser resumes at
 * the next statement boundary. This guarantees that downstream tools
 * (formatter, LSP semantic analysis, compiler) always have a complete
 * AST to work with, even for code that is being actively edited.
 *
 * Callers check Parser::hasError() to decide whether to proceed with
 * compilation or to skip code generation and report diagnostics only.
 *
 * Usage
 * -----
 * @code
 * vora::Lexer lexer(source);
 * auto tokens = lexer.scanTokens();
 * vora::Parser parser(std::move(tokens), errorReporter);
 * parser.setSource(source);
 * auto program = parser.parse();
 * if (!parser.hasError()) {
 *     // compile and execute
 * }
 * @endcode
 */
class Parser {
public:
    /**
     * @brief Construct a Parser from a token stream and error reporter.
     *
     * @param tokens The token stream produced by the Lexer. Ownership is
     *               transferred into the parser (moved, not copied).
     * @param reporter Error reporter that collects diagnostics. The
     *                 reporter must outlive the Parser.
     */
    Parser(std::vector<Token> tokens, ErrorReporter& reporter);

    /**
     * @brief Parse the entire token stream and return a Program AST node.
     *
     * This is the main entry point. It repeatedly calls statement() until
     * EOF is reached, collecting all top-level statements into a Program.
     *
     * @return A non-null Program node. On error the returned Program
     *         contains ErrorStmt placeholders for malformed statements.
     *         Never returns nullptr.
     */
    std::unique_ptr<Program> parse();

    /**
     * @brief Check whether any syntax errors were encountered.
     *
     * Call this after parse() to decide whether the AST is safe to
     * compile and execute. Even when true, the returned Program is
     * structurally valid (it contains ErrorStmt nodes, not nullptr).
     *
     * @return true if at least one error was reported.
     */
    bool hasError() const { return reporter_.hadError(); }

    /**
     * @brief Set the source text for error display.
     *
     * Without this, parser error messages cannot show source-line
     * snippets (the caret underlining the error location will be
     * missing). Always call this before parse() if source text is
     * available.
     *
     * @param source The original source code string (used for
     *               diagnostic line/column display only).
     */
    void setSource(const std::string& source) { sourceText = source; }

private:
    /// The token stream to parse.
    std::vector<Token> tokens;

    /// Source text for error message context (optional but recommended).
    std::string sourceText;

    /// Reference to the diagnostic collector.
    ErrorReporter& reporter_;

    /// Index of the next token to be consumed (0-based into tokens).
    size_t current = 0;

private:
    // =================================================================
    //  Error handling
    // =================================================================

    /**
     * @brief Report a syntax error at the current token position.
     *
     * The error is forwarded to the ErrorReporter. Parsing continues;
     * the caller is responsible for inserting placeholder AST nodes.
     *
     * @param message Human-readable error description.
     */
    void error(const std::string& message);

    /**
     * @brief Report an error and return an ErrorStmt placeholder.
     *
     * Convenience combining error() + synchronize() + ErrorStmt creation.
     * Used by statement-level parse methods when they encounter malformed
     * input they cannot recover from locally.
     *
     * @param message Human-readable error description.
     * @return A placeholder ErrorStmt that compiles to a no-op.
     */
    std::unique_ptr<Stmt> errorStmt(const std::string& message);

    /**
     * @brief Report an error and return an ErrorExpr placeholder.
     *
     * Convenience combining error() + synchronize() + ErrorExpr creation.
     * Used by expression-level parse methods when they encounter malformed
     * input.
     *
     * @param message Human-readable error description.
     * @return A placeholder ErrorExpr that compiles to OP_NULL.
     */
    std::unique_ptr<Expr> errorExpr(const std::string& message);

    /**
     * @brief Skip tokens until the next statement boundary.
     *
     * Synchronization scans forward until it finds a token that is
     * likely to start a new statement (after a semicolon, newline at
     * statement level, or a leading keyword like if/while/for/func/obj
     * /try/let/const/return/break/continue/throw/import/export/defer).
     *
     * Brace, paren, and bracket depth is tracked so that the scanner
     * correctly crosses nested blocks without stopping prematurely.
     * This prevents synchronization from landing inside a nested
     * expression and discarding valid surrounding tokens.
     *
     * After calling synchronize(), advance() will return the first
     * token of the next recoverable statement.
     */
    void synchronize();

    // =================================================================
    //  Expression parsing
    // =================================================================

    /**
     * @brief Parse a complete expression.
     *
     * Entry point for expression parsing. Delegates to parsePrecedence(0)
     * (the lowest precedence level) so that the entire expression is
     * consumed.
     *
     * @return The parsed expression node. ErrorExpr on failure.
     */
    std::unique_ptr<Expr> expression();

    /**
     * @brief Core Pratt parser loop — parse an expression with a given
     *        minimum precedence.
     *
     * First, a prefix (nud) parselet is consumed: parsePrecedence()
     * dispatches based on the current token type — number/string/bool
     * /null/this are handled as literals, identifiers trigger call()
     * (which handles variables, function calls, property access, and
     * indexing), and prefix operators (unary -, !, ~, ++, --) recurse
     * with the operator's precedence.
     *
     * Then, while the next token is an infix (led) operator with
     * precedence >= the minimum, the loop consumes the operator and
     * its right operand, building left-associative or right-associative
     * trees as dictated by the precedence table.
     *
     * Assignment (=) and compound assignment (+=, -=, *=, /=, %=,
     * **=, ^=, |=, &=) are desugared inside the Pratt loop. The LHS
     * is verified to be a valid l-value (VariableExpr, PropertyExpr,
     * or IndexExpr); otherwise an error is reported and the RHS value
     * is returned as a fallback.
     *
     * @param precedence Minimum precedence level for infix operators.
     *                   Top-level callers pass 0 (ASSIGNMENT).
     * @return The parsed expression node.
     */
    std::unique_ptr<Expr> parsePrecedence(int precedence);

    /**
     * @brief Parse a primary expression (lowest-level parselet).
     *
     * Handles literal tokens (number, string, bool, null, this),
     * grouping parentheses (which recurse via expression()), array
     * literals [...], dict (map) literals {key: value, ...}, function
     * expressions (via funcExpression()), match expressions (via
     * matchExpression()), string interpolation parsing, and
     * yield expressions.
     *
     * If the current token does not match any primary expression form,
     * an error is reported and an ErrorExpr is returned.
     *
     * @return The parsed primary expression.
     */
    std::unique_ptr<Expr> primary();

    /**
     * @brief Parse a call expression (identifiers, function calls,
     *        property access, indexing).
     *
     * Handles the chain of post-primary operations:
     *   - Simple variable references.
     *   - Function calls: callee(args).
     *   - Property access: expr.ident (dot notation).
     *   - Index access: expr[index] (bracket notation).
     *   - Optional chaining: expr?.ident, expr?.[index], expr?.(args).
     *   - Postfix ++ and --.
     *
     * These are parsed as a tight loop: after consuming the initial
     * identifier or expression, call() repeatedly matches call/index/
     * dot/postfix tokens to build a left-associated chain.
     *
     * @return The parsed call chain expression.
     */
    std::unique_ptr<Expr> call();

    /**
     * @brief Parse a function expression literal.
     *
     * Parses the form: func(params) { body }.
     * Called from primary() when the current token is KW_FUNC and the
     * next token is '(' (distinguishing func expressions from func
     * statements).
     *
     * @return A FuncExpr AST node, or ErrorExpr on parse failure.
     */
    std::unique_ptr<Expr> funcExpression();

    /**
     * @brief Parse a yield expression.
     *
     * Parses yield <expr> (expression form) or yield (bare form which
     * yields null). Called from primary() when the token is KW_YIELD.
     *
     * @return A YieldExpr AST node.
     */
    std::unique_ptr<Expr> yieldExpression();

    /**
     * @brief Parse a match expression.
     *
     * Parses the form: match (expr) { pattern1 => result1, pattern2 =>
     * result2, ... }. Patterns can be literal values, variable bindings,
     * or wildcards (_).
     *
     * @return A MatchExpr AST node, or ErrorExpr on parse failure.
     */
    std::unique_ptr<Expr> matchExpression();

    /**
     * @brief Parse a single match arm pattern.
     *
     * Used internally by matchExpression() to parse each case before
     * the => arrow.
     *
     * @return A MatchPattern describing the pattern to match against.
     */
    MatchPattern parseMatchPattern();

    /**
     * @brief Parse the argument list and closing parenthesis of a call.
     *
     * Consumes '(' arguments ')' after the callee has already been
     * parsed. Handles zero-argument calls, comma-separated argument
     * lists, and trailing commas.
     *
     * @param callee The expression being called (the receiver).
     * @return A CallExpr with the given callee and parsed arguments.
     */
    std::unique_ptr<Expr> finishCall(
        std::unique_ptr<Expr> callee
    );

    // =================================================================
    //  Statement parsing
    // =================================================================

    /**
     * @brief Parse a single statement.
     *
     * The top-level statement dispatcher. Routes by leading keyword:
     *   - let / const  → letStatement() / constStatement()
     *   - if           → ifStatement()
     *   - while        → whileStatement()
     *   - do           → doWhileStatement()
     *   - for          → forStatement()
     *   - func         → funcStatement()
     *   - obj          → objStatement()
     *   - return       → returnStatement()
     *   - break        → breakStatement()
     *   - continue     → continueStatement()
     *   - throw        → throwStatement()
     *   - try          → tryStatement()
     *   - import       → importStatement() or fromImportStatement()
     *   - export       → exportStatement()
     *   - defer        → deferStatement()
     *   - {            → blockStatement() (block as statement)
     *
     * Any other token is treated as the start of an expression
     * statement.
     *
     * @return The parsed statement node. ErrorStmt on failure.
     */
    std::unique_ptr<Stmt> statement();

    /**
     * @brief Parse a let variable declaration statement.
     *
     * Parses: let name = expr; or let name;
     * Also handles destructuring: let [a, b] = arr; / let {x, y} = obj;
     *
     * @return A LetStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> letStatement();

    /**
     * @brief Parse a const variable declaration statement.
     *
     * Same syntax as let but the binding is immutable.
     *
     * @return A ConstStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> constStatement();

    /**
     * @brief Parse a binding declaration (shared by let and const).
     *
     * Handles both simple bindings (let x = 5) and destructuring
     * bindings (let [a, b] = arr; let {x, y} = obj;).
     *
     * @param isConst If true, the binding is immutable (const).
     * @return A LetStmt or ConstStmt AST node.
     */
    std::unique_ptr<Stmt> bindingDeclaration(bool isConst);

    /**
     * @brief Parse a destructuring binding pattern.
     *
     * Dispatches to parseArrayBinding() for [...] patterns or
     * parseObjectBinding() for {...} patterns.
     *
     * @return The parsed binding pattern tree.
     */
    std::unique_ptr<BindingPattern> parseBindingPattern();

    /**
     * @brief Parse an array destructuring pattern: [a, b, ...rest].
     *
     * @return An array binding pattern node.
     */
    std::unique_ptr<BindingPattern> parseArrayBinding();

    /**
     * @brief Parse an object destructuring pattern: {x, y: alias, ...rest}.
     *
     * @return An object binding pattern node.
     */
    std::unique_ptr<BindingPattern> parseObjectBinding();

    /**
     * @brief Convert an array expression to a binding pattern.
     *
     * Used when destructuring assignment is written without a declaration
     * keyword, e.g.: [a, b] = arr; — the LHS is initially parsed as an
     * ArrayExpr and then converted to a binding pattern.
     *
     * @param expr The already-parsed ArrayExpr.
     * @return The corresponding ArrayBindingPattern.
     */
    std::unique_ptr<BindingPattern> convertArrayExprToBinding(const ArrayExpr& expr);

    /**
     * @brief Convert a dict expression to a binding pattern.
     *
     * Used when destructuring assignment is written without a declaration
     * keyword, e.g.: {x, y} = obj; — the LHS is initially parsed as a
     * DictExpr and then converted to a binding pattern.
     *
     * @param expr The already-parsed DictExpr.
     * @return The corresponding ObjectBindingPattern.
     */
    std::unique_ptr<BindingPattern> convertDictExprToBinding(const DictExpr& expr);

    /**
     * @brief Parse a block statement: { statements... }.
     *
     * Consumes the opening brace, repeatedly calls statement() until
     * the closing brace or EOF, and returns a BlockStmt containing
     * the collected statements.
     *
     * @return A BlockStmt node (may be empty if no statements inside).
     */
    std::unique_ptr<BlockStmt> blockStatement();

    /**
     * @brief Parse a return statement.
     *
     * Parses: return expr; or return; (returns null).
     *
     * @return A ReturnStmt AST node.
     */
    std::unique_ptr<Stmt> returnStatement();

    /**
     * @brief Parse an if statement (with optional else/elif chains).
     *
     * Parses: if (condition) body [else if (cond) body]* [else body].
     * elif is sugar desugared into nested if/else during parsing.
     *
     * @return An IfStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> ifStatement();

    /**
     * @brief Parse a while loop statement.
     *
     * Parses: while (condition) body.
     *
     * @return A WhileStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> whileStatement();

    /**
     * @brief Parse a do-while loop statement.
     *
     * Parses: do body while (condition);
     *
     * @return A DoWhileStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> doWhileStatement();

    /**
     * @brief Parse a for or for-in loop statement.
     *
     * Parses:
     *   - C-style for: for (init; cond; incr) body
     *   - For-in: for (var in iterable) body
     *
     * @return A ForStmt or ForInStmt AST node, or ErrorStmt on failure.
     */
    std::unique_ptr<Stmt> forStatement();

    /**
     * @brief Parse a C-style for loop: for (init; cond; incr) body.
     *
     * Called by forStatement() when the header contains semicolons
     * rather than the 'in' keyword.
     *
     * @param forToken The KW_FOR token (for source location).
     * @return A ForStmt AST node.
     */
    std::unique_ptr<Stmt> cForStatement(Token forToken);

    /**
     * @brief Parse a function declaration statement.
     *
     * Parses: func name(params) { body }.
     * Also handles destructured parameters in the parameter list.
     *
     * @return A FuncStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> funcStatement();

    /**
     * @brief Parse an object/class declaration statement.
     *
     * Parses: obj Name [: Parent] { constructor() { ... } method() { ... } }.
     * Method bodies are FunctionExpressions; the constructor body is a
     * block of statements collected directly.
     *
     * @return An ObjStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> objStatement();

    /**
     * @brief Parse a break statement.
     *
     * Parses: break;
     *
     * @return A BreakStmt AST node.
     */
    std::unique_ptr<Stmt> breakStatement();

    /**
     * @brief Parse a continue statement.
     *
     * Parses: continue;
     *
     * @return A ContinueStmt AST node.
     */
    std::unique_ptr<Stmt> continueStatement();

    /**
     * @brief Parse a try-catch-finally statement.
     *
     * Parses: try { body } catch (var) { handler } [finally { cleanup }].
     * At least one of catch or finally must be present.
     *
     * @return A TryStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> tryStatement();

    /**
     * @brief Parse a throw statement.
     *
     * Parses: throw expr;
     *
     * @return A ThrowStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> throwStatement();

    /**
     * @brief Parse an import statement.
     *
     * Parses: import "module"; — imports all exports from a module.
     *
     * @return An ImportStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> importStatement();

    /**
     * @brief Parse a from-import statement.
     *
     * Parses: import { name1, name2 } from "module";
     *
     * @return An ImportStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> fromImportStatement();

    /**
     * @brief Parse an export statement.
     *
     * Parses: export { name1, name2 }; or export statement (exporting
     * a declaration directly).
     *
     * @return An ExportStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> exportStatement();

    /**
     * @brief Parse a defer statement.
     *
     * Parses: defer statement; — schedules the statement to execute
     * when the current scope exits (like Go's defer).
     *
     * @return A DeferStmt AST node, or ErrorStmt on parse failure.
     */
    std::unique_ptr<Stmt> deferStatement();

private:
    // =================================================================
    //  Token stream helpers
    // =================================================================

    /**
     * @brief Check if the parser has reached the end of the token stream.
     *
     * @return true if the current token is TOKEN_EOF.
     */
    bool isAtEnd() const;

    /**
     * @brief Consume and return the current token, advancing to the next.
     *
     * @return The token that was just consumed.
     */
    Token advance();

    /**
     * @brief Return the current token without consuming it.
     *
     * @return A const reference to the current lookahead token.
     */
    Token peek() const;

    /**
     * @brief Return the most recently consumed token.
     *
     * @return The token that was returned by the last advance() call.
     */
    Token previous() const;

    /**
     * @brief Conditionally consume the current token if it matches the
     *        given type.
     *
     * If the current token's type equals @p type, it is consumed
     * (advance() is called) and the method returns true. Otherwise
     * the token is left untouched and false is returned.
     *
     * @param type The token type to match.
     * @return true if the token was matched and consumed.
     */
    bool match(TokenType type);

    /**
     * @brief Check if the current token has the given type without
     *        consuming it.
     *
     * @param type The token type to check.
     * @return true if the current token's type equals @p type.
     */
    bool check(TokenType type) const;

    /**
     * @brief Return the Pratt precedence level for a token type.
     *
     * Used by parsePrecedence() to decide whether an infix operator
     * should be consumed. Higher values bind tighter.
     *
     * Precedence levels (lowest to highest):
     *   - ASSIGNMENT (=, +=, -=, etc.)
     *   - COMMA (,)
     *   - CONDITIONAL (?:)
     *   - LOGICAL_OR (||)
     *   - LOGICAL_AND (&&)
     *   - BITWISE_OR (|)
     *   - BITWISE_XOR (^)
     *   - BITWISE_AND (&)
     *   - EQUALITY (==, !=)
     *   - COMPARISON (<, >, <=, >=)
     *   - TERM (+, -)
     *   - FACTOR (*, /, %)
     *   - POWER (**)
     *   - UNARY (!, -, ~, ++, --)
     *   - CALL (., (), [])
     *   - PRIMARY
     *
     * @param type The token type to look up.
     * @return The precedence level (0 if the token is not an operator).
     */
    int getPrecedence(TokenType type) const;
};

}
