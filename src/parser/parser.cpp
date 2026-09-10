#include "parser.h"

#include <cctype>
#include <stdexcept>

#include "../ast/stmt.h"
#include "../gc/gc_heap.h"

namespace vora {

Parser::Parser(std::vector<Token> tokens, ErrorReporter& reporter)
    : tokens(std::move(tokens)), reporter_(reporter) {
}

std::unique_ptr<Program> Parser::parse() {

    std::vector<std::unique_ptr<Stmt>> statements;

    while (!isAtEnd()) {

        auto stmt = statement();

        if (!stmt) {
            // Safety net: if a statement method returns nullptr despite error
            // recovery, synchronize and emit an ErrorStmt to preserve AST coverage.
            synchronize();
            stmt = std::make_unique<ErrorStmt>("Failed to parse statement", previous());
        }

        // Skip ExprStmt wrappers whose inner expression failed to parse.
        // ErrorExpr nodes are kept for LSP coverage; bare nullptr expressions
        // (from unrecoverable paths) are replaced with ErrorExpr.
        if (auto* es = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (!es->expression) {
                es->expression = std::make_unique<ErrorExpr>("Failed to parse expression", previous());
            }
        }

        statements.push_back(std::move(stmt));
    }

    // Always return a Program — even with errors, the partial AST is
    // useful for LSP diagnostics, completion, and go-to-definition.
    // Callers check Parser::hasError() to decide whether to execute.
    return std::make_unique<Program>(std::move(statements));
}

void Parser::error(const std::string& message) {
    reporter_.error(peek().line, peek().column,
                    static_cast<int>(peek().lexeme.size()),
                    message);
}

void Parser::synchronize() {
    // Guard: if called with no tokens consumed, previous() would OOB.
    if (current == 0) return;

    // Track delimiter nesting depth so we can synchronize to the matching
    // close-brace rather than stopping at the first one we encounter.
    // This prevents cascading parse errors when recovering inside nested blocks.
    //
    // Counters start at 0 and may temporarily go negative when recovery begins
    // inside a nested block (more RIGHT_* than LEFT_* before the recovery point).
    // The <= 0 checks below correctly handle this: a negative depth means we're
    // already at or above the enclosing level, which is a safe exit point.
    int braceDepth = 0;
    int parenDepth = 0;
    int bracketDepth = 0;

    // Skip tokens until we hit a likely statement boundary.
    while (!isAtEnd()) {
        // Track delimiter depth as we skip.
        TokenType prevType = previous().type;
        if (prevType == TokenType::LEFT_BRACE) braceDepth++;
        if (prevType == TokenType::RIGHT_BRACE) braceDepth--;
        if (prevType == TokenType::LEFT_PAREN) parenDepth++;
        if (prevType == TokenType::RIGHT_PAREN) parenDepth--;
        if (prevType == TokenType::LEFT_BRACKET) bracketDepth++;
        if (prevType == TokenType::RIGHT_BRACKET) bracketDepth--;

        // If we just passed a semicolon at depth 0, we're at a clean boundary.
        if (prevType == TokenType::SEMICOLON &&
            braceDepth <= 0 && parenDepth <= 0 && bracketDepth <= 0) {
            return;
        }

        // A closing brace at depth 0 means we've exited the current block
        // and are back at the enclosing level — a safe resync point.
        if (prevType == TokenType::RIGHT_BRACE &&
            braceDepth <= 0 && parenDepth <= 0 && bracketDepth <= 0) {
            return;
        }

        // Statement-start keywords are safe resync points (at depth 0).
        if (braceDepth <= 0 && parenDepth <= 0 && bracketDepth <= 0) {
            switch (peek().type) {
                case TokenType::LET:
                case TokenType::CONST:
                case TokenType::FUNC:
                case TokenType::IF:
                case TokenType::WHILE:
                case TokenType::FOR:
                case TokenType::RETURN:
                case TokenType::OBJ:
                case TokenType::BREAK:
                case TokenType::CONTINUE:
                case TokenType::TRY:
                case TokenType::IMPORT:
                case TokenType::EXPORT:
                case TokenType::THROW:
                case TokenType::YIELD:
                case TokenType::DEFER:
                case TokenType::DO:
                    return;
                default:
                    break;
            }
        }

        advance();
    }
}

std::unique_ptr<Stmt> Parser::errorStmt(const std::string& message) {
    error(message);
    synchronize();
    return std::make_unique<ErrorStmt>(message, previous());
}

std::unique_ptr<Expr> Parser::errorExpr(const std::string& message) {
    error(message);
    // Skip the bad token so we don't loop on it.
    if (!isAtEnd()) advance();
    return std::make_unique<ErrorExpr>(message, previous());
}

// =========================
// ENTRY
// =========================

std::unique_ptr<Expr> Parser::expression() {
    // Enable Go-style ASI for this statement-level expression: a newline
    // between an atom and the next token (when not inside () / [])
    // terminates the expression as if a ';' were there.
    //
    // Recursive calls to parsePrecedence for infix / ternary right
    // operands reuse this flag, which is intentional — ASI fires
    // anywhere within the expression as long as we're at depth 0 of
    // parens. The flag is restored on exit so nested expression() calls
    // (rare) start from the outer context's state.
    size_t prevAsiDepth = asiDepth_;
    asiDepth_ = prevAsiDepth + 1;
    auto e = parsePrecedence(0);
    asiDepth_ = prevAsiDepth;
    return e;
}

std::unique_ptr<Stmt> Parser::statement() {

    // Labeled loop: `name: for (...) { ... }` / `while` / `do`.
    //
    // `IDENT COLON` is not valid at the start of a statement otherwise (it
    // fails as "Unexpected token: :"), so two-token lookahead is unambiguous.
    // Labels only attach to loops: with no `goto`, a label on anything else
    // could never be the target of a jump, so it is rejected rather than
    // accepted-and-ignored.
    if (check(TokenType::IDENTIFIER) && peekNext().type == TokenType::COLON) {
        Token labelTok = advance();   // the label identifier
        advance();                    // ':'

        if (!(check(TokenType::WHILE) || check(TokenType::DO) ||
              check(TokenType::FOR))) {
            error("labeled statement must be a loop (while / do-while / for)");
        }

        auto inner = statement();
        if (inner) {
            // Attach the label to whichever loop kind was parsed.
            if (auto* w = dynamic_cast<WhileStmt*>(inner.get())) {
                w->label = labelTok.lexeme;
            } else if (auto* d = dynamic_cast<DoWhileStmt*>(inner.get())) {
                d->label = labelTok.lexeme;
            } else if (auto* f = dynamic_cast<ForStmt*>(inner.get())) {
                f->label = labelTok.lexeme;
            } else if (auto* c = dynamic_cast<CForStmt*>(inner.get())) {
                c->label = labelTok.lexeme;
            }
            return inner;
        }
        return errorStmt("Expected a loop after label");
    }

    if (match(TokenType::LET)) {
        return letStatement();
    }

    if (match(TokenType::CONST)) {
        return constStatement();
    }

    if (match(TokenType::LEFT_BRACE)) {
        return blockStatement();
    }

    if (match(TokenType::RETURN)) {
        return returnStatement();
    }

    if (match(TokenType::IF)) {
        return ifStatement();
    }

    if (match(TokenType::WHILE)) {
        return whileStatement();
    }

    if (match(TokenType::DO)) {
        return doWhileStatement();
    }

    if (match(TokenType::FOR)) {
        return forStatement();
    }

    bool isAsync = match(TokenType::ASYNC);
    if (isAsync) {
        // Must be followed by 'func'
        if (!match(TokenType::FUNC)) {
            return errorStmt("Expected 'func' after 'async'");
        }
    }
    if (isAsync || match(TokenType::FUNC)) {
        // Anonymous function expression: func(x) { ... } or async func(x) { ... }
        if (check(TokenType::LEFT_PAREN)) {
            auto expr = funcExpression(isAsync);
            if (!expr) {
                return errorStmt("Failed to parse anonymous function expression");
            }
            match(TokenType::SEMICOLON);
            return std::make_unique<ExprStmt>(std::move(expr));
        }
        return funcStatement(isAsync);
    }

    if (match(TokenType::OBJ)) {
        return objStatement();
    }

    if (match(TokenType::BREAK)) {
        return breakStatement();
    }

    if (match(TokenType::CONTINUE)) {
        return continueStatement();
    }

    if (match(TokenType::TRY)) {
        return tryStatement();
    }

    if (match(TokenType::THROW)) {
        return throwStatement();
    }

    if (match(TokenType::IMPORT)) {
        return importStatement();
    }

    if (match(TokenType::FROM)) {
        return fromImportStatement();
    }

    if (match(TokenType::EXPORT)) {
        return exportStatement();
    }

    if (match(TokenType::DEFER)) {
        return deferStatement();
    }

    auto expr = expression();

    match(TokenType::SEMICOLON);

    return std::make_unique<ExprStmt>(
        std::move(expr)
    );
}

std::unique_ptr<Stmt> Parser::letStatement() {

    // Destructuring: let [a, b] = ...  or  let {x, y} = ...
    if (check(TokenType::LEFT_BRACKET) || check(TokenType::LEFT_BRACE)) {
        return bindingDeclaration(/*isConst=*/false);
    }

    if (!match(TokenType::IDENTIFIER)) {
        return errorStmt("Expected variable name");
    }

    std::string name = previous().lexeme;
    Token nameToken = previous();  // save for LSP position info

    // Optional type annotation: let x:int = ...
    std::string typeAnnotation;
    if (match(TokenType::COLON)) {
        if (!match(TokenType::IDENTIFIER)) {
            error("Expected type name after ':'");
            // Continue — still try to parse the initializer
        } else {
            typeAnnotation = previous().lexeme;
        }
    }

    // `let x` without an initializer is allowed — the binding defaults to
    // null, so `let x; if (c) { x = 1 }` works (syntax-review #2.7).
    if (!match(TokenType::EQUAL)) {
        match(TokenType::SEMICOLON);
        return std::make_unique<LetStmt>(
            name,
            nameToken,
            std::make_unique<LiteralExpr>(nullptr),
            typeAnnotation
        );
    }

    auto initializer = expression();
    if (!initializer) {
        initializer = std::make_unique<ErrorExpr>("Expected initializer expression", peek());
    }

    match(TokenType::SEMICOLON);

    return std::make_unique<LetStmt>(
        name,
        nameToken,
        std::move(initializer),
        typeAnnotation
    );
}

std::unique_ptr<Stmt> Parser::constStatement() {

    // Destructuring: const [a, b] = ...  or  const {x, y} = ...
    if (check(TokenType::LEFT_BRACKET) || check(TokenType::LEFT_BRACE)) {
        return bindingDeclaration(/*isConst=*/true);
    }

    if (!match(TokenType::IDENTIFIER)) {
        return errorStmt("Expected variable name");
    }

    std::string name = previous().lexeme;
    Token nameToken = previous();  // save for LSP position info

    // Optional type annotation: const x:int = ...
    std::string typeAnnotation;
    if (match(TokenType::COLON)) {
        if (!match(TokenType::IDENTIFIER)) {
            error("Expected type name after ':'");
            // Continue anyway
        } else {
            typeAnnotation = previous().lexeme;
        }
    }

    if (!match(TokenType::EQUAL)) {
        error("Expected '=' after const variable name");
        // Return partial LetStmt — the variable name is still useful for LSP.
        match(TokenType::SEMICOLON);
        return std::make_unique<LetStmt>(
            name,
            nameToken,
            std::make_unique<ErrorExpr>("Missing initializer", previous()),
            typeAnnotation,
            true  // isConst
        );
    }

    auto initializer = expression();
    if (!initializer) {
        initializer = std::make_unique<ErrorExpr>("Expected initializer expression", peek());
    }

    match(TokenType::SEMICOLON);

    return std::make_unique<LetStmt>(
        name,
        nameToken,
        std::move(initializer),
        typeAnnotation,
        true  // isConst
    );
}

// =========================================================================
// Destructuring support
// =========================================================================

// bindingDeclaration — parse `let/const <pattern> = <expr>`
std::unique_ptr<Stmt> Parser::bindingDeclaration(bool isConst) {
    auto binding = parseBindingPattern();

    // Optional type annotation
    std::string typeAnnotation;
    if (match(TokenType::COLON)) {
        if (match(TokenType::IDENTIFIER)) {
            typeAnnotation = previous().lexeme;
        } else {
            error("Expected type name after ':'");
        }
    }

    if (!match(TokenType::EQUAL)) {
        error("Expected '=' in destructuring declaration");
        match(TokenType::SEMICOLON);
        return std::make_unique<LetStmt>(
            "", binding->startToken,
            std::make_unique<ErrorExpr>("Missing initializer", peek()),
            typeAnnotation, isConst, std::move(binding));
    }

    auto initializer = expression();
    if (!initializer) {
        initializer = std::make_unique<ErrorExpr>("Expected initializer expression", peek());
    }

    match(TokenType::SEMICOLON);

    return std::make_unique<LetStmt>(
        "", binding->startToken,
        std::move(initializer),
        typeAnnotation, isConst, std::move(binding));
}

// parseBindingPattern — dispatch on first token: [ → array, { → object, else identifier
std::unique_ptr<BindingPattern> Parser::parseBindingPattern() {
    if (match(TokenType::LEFT_BRACKET)) {
        return parseArrayBinding();
    }
    if (match(TokenType::LEFT_BRACE)) {
        return parseObjectBinding();
    }
    // Fallback: simple identifier (used for nested patterns and rest args)
    if (match(TokenType::IDENTIFIER)) {
        std::string name = previous().lexeme;
        Token nameToken = previous();
        std::unique_ptr<Expr> defaultValue;
        if (match(TokenType::EQUAL)) {
            defaultValue = expression();
        }
        return std::make_unique<IdentifierBinding>(
            std::move(name), std::move(nameToken), std::move(defaultValue));
    }
    error("Expected binding pattern");
    return std::make_unique<IdentifierBinding>("?error?", peek());
}

// parseArrayBinding — parse `[elem1, elem2, ...rest]`
std::unique_ptr<BindingPattern> Parser::parseArrayBinding() {
    Token leftBracket = previous();
    std::vector<std::unique_ptr<BindingPattern>> elements;
    std::unique_ptr<BindingPattern> rest;

    if (!check(TokenType::RIGHT_BRACKET)) {
        do {
            if (match(TokenType::DOT_DOT_DOT)) {
                rest = parseBindingPattern();
                break;  // rest is always last
            }
            auto elem = parseBindingPattern();
            elements.push_back(std::move(elem));
        } while (match(TokenType::COMMA));
    }

    if (!match(TokenType::RIGHT_BRACKET)) {
        error("Expected ']' after binding pattern");
    }

    return std::make_unique<ArrayBinding>(
        std::move(elements), std::move(rest), std::move(leftBracket));
}

// parseObjectBinding — parse `{key1: pat1, shorth, ...rest}`
std::unique_ptr<BindingPattern> Parser::parseObjectBinding() {
    Token leftBrace = previous();
    std::vector<ObjectBinding::Property> properties;
    std::unique_ptr<BindingPattern> rest;

    if (!check(TokenType::RIGHT_BRACE)) {
        do {
            if (match(TokenType::DOT_DOT_DOT)) {
                rest = parseBindingPattern();
                break;  // rest is always last
            }

            if (!match(TokenType::IDENTIFIER)) {
                error("Expected property name in destructuring pattern");
                break;
            }
            std::string propName = previous().lexeme;

            if (match(TokenType::COLON)) {
                // Named: {key: binding} — binding can be another pattern
                auto nestedBinding = parseBindingPattern();
                properties.push_back({std::move(propName), std::move(nestedBinding), false});
            } else {
                // Shorthand: {x} means {x: x}
                Token nameToken = previous();
                std::unique_ptr<Expr> defaultValue;
                if (match(TokenType::EQUAL)) {
                    defaultValue = expression();
                }
                auto idBinding = std::make_unique<IdentifierBinding>(
                    propName, std::move(nameToken), std::move(defaultValue));
                properties.push_back({std::move(propName), std::move(idBinding), true});
            }
        } while (match(TokenType::COMMA));
    }

    if (!match(TokenType::RIGHT_BRACE)) {
        error("Expected '}' after binding pattern");
    }

    return std::make_unique<ObjectBinding>(
        std::move(properties), std::move(rest), std::move(leftBrace));
}

// convertArrayExprToBinding — walk ArrayExpr AST, extract binding names
std::unique_ptr<BindingPattern> Parser::convertArrayExprToBinding(const ArrayExpr& expr) {
    std::vector<std::unique_ptr<BindingPattern>> elements;
    std::unique_ptr<BindingPattern> rest;

    for (size_t i = 0; i < expr.elements.size(); i++) {
        const auto& elem = expr.elements[i];

        // Check for spread operator in the converted expression
        // (the original [a, ...rest] was parsed as array literal elements
        //  but the parser's primary() would parse ... as an error — so for now
        //  we only handle simple conversions)

        if (auto* var = dynamic_cast<const VariableExpr*>(elem.get())) {
            elements.push_back(std::make_unique<IdentifierBinding>(
                var->name, var->nameToken));
        } else if (auto* arr = dynamic_cast<const ArrayExpr*>(elem.get())) {
            elements.push_back(convertArrayExprToBinding(*arr));
        } else if (auto* dict = dynamic_cast<const DictExpr*>(elem.get())) {
            elements.push_back(convertDictExprToBinding(*dict));
        } else if (auto* error = dynamic_cast<const ErrorExpr*>(elem.get())) {
            // Skip error elements — they're placeholders from error-tolerant parsing
        } else {
            // Invalid: non-variable expression in destructuring pattern
            return nullptr;
        }
    }

    return std::make_unique<ArrayBinding>(
        std::move(elements), std::move(rest), expr.leftBracket);
}

// convertDictExprToBinding — walk DictExpr AST, extract binding names
std::unique_ptr<BindingPattern> Parser::convertDictExprToBinding(const DictExpr& expr) {
    std::vector<ObjectBinding::Property> properties;

    for (const auto& [key, value] : expr.pairs) {
        // The key should be a string literal (auto-converted from identifier in dict parsing)
        std::string propName;
        if (auto* lit = dynamic_cast<const LiteralExpr*>(key.get())) {
            if (lit->value.isGcString()) {
                propName = lit->value.asGcString()->value;
            } else {
                return nullptr;  // non-string key in pattern — invalid
            }
        } else {
            return nullptr;  // computed key in pattern — invalid
        }

        if (auto* var = dynamic_cast<const VariableExpr*>(value.get())) {
            // Shorthand-like: {key: var} — bind to var
            properties.push_back({std::move(propName),
                std::make_unique<IdentifierBinding>(var->name, var->nameToken), false});
        } else if (auto* arr = dynamic_cast<const ArrayExpr*>(value.get())) {
            properties.push_back({std::move(propName), convertArrayExprToBinding(*arr), false});
        } else if (auto* dict = dynamic_cast<const DictExpr*>(value.get())) {
            properties.push_back({std::move(propName), convertDictExprToBinding(*dict), false});
        } else {
            return nullptr;  // invalid value in destructuring pattern
        }
    }

    return std::make_unique<ObjectBinding>(
        std::move(properties), nullptr, expr.leftBrace);
}

std::unique_ptr<BlockStmt> Parser::blockStatement() {

    std::vector<std::unique_ptr<Stmt>> statements;

    while (!isAtEnd() &&
           peek().type != TokenType::RIGHT_BRACE) {

        auto stmt = statement();

        if (!stmt) {
            // Inner statement failed — emit an ErrorStmt and keep going.
            stmt = std::make_unique<ErrorStmt>("Failed to parse statement in block", peek());
        }

        statements.push_back(
            std::move(stmt)
        );
    }

    if (!match(TokenType::RIGHT_BRACE)) {

                    error("Expected '}' after block\n");

        // Return partial block anyway — the caller can still use what
        // was parsed before the error.
        return std::make_unique<BlockStmt>(std::move(statements));
    }

    return std::make_unique<BlockStmt>(
        std::move(statements)
    );
}

std::unique_ptr<Stmt> Parser::returnStatement() {

    // Allow bare `return` / `return;` (void return) — these are
    // common in early-return guards and void functions.
    // If the next token cannot start an expression we emit OP_NULL.
    std::unique_ptr<Expr> value;
    if (!check(TokenType::SEMICOLON) &&
        !check(TokenType::RIGHT_BRACE) &&
        !check(TokenType::END_OF_FILE)) {
        value = expression();
    }

    match(TokenType::SEMICOLON);

    return std::make_unique<ReturnStmt>(
        std::move(value)
    );
}

std::unique_ptr<Stmt> Parser::ifStatement() {

    if (!match(TokenType::LEFT_PAREN)) {
        error("Expected '(' after if");
        // Continue anyway — try to parse condition without '('
    }

    auto condition = expression();
    if (!condition) {
        condition = std::make_unique<ErrorExpr>("Expected condition after if", peek());
    }

    if (!match(TokenType::RIGHT_PAREN)) {
        error("Expected ')' after condition");
        // Continue — thenBranch is still parsable
    }

    auto thenBranch = statement();
    if (!thenBranch) {
        thenBranch = std::make_unique<ErrorStmt>("Expected body for if statement", peek());
    }

    std::unique_ptr<Stmt> elseBranch;

    if (match(TokenType::ELSE)) {
        elseBranch = statement();
        if (!elseBranch) {
            elseBranch = std::make_unique<ErrorStmt>("Expected body for else clause", peek());
        }
    }

    return std::make_unique<IfStmt>(
        std::move(condition),
        std::move(thenBranch),
        std::move(elseBranch)
    );
}

std::unique_ptr<BindingPattern> Parser::parseForBindingPattern() {
    if (check(TokenType::LEFT_BRACKET) || check(TokenType::LEFT_BRACE)) {
        return parseBindingPattern();
    }

    if (!match(TokenType::IDENTIFIER)) {
        error("Expected loop variable name after 'for'");
        return std::make_unique<IdentifierBinding>("?error?", peek());
    }

    Token firstName = previous();
    std::vector<std::unique_ptr<BindingPattern>> elements;
    elements.push_back(std::make_unique<IdentifierBinding>(
        previous().lexeme,
        previous()
    ));

    while (match(TokenType::COMMA)) {
        if (!match(TokenType::IDENTIFIER)) {
            error("Expected identifier after ',' in for-in loop variable list");
            break;
        }
        elements.push_back(std::make_unique<IdentifierBinding>(
            previous().lexeme,
            previous()
        ));
    }

    if (elements.size() == 1) {
        return std::move(elements[0]);
    }

    return std::make_unique<ArrayBinding>(
        std::move(elements),
        nullptr,
        firstName
    );
}

std::unique_ptr<Stmt> Parser::forStatement() {

    Token forToken = previous();

    // C-style for: for (initializer; condition; increment) { body }
    if (match(TokenType::LEFT_PAREN)) {
        return cForStatement(forToken);
    }

    // For-in: for <pattern> in iterable { body }
    auto variablePattern = parseForBindingPattern();

    if (!match(TokenType::IN)) {
        error("Expected 'in' after loop variable");
        // Continue — try to parse the iterable anyway
    }

    auto iterable = expression();
    if (!iterable) {
        iterable = std::make_unique<ErrorExpr>("Expected iterable after 'in'", peek());
    }

    std::unique_ptr<BlockStmt> body;
    if (!match(TokenType::LEFT_BRACE)) {
        error("Expected '{' before for loop body");
        // Create an empty block so the partial ForStmt is still valid.
        body = std::make_unique<BlockStmt>(std::vector<std::unique_ptr<Stmt>>{});
    } else {
        body = blockStatement();
    }

    if (!body) {
        body = std::make_unique<BlockStmt>(std::vector<std::unique_ptr<Stmt>>{});
    }

    return std::make_unique<ForStmt>(
        std::move(variablePattern),
        std::move(iterable),
        std::move(body),
        forToken
    );
}

std::unique_ptr<Stmt> Parser::cForStatement(Token forToken) {
    // Detect `for (<pattern> in <iterable>)` — for-in style with parens.
    // We classify the form by a small brace-aware scan:
    //   for ( IDENT in          )   → for-in shape (2-token lookahead)
    //   for ( [ ... ] in        )   → for-in shape (scan over brackets)
    //   for ( { ... } in        )   → for-in shape
    //   for ( let / ;           )   → C-for shape (no binding pattern possible)
    bool isForInShape = false;
    Token currentTok = peek();
    if (currentTok.type == TokenType::IDENTIFIER &&
        peekNext().type == TokenType::IN) {
        isForInShape = true;
    } else if (currentTok.type == TokenType::LEFT_BRACKET ||
               currentTok.type == TokenType::LEFT_BRACE) {
        // Scan to the matching closer; if the next non-trivia token is IN,
        // this is a for-in.
        int depth = 0;
        Token opener = currentTok;
        size_t scanIndex = current;
        while (scanIndex < tokens.size()) {
            TokenType tt = tokens[scanIndex].type;
            if (tt == opener.type) depth++;
            else if ((opener.type == TokenType::LEFT_BRACKET &&
                      tt == TokenType::RIGHT_BRACKET) ||
                     (opener.type == TokenType::LEFT_BRACE &&
                      tt == TokenType::RIGHT_BRACE)) {
                depth--;
                if (depth == 0) {
                    // Position just after the closer; check the next token.
                    if (scanIndex + 1 < tokens.size() &&
                        tokens[scanIndex + 1].type == TokenType::IN) {
                        isForInShape = true;
                    }
                    break;
                }
            }
            if (tt == TokenType::END_OF_FILE) break;
            scanIndex++;
        }
    }
    if (isForInShape) {
        // We've already consumed '('. Parse binding pattern, then in, then
        // iterable, then expect ')'.
        auto variablePattern = parseForBindingPattern();
        if (!match(TokenType::IN)) {
            error("Expected 'in' after loop variable");
        }
        auto iterable = expression();
        if (!iterable) {
            iterable = std::make_unique<ErrorExpr>("Expected iterable after 'in'", peek());
        }
        if (!match(TokenType::RIGHT_PAREN)) {
            error("Expected ')' after for-in iterable");
        }
        std::unique_ptr<BlockStmt> body;
        if (match(TokenType::LEFT_BRACE)) {
            body = blockStatement();
            if (!body) {
                body = std::make_unique<BlockStmt>(std::vector<std::unique_ptr<Stmt>>{});
            }
        } else {
            error("Expected '{' before for loop body");
            body = std::make_unique<BlockStmt>(std::vector<std::unique_ptr<Stmt>>{});
        }
        return std::make_unique<ForStmt>(
            std::move(variablePattern),
            std::move(iterable),
            std::move(body),
            forToken
        );
    }

    // Parse: for (initializer? ; condition? ; increment?) body

    std::unique_ptr<Stmt> initializer;

    // Parse optional initializer (let-stmt, expression-stmt, or empty).
    // letStatement() consumes its own trailing semicolon; expression/empty
    // initializers need us to consume the clause-separating ';'.
    bool initHadSemicolon = false;

    if (match(TokenType::LET)) {
        // let x = expr  — letStatement() always returns a valid node now
        initializer = letStatement();
        initHadSemicolon = true;  // letStatement already consumed ';'
    } else if (match(TokenType::CONST)) {
        // const x = expr  — constStatement() always returns a valid node now
        initializer = constStatement();
        initHadSemicolon = true;  // constStatement already consumed ';'
    } else if (!check(TokenType::SEMICOLON)) {
        // Expression initializer (e.g. x = 0)
        auto expr = expression();
        if (expr) {
            initializer = std::make_unique<ExprStmt>(std::move(expr));
        }
    }

    if (!initHadSemicolon) {
        // Consume ';' after initializer (not needed for let-stmt init).
        match(TokenType::SEMICOLON);
    }

    // Parse condition (optional — null means always true)
    std::unique_ptr<Expr> condition;
    if (!check(TokenType::SEMICOLON)) {
        condition = expression();
    }

    if (!match(TokenType::SEMICOLON)) {
        error("Expected ';' after for-loop condition");
        // Continue — increment and body might still be parsable.
    }

    // Parse increment (optional)
    std::unique_ptr<Expr> increment;
    if (!check(TokenType::RIGHT_PAREN)) {
        increment = expression();
    }

    if (!match(TokenType::RIGHT_PAREN)) {
        error("Expected ')' after for-loop clauses");
        // Continue — body might still be parsable.
    }

    // Parse body (block or single statement)
    std::unique_ptr<Stmt> body;
    if (match(TokenType::LEFT_BRACE)) {
        body = blockStatement();
        if (!body) body = std::make_unique<ErrorStmt>("Failed to parse for-loop body", peek());
    } else {
        body = statement();
        if (!body) body = std::make_unique<ErrorStmt>("Failed to parse for-loop body", peek());
    }

    return std::make_unique<CForStmt>(
        std::move(initializer),
        std::move(condition),
        std::move(increment),
        std::move(body)
    );
}

std::unique_ptr<Stmt> Parser::funcStatement(bool isAsync) {

    std::string name;
    Token nameToken;  // saved for LSP position info
    if (!match(TokenType::IDENTIFIER)) {
        error("Expected function name");
        name = "?error?";
        nameToken = previous();  // error token
    } else {
        name = previous().lexeme;
        nameToken = previous();
    }

    if (!match(TokenType::LEFT_PAREN)) {
        error("Expected '(' after function name");
        // Continue — try to parse params without '('
    }

    std::vector<ParamDecl> params;

    if (!check(TokenType::RIGHT_PAREN)) {

        do {
            // Check for rest parameter: ...name
            bool isRest = match(TokenType::DOT_DOT_DOT);
            if (isRest) {
                if (!match(TokenType::IDENTIFIER)) {
                    error("Expected parameter name after '...'");
                    break;
                }
                std::string paramName = previous().lexeme;
                if (match(TokenType::EQUAL)) {
                    error("Rest parameter cannot have a default value");
                }
                if (check(TokenType::COMMA)) {
                    error("Rest parameter must be the last parameter");
                }
                params.emplace_back(std::move(paramName), nullptr, true);
                break;  // rest is always last — skip to ')'
            }

            // Check for destructuring pattern: {x, y} or [a, b]
            if (peek().type == TokenType::LEFT_BRACE ||
                peek().type == TokenType::LEFT_BRACKET) {
                auto pattern = parseBindingPattern();
                if (!pattern) {
                    error("Expected destructuring pattern");
                    continue;
                }
                std::unique_ptr<Expr> defaultValue;
                if (match(TokenType::EQUAL)) {
                    defaultValue = expression();
                }
                ParamDecl pd("", std::move(defaultValue));
                pd.pattern = std::move(pattern);
                params.push_back(std::move(pd));
                continue;
            }

            if (!match(TokenType::IDENTIFIER)) {
                error("Expected parameter name");
                // Skip this param and continue
                if (!check(TokenType::COMMA) && !check(TokenType::RIGHT_PAREN)) {
                    advance();  // skip the bad token
                }
                continue;
            }

            std::string paramName = previous().lexeme;

            // Optional default value: param = expr
            std::unique_ptr<Expr> defaultValue;
            if (match(TokenType::EQUAL)) {
                defaultValue = expression();
                if (!defaultValue) {
                    defaultValue = std::make_unique<ErrorExpr>("Expected default value", peek());
                }
            } else {
                // Validate: required params cannot follow default params
                if (!params.empty() && params.back().defaultValue) {
                    error("Required parameter cannot follow a parameter with a default value");
                }
            }

            params.emplace_back(std::move(paramName), std::move(defaultValue));
        }
        while (match(TokenType::COMMA));
    }

    if (!match(TokenType::RIGHT_PAREN)) {
        error("Expected ')' after parameters");
        // Continue — body is still parsable
    }

    std::shared_ptr<BlockStmt> body;
    if (!match(TokenType::LEFT_BRACE)) {
        error("Expected '{' before function body");
        body = std::make_shared<BlockStmt>(std::vector<std::unique_ptr<Stmt>>{});
    } else {
        auto bodyResult = blockStatement();
        if (!bodyResult) {
            body = std::make_shared<BlockStmt>(std::vector<std::unique_ptr<Stmt>>{});
        } else {
            body = std::shared_ptr<BlockStmt>(std::move(bodyResult));
        }
    }

    return std::make_unique<FuncStmt>(
        name,
        nameToken,
        std::move(params),
        std::move(body),
        false,
        isAsync
    );
}

std::unique_ptr<Expr> Parser::yieldExpression() {
    Token keyword = previous();
    // yield <expr>  OR  yield (no value → yields null)
    if (check(TokenType::SEMICOLON) || check(TokenType::RIGHT_BRACE) ||
        check(TokenType::RIGHT_PAREN) || check(TokenType::COMMA) ||
        check(TokenType::RIGHT_BRACKET) || isAtEnd()) {
        return std::make_unique<YieldExpr>(nullptr, std::move(keyword));
    }
    auto value = expression();
    return std::make_unique<YieldExpr>(std::move(value), std::move(keyword));
}

std::unique_ptr<Expr> Parser::awaitExpression() {
    Token keyword = previous();
    // await <expr>
    if (check(TokenType::SEMICOLON) || check(TokenType::RIGHT_BRACE) ||
        check(TokenType::RIGHT_PAREN) || check(TokenType::COMMA) ||
        check(TokenType::RIGHT_BRACKET) || isAtEnd()) {
        error("Expected expression after 'await'");
        return std::make_unique<AwaitExpr>(nullptr, std::move(keyword));
    }
    auto value = expression();
    return std::make_unique<AwaitExpr>(std::move(value), std::move(keyword));
}

MatchPattern Parser::parseMatchPattern() {
    // Wildcard: _
    if (check(TokenType::IDENTIFIER) && peek().lexeme == "_") {
        Token underscore = advance();
        MatchPattern p;
        p.kind = PatternKind::Wildcard;
        p.start = underscore;
        return p;
    }

    // Boolean and null literals (cannot be part of ranges)
    if (match(TokenType::TRUE)) {
        MatchPattern p;
        p.kind = PatternKind::Literal;
        p.literal = true;
        p.start = previous();
        return p;
    }
    if (match(TokenType::FALSE)) {
        MatchPattern p;
        p.kind = PatternKind::Literal;
        p.literal = false;
        p.start = previous();
        return p;
    }
    if (match(TokenType::NULL_TOKEN)) {
        MatchPattern p;
        p.kind = PatternKind::Literal;
        p.literal = nullptr;
        p.start = previous();
        return p;
    }

    // Numbers and strings: parse as primary, then check for range operator
    // (moved below to allow 1..=5 range patterns)
    auto low = primary();
    if (!low) {
        MatchPattern p;
        p.kind = PatternKind::Wildcard;  // error recovery
        p.start = peek();
        return p;
    }

    // Check for range operator: .. or ..=
    if (match(TokenType::DOT_DOT) || match(TokenType::DOT_DOT_EQUAL)) {
        bool inclusive = (previous().type == TokenType::DOT_DOT_EQUAL);
        auto high = primary();
        if (!high) {
            MatchPattern p;
            p.kind = PatternKind::Wildcard;
            p.start = previous();
            return p;
        }
        MatchPattern p;
        p.kind = PatternKind::Range;
        p.start = previous();
        if (auto* lit = dynamic_cast<LiteralExpr*>(low.get())) {
            p.rangeLow = lit->value;
        } else {
            p.rangeLow = static_cast<double>(0);
        }
        if (auto* lit = dynamic_cast<LiteralExpr*>(high.get())) {
            p.rangeHigh = lit->value;
        } else {
            p.rangeHigh = static_cast<double>(0);
        }
        p.rangeInclusive = inclusive;
        return p;
    }

    // Plain literal pattern (from primary)
    MatchPattern p;
    p.kind = PatternKind::Literal;
    p.start = previous();
    if (auto* lit = dynamic_cast<LiteralExpr*>(low.get())) {
        p.literal = lit->value;
    } else {
        // Not a valid pattern literal — error recovery as wildcard
        p.kind = PatternKind::Wildcard;
    }
    return p;
}

std::unique_ptr<Expr> Parser::matchExpression() {
    Token matchKeyword = previous();  // 'match' token already consumed by caller

    // Parse scrutinee
    auto scrutinee = expression();
    if (!scrutinee) {
        scrutinee = std::make_unique<ErrorExpr>("Expected expression after 'match'", matchKeyword);
    }

    // Expect '{'
    if (!match(TokenType::LEFT_BRACE)) {
        error("Expected '{' after match scrutinee");
        return std::make_unique<ErrorExpr>("Expected '{' after match scrutinee", peek());
    }

    std::vector<MatchCase> cases;

    // Parse arms (comma-separated)
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        MatchCase case_;
        Token firstPatternTok = peek();

        // Parse one or more patterns separated by `|`. v1 supports
        // arbitrary-length or-pattern lists (P1-B: | token now lexed
        // as TokenType::PIPE; `||` short-circuit OR is unaffected since
        // the lexer only emits PIPE for the bare `|` token).
        auto pat = parseMatchPattern();
        case_.patterns.push_back(std::move(pat));
        while (match(TokenType::PIPE)) {
            case_.patterns.push_back(parseMatchPattern());
        }
        if (case_.patterns.size() > 1) {
            // Syntax guard: Vora v1 or-patterns may only combine patterns
            // of the same kind so the runtime can dispatch by a single
            // test, instead of needing a sub-tree per arm. The kind of
            // the first pattern is the canonical kind for the arm.
            PatternKind canonical = case_.patterns.front().kind;
            for (size_t i = 1; i < case_.patterns.size(); ++i) {
                if (case_.patterns[i].kind != canonical) {
                    error("Mixed pattern kinds in or-pattern: each `|` branch must use the same pattern form");
                    // Continue parsing so the surrounding match expression
                    // still produces a usable AST node.
                }
            }
        }

        // Expect =>
        if (!match(TokenType::FAT_ARROW)) {
            error("Expected '=>' after match pattern");
            synchronize();
            if (check(TokenType::RIGHT_BRACE)) break;
            continue;
        }
        case_.arrow = previous();

        // Parse body: block { ... } or expression
        // NOTE: blockStatement() expects LEFT_BRACE already consumed by caller
        if (match(TokenType::LEFT_BRACE)) {
            auto block = blockStatement();
            case_.blockBody = std::shared_ptr<BlockStmt>(std::move(block));
        } else {
            // Expression body
            case_.body = expression();
            if (!case_.body) {
                case_.body = std::make_unique<ErrorExpr>(
                    "Expected expression after '=>'", case_.arrow);
            }
        }

        cases.push_back(std::move(case_));

        // Optional comma between arms
        match(TokenType::COMMA);
    }

    Token rightBrace = peek();
    if (!match(TokenType::RIGHT_BRACE)) {
        error("Expected '}' after match arms");
    }
    rightBrace = previous();

    if (cases.empty()) {
        error("Match expression must have at least one arm");
        return std::make_unique<ErrorExpr>("Empty match expression", matchKeyword);
    }

    return std::make_unique<MatchExpr>(
        std::move(scrutinee),
        std::move(cases),
        matchKeyword,
        rightBrace
    );
}

std::unique_ptr<Expr> Parser::funcExpression(bool isAsync) {
    // Parse anonymous function: func(params) { body }
    // 'func' keyword already consumed by caller.

    if (!match(TokenType::LEFT_PAREN)) {
        return errorExpr("Expected '(' after 'func'");
    }

    std::vector<ParamDecl> params;

    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            // Check for rest parameter: ...name
            bool isRest = match(TokenType::DOT_DOT_DOT);
            if (isRest) {
                if (!match(TokenType::IDENTIFIER)) {
                    return errorExpr("Expected parameter name after '...'");
                }
                std::string paramName = previous().lexeme;
                if (match(TokenType::EQUAL)) {
                    return errorExpr("Rest parameter cannot have a default value");
                }
                if (check(TokenType::COMMA)) {
                    return errorExpr("Rest parameter must be the last parameter");
                }
                params.emplace_back(std::move(paramName), nullptr, true);
                break;  // rest is always last — skip to ')'
            }

            // Check for destructuring pattern in lambda params
            if (peek().type == TokenType::LEFT_BRACE ||
                peek().type == TokenType::LEFT_BRACKET) {
                auto pattern = parseBindingPattern();
                if (!pattern) return errorExpr("Expected destructuring pattern");
                std::unique_ptr<Expr> defaultValue;
                if (match(TokenType::EQUAL)) {
                    defaultValue = expression();
                }
                ParamDecl pd("", std::move(defaultValue));
                pd.pattern = std::move(pattern);
                params.push_back(std::move(pd));
                continue;
            }

            if (!match(TokenType::IDENTIFIER)) {
                return errorExpr("Expected parameter name");
            }

            std::string paramName = previous().lexeme;

            // Optional default value: param = expr
            std::unique_ptr<Expr> defaultValue;
            if (match(TokenType::EQUAL)) {
                defaultValue = expression();
                if (!defaultValue) {
                    return errorExpr("Expected default value expression");
                }
            } else {
                if (!params.empty() && params.back().defaultValue) {
                    return errorExpr("Required parameter cannot follow a parameter with a default value");
                }
            }

            params.emplace_back(std::move(paramName), std::move(defaultValue));
        }
        while (match(TokenType::COMMA));
    }

    if (!match(TokenType::RIGHT_PAREN)) {
        return errorExpr("Expected ')' after parameters");
    }

    if (!match(TokenType::LEFT_BRACE)) {
        return errorExpr("Expected '{' before function body");
    }

    auto body = blockStatement();
    if (!body) {
        return errorExpr("Failed to parse function body");
    }

    return std::make_unique<FuncExpr>(
        std::move(params),
        std::shared_ptr<BlockStmt>(std::move(body)),
        isAsync
    );
}

std::unique_ptr<Stmt> Parser::objStatement() {

    std::string name;
    Token nameToken;  // saved for LSP position info
    if (!match(TokenType::IDENTIFIER)) {
        error("Expected object name");
        name = "?error?";
        nameToken = previous();  // error token
    } else {
        name = previous().lexeme;
        nameToken = previous();
    }

    // Optional inheritance: Obj Child : Parent1, Parent2, ... (params) { ... }
    std::vector<std::string> parentNames;
    if (match(TokenType::COLON)) {
        do {
            if (!match(TokenType::IDENTIFIER)) {
                error("Expected parent class name after ':'");
                // Skip bad token and continue
                if (!check(TokenType::COMMA) && !check(TokenType::LEFT_PAREN)) {
                    advance();
                }
                continue;
            }
            parentNames.push_back(previous().lexeme);
        } while (match(TokenType::COMMA));
    }

    if (!match(TokenType::LEFT_PAREN)) {
        error("Expected '(' after object name");
        // Continue — try to parse params without '('
    }

    std::vector<ParamDecl> params;

    if (!check(TokenType::RIGHT_PAREN)) {

        do {
            // Check for rest parameter: ...name
            bool isRest = match(TokenType::DOT_DOT_DOT);
            if (isRest) {
                if (!match(TokenType::IDENTIFIER)) {
                    error("Expected parameter name after '...'");
                    break;
                }
                std::string paramName = previous().lexeme;
                if (match(TokenType::EQUAL)) {
                    error("Rest parameter cannot have a default value");
                }
                if (check(TokenType::COMMA)) {
                    error("Rest parameter must be the last parameter");
                }
                params.emplace_back(std::move(paramName), nullptr, true);
                break;  // rest is always last — skip to ')'
            }

            // Check for destructuring pattern in obj params
            if (peek().type == TokenType::LEFT_BRACE ||
                peek().type == TokenType::LEFT_BRACKET) {
                auto pattern = parseBindingPattern();
                if (!pattern) {
                    error("Expected destructuring pattern");
                    continue;
                }
                std::unique_ptr<Expr> defaultValue;
                if (match(TokenType::EQUAL)) {
                    defaultValue = expression();
                }
                ParamDecl pd("", std::move(defaultValue));
                pd.pattern = std::move(pattern);
                params.push_back(std::move(pd));
                continue;
            }

            if (!match(TokenType::IDENTIFIER)) {
                error("Expected parameter name");
                if (!check(TokenType::COMMA) && !check(TokenType::RIGHT_PAREN)) {
                    advance();
                }
                continue;
            }

            std::string paramName = previous().lexeme;

            // Optional default value: param = expr
            std::unique_ptr<Expr> defaultValue;
            if (match(TokenType::EQUAL)) {
                defaultValue = expression();
                if (!defaultValue) {
                    defaultValue = std::make_unique<ErrorExpr>("Expected default value", peek());
                }
                if (!params.empty() && !params.back().defaultValue) {
                    error("Required parameter cannot follow a parameter with a default value");
                }
            } else {
                if (!params.empty() && params.back().defaultValue) {
                    error("Required parameter cannot follow a parameter with a default value");
                }
            }

            params.emplace_back(std::move(paramName), std::move(defaultValue));
        }
        while (match(TokenType::COMMA));
    }

    if (!match(TokenType::RIGHT_PAREN)) {
        error("Expected ')' after parameters");
        // Continue — body is still parsable
    }

    std::shared_ptr<BlockStmt> body;
    if (!match(TokenType::LEFT_BRACE)) {
        error("Expected '{' before object body");
        body = std::make_shared<BlockStmt>(std::vector<std::unique_ptr<Stmt>>{});
    } else {
        // Object body members are classified by statement type:
        // - FuncStmt nodes (from `func` keyword) → methods vector
        // - All other statements → constructor body (bodyStmts)
        std::vector<std::unique_ptr<Stmt>> methods;
        std::vector<std::unique_ptr<Stmt>> bodyStmts;

        while (!isAtEnd() &&
               peek().type != TokenType::RIGHT_BRACE) {

            // Check for static method declaration: this.func name(...) { ... }
            // Only intercept when all three tokens match — otherwise let
            // statement() parse 'this.x' as a regular property access.
            bool isStatic = false;
            if (peek().type == TokenType::THIS &&
                tokens[current + 1].type == TokenType::DOT &&
                tokens[current + 2].type == TokenType::FUNC) {
                // Consume only 'this' and '.' — leave 'func' for statement().
                advance();  // 'this'
                advance();  // '.'
                isStatic = true;
                // statement() will see 'func' and parse the function declaration.
            }

            auto stmt = statement();

            if (!stmt) {
                stmt = std::make_unique<ErrorStmt>("Failed to parse object member", peek());
            }

            if (auto funcStmt = dynamic_cast<FuncStmt*>(stmt.get())) {
                if (isStatic) {
                    funcStmt->isStatic = true;
                }
                methods.push_back(std::move(stmt));
            } else if (isStatic) {
                // 'this.' was followed by something that wasn't a func
                // (shouldn't happen since we checked for func above)
                error("Only 'func' declarations can follow 'this.' in an object");
                bodyStmts.push_back(std::move(stmt));
            } else {
                bodyStmts.push_back(std::move(stmt));
            }
        }

        if (!match(TokenType::RIGHT_BRACE)) {
            error("Expected '}' after object body");
            // Return partial object — methods and body parsed so far are preserved.
        }

        body = std::make_shared<BlockStmt>(std::move(bodyStmts));

        // We already built the methods vector inside the brace block.
        // Need to restructure: ObjStmt takes methods as a separate vector.
        // We can't easily return both body and methods from inside this scope,
        // so let's restructure the return.
        return std::make_unique<ObjStmt>(
            name,
            nameToken,
            std::move(parentNames),
            std::move(params),
            std::move(methods),
            body
        );
    }

    // This path is taken when '{' was missing — no methods, empty body.
    return std::make_unique<ObjStmt>(
        name,
        nameToken,
        std::move(parentNames),
        std::move(params),
        std::vector<std::unique_ptr<Stmt>>{},  // no methods
        body
    );
}

std::unique_ptr<Stmt> Parser::whileStatement() {

    if (!match(TokenType::LEFT_PAREN)) {
        error("Expected '(' after while");
        // Continue anyway — try to parse condition without '('
    }

    auto condition = expression();
    if (!condition) {
        condition = std::make_unique<ErrorExpr>("Expected condition after while", peek());
    }

    if (!match(TokenType::RIGHT_PAREN)) {
        error("Expected ')' after condition");
        // Continue — body is still parsable
    }

    auto body = statement();
    if (!body) {
        body = std::make_unique<ErrorStmt>("Expected body for while loop", peek());
    }

    return std::make_unique<WhileStmt>(
        std::move(condition),
        std::move(body)
    );
}

std::unique_ptr<Stmt> Parser::doWhileStatement() {
    // Parse body first (always executes at least once)
    auto body = statement();
    if (!body) {
        body = std::make_unique<ErrorStmt>("Expected body for do-while loop", peek());
    }

    // Expect 'while' keyword
    Token whileToken = peek();
    if (!match(TokenType::WHILE)) {
        error("Expected 'while' after 'do' body");
        return std::make_unique<DoWhileStmt>(
            std::make_unique<ErrorExpr>("Expected 'while' after do body", whileToken),
            std::move(body)
        );
    }

    // Expect '('
    if (!match(TokenType::LEFT_PAREN)) {
        error("Expected '(' after 'while'");
        // Continue anyway — try to parse condition without '('
    }

    auto condition = expression();
    if (!condition) {
        condition = std::make_unique<ErrorExpr>("Expected condition after while", peek());
    }

    // Expect ')'
    if (!match(TokenType::RIGHT_PAREN)) {
        error("Expected ')' after condition");
        // Continue — the AST is still valid
    }

    // Optional trailing semicolon
    match(TokenType::SEMICOLON);

    return std::make_unique<DoWhileStmt>(
        std::move(condition),
        std::move(body)
    );
}

// =========================
// UTIL
// =========================

bool Parser::isAtEnd() const {
    return peek().type == TokenType::END_OF_FILE;
}

Token Parser::peek() const {
    return tokens[current];
}

Token Parser::peekNext() const {
    if (current + 1 >= tokens.size()) {
        // Return a synthesized EOF token to avoid OOB; the caller can check
        // the type to detect end-of-stream. Mirrors lexer END_OF_FILE.
        static const Token kEof{TokenType::END_OF_FILE, "", 0, 0};
        return kEof;
    }
    return tokens[current + 1];
}

Token Parser::previous() const {
    return tokens[current - 1];
}

Token Parser::advance() {
    if (!isAtEnd()) {
        current++;
    }
    return previous();
}

bool Parser::match(TokenType type) {
    if (peek().type != type) return false;
    advance();
    // Track paren / bracket depth for ASI scoping. Used by
    // parsePrecedence() to know whether a newline should be treated as
    // an implicit semicolon (only when depth is 0).
    switch (type) {
        case TokenType::LEFT_PAREN:
        case TokenType::LEFT_BRACKET:
            parenDepth_++;
            break;
        case TokenType::RIGHT_PAREN:
        case TokenType::RIGHT_BRACKET:
            if (parenDepth_ > 0) parenDepth_--;
            break;
        default:
            break;
    }
    return true;
}

bool Parser::check(TokenType type) const {
    return peek().type == type;
}

// =========================
// PRECEDENCE TABLE
// =========================

int Parser::getPrecedence(TokenType type) const {
    switch (type) {

        case TokenType::EQUAL:
        case TokenType::PLUS_EQUAL:
        case TokenType::MINUS_EQUAL:
        case TokenType::MULTIPLY_EQUAL:
        case TokenType::DIVIDE_EQUAL:
        case TokenType::MODULO_EQUAL:
        case TokenType::POWER_EQUAL:
            return 1; // lowest, right-associative handled in Pratt

        case TokenType::OR:
        case TokenType::QUESTION_QUESTION:
            return 1;

        case TokenType::AND:
            return 2;

        case TokenType::QUESTION:
            return 1; // P0 #2 fix: ternary binds less tightly than ||.
                      // OR/??/? all share precedence 1 — OR's left-
                      // associativity then drains || first, leaving the
                      // ternary to wrap the resulting logical value
                      // (matches JS / C / Java semantics).

        // P1-F: bitwise operators. Precedence follows C/JS — `&` < `^`
        // < `|` (each more tightly bound than the next), all looser than
        // `==`/`!=`. This is the classic C/JS layout: `a & b == c`
        // parses as `a & (b == c)`. Documented explicitly in the EBNF
        // (see docs/16-v1.0-grammar-ebnf.md §6) because it surprises.
        case TokenType::PIPE:
            return 3;

        case TokenType::CARET:
            return 4;

        case TokenType::AMPERSAND:
            return 5;

        case TokenType::EQUAL_EQUAL:
        case TokenType::NOT_EQUAL:
            return 6;

        // P1-G: `in` is now also an infix operator (returns bool).
        // Precedence between comparison (6) and additive (9) so
        // `a + b in arr` parses as `(a + b) in arr` (matches Python).
        case TokenType::IN:
            return 7;

        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
            return 7;

        // P1-F: shifts bind tighter than relational but looser than
        // additive (C/JS): `a + b << c` = `(a + b) << c`.
        case TokenType::LESS_LESS:
        case TokenType::GREATER_GREATER:
            return 8;

        case TokenType::PLUS:
        case TokenType::MINUS:
            return 9;

        case TokenType::MULTIPLY:
        case TokenType::DIVIDE:
        case TokenType::MODULO:
            return 10;

        case TokenType::POWER:
            return 11;

        default:
            return 0;
    }
}

// =========================
// PRIMARY
// =========================

std::unique_ptr<Expr> Parser::primary() {

    if (match(TokenType::MINUS)) {
        Token op = previous();
        // Use call() (not primary()) so postfix ops like [i], .prop, ()
        // bind tighter than unary minus: -a[i] → -(a[i]).
        auto right = call();

        if (!right) right = std::make_unique<ErrorExpr>("Expected expression after '-'", op);

        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    // Unary plus: +x is just x (identity). Allowed for symmetry with -x
    // and so expressions like `+"42"` (Number coerce) and `+a` parse.
    // The compiler folds this into a no-op at codegen time.
    if (match(TokenType::PLUS)) {
        Token op = previous();
        auto right = call();

        if (!right) right = std::make_unique<ErrorExpr>("Expected expression after '+'", op);

        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    if (match(TokenType::NOT)) {
        Token op = previous();
        // Same as unary minus: postfix binds tighter than not.
        // NOTE: `not` is an alias for `!` and therefore binds tighter than
        // comparison (unlike Python's `not`). `not x > 0` parses as
        // `(not x) > 0` — write `not (x > 0)`.
        auto right = call();

        if (!right) right = std::make_unique<ErrorExpr>("Expected expression after '!' / 'not'", op);

        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    // Bitwise NOT (~x, P1-F). Unary prefix, binds like unary minus.
    if (match(TokenType::TILDE)) {
        Token op = previous();
        auto right = call();

        if (!right) right = std::make_unique<ErrorExpr>("Expected expression after '~'", op);

        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    // prefix ++x / --x
    if (match(TokenType::PLUS_PLUS) || match(TokenType::MINUS_MINUS)) {
        Token op = previous();
        auto target = call();  // use call() so ++obj.prop chains work

        if (!target) target = std::make_unique<ErrorExpr>("Expected target for prefix " + op.lexeme, op);

        // target must be assignable
        if (!dynamic_cast<VariableExpr*>(target.get()) &&
            !dynamic_cast<PropertyExpr*>(target.get())) {
            return errorExpr("Invalid target for prefix " + op.lexeme);
        }

        return std::make_unique<IncDecExpr>(op, std::move(target), true);
    }

    if (match(TokenType::YIELD)) {
        return yieldExpression();
    }

    if (match(TokenType::AWAIT)) {
        return awaitExpression();
    }

    if (match(TokenType::MATCH)) {
        return matchExpression();
    }

    bool isAsyncExpr = match(TokenType::ASYNC);
    if (isAsyncExpr) {
        if (!match(TokenType::FUNC)) {
            return errorExpr("Expected 'func' after 'async'");
        }
    }
    if (isAsyncExpr || match(TokenType::FUNC)) {
        return funcExpression(isAsyncExpr);
    }

    if (match(TokenType::THIS)) {
        return std::make_unique<ThisExpr>(
            previous()
        );
    }

    if (match(TokenType::SUPER)) {
        return std::make_unique<SuperExpr>(
            previous()
        );
    }

    if (match(TokenType::NUMBER)) {
        const std::string& lexeme = previous().lexeme;

        // Handle hex, octal, binary prefixes — always integer
        if (lexeme.size() >= 2 && lexeme[0] == '0') {
            switch (lexeme[1]) {
                case 'x': case 'X':
                    try {
                        return std::make_unique<LiteralExpr>(
                            static_cast<int64_t>(std::stoull(lexeme, nullptr, 16))
                        );
                    } catch (const std::invalid_argument&) {
                        error("Invalid hex literal");
                        return std::make_unique<ErrorExpr>("Invalid hex literal", previous());
                    } catch (const std::out_of_range&) {
                        error("Hex literal out of range");
                        return std::make_unique<ErrorExpr>("Hex literal out of range", previous());
                    }
                case 'o': case 'O':
                    try {
                        return std::make_unique<LiteralExpr>(
                            static_cast<int64_t>(std::stoull(lexeme.substr(2), nullptr, 8))
                        );
                    } catch (const std::invalid_argument&) {
                        error("Invalid octal literal");
                        return std::make_unique<ErrorExpr>("Invalid octal literal", previous());
                    } catch (const std::out_of_range&) {
                        error("Octal literal out of range");
                        return std::make_unique<ErrorExpr>("Octal literal out of range", previous());
                    }
                case 'b': case 'B':
                    try {
                        return std::make_unique<LiteralExpr>(
                            static_cast<int64_t>(std::stoull(lexeme.substr(2), nullptr, 2))
                        );
                    } catch (const std::invalid_argument&) {
                        error("Invalid binary literal");
                        return std::make_unique<ErrorExpr>("Invalid binary literal", previous());
                    } catch (const std::out_of_range&) {
                        error("Binary literal out of range");
                        return std::make_unique<ErrorExpr>("Binary literal out of range", previous());
                    }
                default:
                    break;
            }
        }

        // Decimal: check for float vs integer
        bool hasDot = (lexeme.find('.') != std::string::npos);
        bool hasExp = (lexeme.find('e') != std::string::npos || lexeme.find('E') != std::string::npos);

        if (hasDot || hasExp) {
            // Float literal
            return std::make_unique<LiteralExpr>(std::stod(lexeme));
        } else {
            // Integer literal — try int64, fallback to double if overflow
            try {
                size_t pos;
                int64_t ival = std::stoll(lexeme, &pos);
                if (pos == lexeme.size()) {
                    return std::make_unique<LiteralExpr>(ival);
                }
            } catch (const std::out_of_range&) {
                // Overflow — fall through to double
            }
            return std::make_unique<LiteralExpr>(std::stod(lexeme));
        }
    }

    if (match(TokenType::STRING)) {
        return std::make_unique<LiteralExpr>(
            GcHeap::instance().alloc<GcString>(previous().lexeme)
        );
    }

    if (match(TokenType::TRUE)) {
        return std::make_unique<LiteralExpr>(
            true
        );
    }

    if (match(TokenType::FALSE)) {
        return std::make_unique<LiteralExpr>(
            false
        );
    }

    if (match(TokenType::NULL_TOKEN)) {

        return std::make_unique<LiteralExpr>(
            nullptr
        );
    }

    if (match(TokenType::IDENTIFIER)) {
        return std::make_unique<VariableExpr>(
            previous().lexeme,
            previous()
        );
    }

    if (match(TokenType::LEFT_BRACKET)) {
        Token leftBracket = previous();

        // Empty array: []
        if (match(TokenType::RIGHT_BRACKET)) {
            return std::make_unique<ArrayExpr>(
                std::vector<std::unique_ptr<Expr>>{},
                leftBracket
            );
        }

        // Parse first expression — could be an array element or a comprehension result
        auto first = expression();
        if (!first) {
            first = std::make_unique<ErrorExpr>("Expected expression", peek());
        }

        // Check if this is a list comprehension: [expr for var in iterable if cond]
        if (match(TokenType::FOR)) {
            // Parse loop variable name
            std::string variable;
            if (!match(TokenType::IDENTIFIER)) {
                error("Expected variable name after 'for' in list comprehension");
                variable = "?error?";
            } else {
                variable = previous().lexeme;
            }

            // Parse 'in' keyword
            if (!match(TokenType::IN)) {
                error("Expected 'in' after variable in list comprehension");
            }

            // Parse iterable expression
            auto iterable = expression();
            if (!iterable) {
                iterable = std::make_unique<ErrorExpr>("Expected iterable after 'in' in list comprehension", peek());
            }

            // Optional 'if' condition
            std::unique_ptr<Expr> condition;
            if (match(TokenType::IF)) {
                condition = expression();
                if (!condition) {
                    condition = std::make_unique<ErrorExpr>("Expected condition after 'if' in list comprehension", peek());
                }
            }

            // Expect ']'
            if (!match(TokenType::RIGHT_BRACKET)) {
                error("Expected ']' after list comprehension");
            }

            return std::make_unique<ListCompExpr>(
                std::move(first),
                std::move(variable),
                std::move(iterable),
                std::move(condition),
                leftBracket
            );
        }

        // Regular array literal: [elem1, elem2, ...]
        std::vector<std::unique_ptr<Expr>> elements;
        elements.push_back(std::move(first));

        while (match(TokenType::COMMA)) {
            auto element = expression();

            if (!element) {
                element = std::make_unique<ErrorExpr>("Expected expression in array literal", peek());
            }

            elements.push_back(std::move(element));
        }

        if (!match(TokenType::RIGHT_BRACKET)) {
            error("Expected ']' after array literal");
            // Return partial array with whatever elements we parsed.
            return std::make_unique<ArrayExpr>(
                std::move(elements),
                leftBracket
            );
        }

        return std::make_unique<ArrayExpr>(
            std::move(elements),
            leftBracket
        );
    }

    if (match(TokenType::LEFT_BRACE)) {
        Token leftBrace = previous();

        // Empty dict: {}
        if (match(TokenType::RIGHT_BRACE)) {
            return std::make_unique<DictExpr>(
                std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>>{},
                leftBrace
            );
        }

        // Parse first pair (key, colon, value). We defer the bare-identifier→
        // string conversion because if this is a dict comprehension, the key
        // is a template expression (a variable), not a string literal.
        auto key = expression();
        if (!key) {
            key = std::make_unique<ErrorExpr>("Expected dict key", peek());
        }
        Token keyToken = previous();

        // Shorthand entry `{x}` / `{x, y}` means `{"x": x, "y": y}`
        // (syntax-review #2.11, matching the shorthand already accepted in
        // destructuring patterns). Guarded on `,`/`}` following the
        // identifier so dict comprehensions and other forms are unaffected.
        bool shorthand = false;
        std::string shorthandName;
        if (!check(TokenType::COLON) &&
            (check(TokenType::COMMA) || check(TokenType::RIGHT_BRACE))) {
            if (auto* varKey = dynamic_cast<VariableExpr*>(key.get())) {
                shorthand = true;
                shorthandName = varKey->name;
            }
        }

        std::unique_ptr<Expr> value;
        if (shorthand) {
            key = std::make_unique<LiteralExpr>(
                GcHeap::instance().alloc<GcString>(shorthandName));
            value = std::make_unique<VariableExpr>(shorthandName, keyToken);
        } else {
            if (!match(TokenType::COLON)) {
                error("Expected ':' after dict key");
            }

            value = expression();
            if (!value) {
                value = std::make_unique<ErrorExpr>("Expected dict value", peek());
            }
        }

        // Check if this is a dict comprehension: {key: val for var in iterable if cond}
        if (match(TokenType::FOR)) {
            // Parse loop variable name
            std::string variable;
            if (!match(TokenType::IDENTIFIER)) {
                error("Expected variable name after 'for' in dict comprehension");
                variable = "?error?";
            } else {
                variable = previous().lexeme;
            }

            // Parse 'in' keyword
            if (!match(TokenType::IN)) {
                error("Expected 'in' after variable in dict comprehension");
            }

            // Parse iterable expression
            auto iterable = expression();
            if (!iterable) {
                iterable = std::make_unique<ErrorExpr>("Expected iterable after 'in' in dict comprehension", peek());
            }

            // Optional 'if' condition
            std::unique_ptr<Expr> condition;
            if (match(TokenType::IF)) {
                condition = expression();
                if (!condition) {
                    condition = std::make_unique<ErrorExpr>("Expected condition after 'if' in dict comprehension", peek());
                }
            }

            // Expect '}'
            if (!match(TokenType::RIGHT_BRACE)) {
                error("Expected '}' after dict comprehension");
            }

            return std::make_unique<DictCompExpr>(
                std::move(key),
                std::move(value),
                std::move(variable),
                std::move(iterable),
                std::move(condition),
                leftBrace
            );
        }

        // Regular dict literal — now convert bare identifier keys to strings.
        if (auto varKey = dynamic_cast<VariableExpr*>(key.get())) {
            key = std::make_unique<LiteralExpr>(GcHeap::instance().alloc<GcString>(varKey->name));
        }

        std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> pairs;
        pairs.push_back({std::move(key), std::move(value)});

        while (match(TokenType::COMMA)) {
            auto nextKey = expression();
            if (!nextKey) {
                nextKey = std::make_unique<ErrorExpr>("Expected dict key", peek());
            }
            Token nextKeyToken = previous();

            // Shorthand entry (see above): {a: 1, b} -> b: b
            if (!check(TokenType::COLON) &&
                (check(TokenType::COMMA) || check(TokenType::RIGHT_BRACE))) {
                if (auto* varKey = dynamic_cast<VariableExpr*>(nextKey.get())) {
                    std::string name = varKey->name;
                    pairs.push_back({
                        std::make_unique<LiteralExpr>(
                            GcHeap::instance().alloc<GcString>(name)),
                        std::make_unique<VariableExpr>(name, nextKeyToken)
                    });
                    continue;
                }
            }

            if (auto varKey = dynamic_cast<VariableExpr*>(nextKey.get())) {
                nextKey = std::make_unique<LiteralExpr>(GcHeap::instance().alloc<GcString>(varKey->name));
            }

            if (!match(TokenType::COLON)) {
                error("Expected ':' after dict key");
                continue;
            }

            auto nextValue = expression();
            if (!nextValue) {
                nextValue = std::make_unique<ErrorExpr>("Expected dict value", peek());
            }

            pairs.push_back({std::move(nextKey), std::move(nextValue)});
        }

        if (!match(TokenType::RIGHT_BRACE)) {
            error("Expected '}' after dict literal");
            return std::make_unique<DictExpr>(std::move(pairs), leftBrace);
        }

        return std::make_unique<DictExpr>(std::move(pairs), leftBrace);
    }

    if (match(TokenType::LEFT_PAREN)) {
        auto expr = expression();

        if (!match(TokenType::RIGHT_PAREN)) {
            error("Expected ')' after expression");
            // Return partial grouping — the inner expression is still valid.
            return std::make_unique<GroupingExpr>(std::move(expr));
        }

        return std::make_unique<GroupingExpr>(std::move(expr));
    }

    return errorExpr("Unexpected token: " + peek().lexeme);
}

std::unique_ptr<Expr> Parser::finishCall(
    std::unique_ptr<Expr> callee
) {

    std::vector<std::unique_ptr<Expr>> arguments;
    std::vector<std::string> argumentNames;

    if (!check(TokenType::RIGHT_PAREN)) {

        do {
            // Check for call-site spread: ...expr
            if (match(TokenType::DOT_DOT_DOT)) {
                auto spreadExpr = expression();
                if (!spreadExpr) {
                    spreadExpr = std::make_unique<ErrorExpr>(
                        "Expected expression after ...", peek());
                }
                arguments.push_back(std::make_unique<SpreadExpr>(
                    std::move(spreadExpr), previous()));
                argumentNames.push_back("");
            }
            // Check for named argument: name = expr
            // 2-token lookahead: IDENTIFIER followed by EQUALS inside a call
            // argument list means a named argument, not an assignment expression.
            else if (check(TokenType::IDENTIFIER) &&
                     tokens.size() > 1 &&
                     static_cast<size_t>(current + 1) < tokens.size() &&
                     tokens[current + 1].type == TokenType::EQUAL) {
                std::string name = advance().lexeme;  // consume IDENTIFIER
                advance();                             // consume =
                auto value = expression();
                if (!value) {
                    value = std::make_unique<ErrorExpr>(
                        "Expected expression after =", peek());
                }
                arguments.push_back(std::move(value));
                argumentNames.push_back(name);
            } else {
                auto argument = expression();

                if (!argument) {
                    argument = std::make_unique<ErrorExpr>(
                        "Expected expression", peek());
                }

                arguments.push_back(
                    std::move(argument)
                );
                argumentNames.push_back("");
            }
        }
        while (match(TokenType::COMMA));
    }

    // Validate: positional arguments must precede named arguments.
    bool seenNamed = false;
    for (size_t i = 0; i < argumentNames.size(); i++) {
        if (!argumentNames[i].empty()) {
            seenNamed = true;
        } else if (seenNamed) {
            error("Positional argument cannot follow named argument");
        }
    }

    if (!match(TokenType::RIGHT_PAREN)) {

        error("Expected ')' after arguments");

        // Return partial call with whatever arguments we parsed.
        return std::make_unique<CallExpr>(
            std::move(callee),
            std::move(arguments),
            previous(),
            std::move(argumentNames)
        );
    }

    return std::make_unique<CallExpr>(
        std::move(callee),
        std::move(arguments),
        previous(),
        std::move(argumentNames)
    );
}

std::unique_ptr<Expr> Parser::call() {

    auto expr = primary();

    // Safety: primary() always returns a node now (ErrorExpr at minimum).
    if (!expr) {
        expr = std::make_unique<ErrorExpr>("Expected expression", peek());
    }

    while (true) {

        if (match(TokenType::LEFT_PAREN)) {

            expr = finishCall(
                std::move(expr)
            );

            if (!expr) {
                return std::make_unique<ErrorExpr>("Failed to parse call", previous());
            }

            continue;
        }

        if (match(TokenType::LEFT_BRACKET)) {

            auto indexExpr = expression();

            if (!indexExpr) {
                indexExpr = std::make_unique<ErrorExpr>("Expected index expression", peek());
            }

            if (!match(TokenType::RIGHT_BRACKET)) {
                error("Expected ']' after index expression");
                // Return partial index expression anyway.
                expr = std::make_unique<IndexExpr>(
                    std::move(expr),
                    std::move(indexExpr),
                    previous()
                );
                continue;
            }

            expr = std::make_unique<IndexExpr>(
                std::move(expr),
                std::move(indexExpr),
                previous()
            );
            continue;
        }

        if (match(TokenType::DOT)) {
            Token dot = previous();

            if (!match(TokenType::IDENTIFIER)) {
                error("Expected property name after '.'");
                // Keep the object expression (expr unchanged) — the ErrorExpr
                // approach would lose the object. Skip the bad token if it's
                // not a valid start of a new expression.
                if (!isAtEnd() &&
                    peek().type != TokenType::DOT &&
                    peek().type != TokenType::LEFT_PAREN &&
                    peek().type != TokenType::LEFT_BRACKET) {
                    advance();  // skip the non-identifier token
                }
                continue;
            }

            std::string property = previous().lexeme;

            expr = std::make_unique<PropertyExpr>(
                std::move(expr),
                property,
                dot
            );
            continue;
        }

        // Optional chaining: ?.property, ?.(args), ?.[index]
        if (match(TokenType::QUESTION_DOT)) {
            Token qdot = previous();

            // ?. must be followed by identifier, (, or [
            if (match(TokenType::IDENTIFIER)) {
                std::string property = previous().lexeme;
                auto chain = std::make_unique<OptionalChainExpr>(
                    std::move(expr),
                    OptionalChainExpr::Kind::PROPERTY,
                    qdot
                );
                chain->property = property;
                expr = std::move(chain);
                continue;
            }

            if (match(TokenType::LEFT_PAREN)) {
                auto chain = std::make_unique<OptionalChainExpr>(
                    std::move(expr),
                    OptionalChainExpr::Kind::CALL,
                    qdot
                );
                if (!check(TokenType::RIGHT_PAREN)) {
                    do {
                        // Named argument: name = expr (same detection as finishCall)
                        if (check(TokenType::IDENTIFIER) &&
                            tokens.size() > 1 &&
                            static_cast<size_t>(current + 1) < tokens.size() &&
                            tokens[current + 1].type == TokenType::EQUAL) {
                            std::string name = advance().lexeme;
                            advance();
                            auto arg = expression();
                            if (!arg) arg = std::make_unique<ErrorExpr>("Expected expression after =", peek());
                            chain->arguments.push_back(std::move(arg));
                            chain->argumentNames.push_back(name);
                        } else {
                            auto arg = expression();
                            if (!arg) arg = std::make_unique<ErrorExpr>("Expected argument expression", peek());
                            chain->arguments.push_back(std::move(arg));
                            chain->argumentNames.push_back("");
                        }
                    } while (match(TokenType::COMMA));
                }
                if (!match(TokenType::RIGHT_PAREN)) {
                    error("Expected ')' after optional call arguments");
                }
                chain->closeParen = previous();
                expr = std::move(chain);
                continue;
            }

            if (match(TokenType::LEFT_BRACKET)) {
                auto chain = std::make_unique<OptionalChainExpr>(
                    std::move(expr),
                    OptionalChainExpr::Kind::INDEX,
                    qdot
                );
                auto indexExpr = expression();
                if (!indexExpr) indexExpr = std::make_unique<ErrorExpr>("Expected index expression", peek());
                chain->index = std::move(indexExpr);
                if (!match(TokenType::RIGHT_BRACKET)) {
                    error("Expected ']' after optional index expression");
                }
                chain->closeBracket = previous();
                expr = std::move(chain);
                continue;
            }

            error("Expected property name, '(', or '[' after '?.'");
            continue;
        }

        // postfix x++ / x--  (only if lhs is an lvalue, otherwise
        // the ++/-- belongs to the next statement as a prefix operator)
        if ((check(TokenType::PLUS_PLUS) || check(TokenType::MINUS_MINUS)) &&
            (dynamic_cast<VariableExpr*>(expr.get()) ||
             dynamic_cast<PropertyExpr*>(expr.get()) ||
             dynamic_cast<IndexExpr*>(expr.get()))) {
            Token op = advance();  // consume ++ / --
            expr = std::make_unique<IncDecExpr>(op, std::move(expr), false);
            continue;
        }

        break;
    }

    return expr;
}

// =========================
// PRATT CORE
// =========================

std::unique_ptr<Expr> Parser::parsePrecedence(int precedence) {

    auto left = call();

    if (!left) {
        return errorExpr("Expected expression");
    }

    while (!isAtEnd() &&
           precedence < getPrecedence(peek().type)) {

        // ============================================================
        // Go-style Automatic Semicolon Insertion (ASI)
        // --------------------------------------------------------------
        // When we're parsing an expression at statement level (asiDepth_
        // > 0) and not inside any `(...)` / `[...]` (parenDepth_ == 0),
        // and the next infix operator begins on a new line relative to
        // the last consumed token, treat the line break as an implicit
        // ';' — exit the infix loop so the partial expression becomes a
        // complete statement.
        //
        // This prevents the silent吞噬 described in
        // VORA_SYNTAX_REVIEW.md P0 #1 — examples like
        //     let b = a
        //     -1
        // no longer collapse to `let b = (a - 1)`. Instead the parser
        // sees `let b = a` followed by `-1` (a unary-minus expression
        // statement).
        // ============================================================
        if (asiDepth_ > 0 && parenDepth_ == 0 &&
            peek().line > previous().line) {
            break;
        }

        Token op = advance();
        int opPrec = getPrecedence(op.type);

        // =========================
        // TERNARY (precedence 1, right-associative)
        // =========================
        if (op.type == TokenType::QUESTION) {
            auto thenBranch = parsePrecedence(0);
            if (!thenBranch) {
                thenBranch = std::make_unique<ErrorExpr>("Expected expression after '?'", op);
            }
            if (!match(TokenType::COLON)) {
                error("Expected ':' in ternary expression");
                // Return partial ternary with ErrorExpr in else position.
                left = std::make_unique<TernaryExpr>(
                    std::move(left),
                    std::move(thenBranch),
                    std::make_unique<ErrorExpr>("Missing ':' in ternary", op)
                );
                continue;
            }
            // Right-associative: else-branch parsed at opPrec-1 so nested
            // ternaries in the else position bind to the right.
            auto elseBranch = parsePrecedence(opPrec - 1);
            if (!elseBranch) {
                elseBranch = std::make_unique<ErrorExpr>("Expected expression after ':'", previous());
            }
            left = std::make_unique<TernaryExpr>(
                std::move(left),
                std::move(thenBranch),
                std::move(elseBranch)
            );
            continue;
        }

        // right-associativity fix (power + assignment / compound-assignment)
        bool rightAssociative =
            (op.type == TokenType::POWER ||
             op.type == TokenType::EQUAL ||
             op.type == TokenType::PLUS_EQUAL ||
             op.type == TokenType::MINUS_EQUAL ||
             op.type == TokenType::MULTIPLY_EQUAL ||
             op.type == TokenType::DIVIDE_EQUAL ||
             op.type == TokenType::MODULO_EQUAL ||
             op.type == TokenType::POWER_EQUAL);

        int nextPrec = opPrec - (rightAssociative ? 1 : 0);

        auto right = parsePrecedence(nextPrec);

        if (!right) {
            right = std::make_unique<ErrorExpr>("Expected expression after operator", op);
        }

        // =========================
        // ASSIGNMENT (Pratt unified)
        // =========================
        if (op.type == TokenType::EQUAL) {

            auto variable =
                dynamic_cast<VariableExpr*>(left.get());

            if (variable) {
                return std::make_unique<AssignmentExpr>(
                    variable->name,
                    std::move(right),
                    variable->nameToken
                );
            }

            auto property =
                dynamic_cast<PropertyExpr*>(left.get());

            if (property) {
                return std::make_unique<PropertyAssignmentExpr>(
                    std::move(property->object),
                    property->property,
                    std::move(right),
                    property->dot
                );
            }

            auto indexExpr =
                dynamic_cast<IndexExpr*>(left.get());

            if (indexExpr) {
                return std::make_unique<IndexAssignmentExpr>(
                    std::move(indexExpr->array),
                    std::move(indexExpr->index),
                    std::move(right),
                    indexExpr->bracket
                );
            }

            // Destructuring assignment: [a, b] = arr  or  {x, y} = obj
            auto arrayLhs = dynamic_cast<ArrayExpr*>(left.get());
            if (arrayLhs) {
                auto binding = convertArrayExprToBinding(*arrayLhs);
                if (!binding) {
                    error("Invalid destructuring pattern — expected variable names");
                    return right;
                }
                return std::make_unique<DestructureAssignmentExpr>(
                    std::move(binding), std::move(right));
            }

            auto dictLhs = dynamic_cast<DictExpr*>(left.get());
            if (dictLhs) {
                auto binding = convertDictExprToBinding(*dictLhs);
                if (!binding) {
                    error("Invalid destructuring pattern — expected variable names");
                    return right;
                }
                return std::make_unique<DestructureAssignmentExpr>(
                    std::move(binding), std::move(right));
            }

            // Invalid assignment target — report error but return the
            // right-hand side value so the AST retains something useful.
            error("Invalid assignment target");
            return right;
        }

        // =========================
        // COMPOUND ASSIGNMENT (+=, -=, *=, /=, %=, **=)
        // Preserved as first-class AST node (not desugared).
        // =========================
        if (op.type == TokenType::PLUS_EQUAL ||
            op.type == TokenType::MINUS_EQUAL ||
            op.type == TokenType::MULTIPLY_EQUAL ||
            op.type == TokenType::DIVIDE_EQUAL ||
            op.type == TokenType::MODULO_EQUAL ||
            op.type == TokenType::POWER_EQUAL) {

            // Validate target: VariableExpr, PropertyExpr, or IndexExpr
            if (dynamic_cast<VariableExpr*>(left.get()) ||
                dynamic_cast<PropertyExpr*>(left.get()) ||
                dynamic_cast<IndexExpr*>(left.get())) {
                return std::make_unique<CompoundAssignmentExpr>(
                    std::move(left),
                    op,
                    std::move(right)
                );
            }

            // Invalid target — report error but return right-hand side.
            error("Invalid target for compound assignment");
            return right;
        }

        // =========================
        // NORMAL BINARY
        // =========================
        left = std::make_unique<BinaryExpr>(
            std::move(left),
            op,
            std::move(right)
        );
    }

    return left;
}

// =========================
// BREAK / CONTINUE
// =========================

// `break` / `continue` may name their target loop. The identifier counts as a
// label only when it is on the *same line* as the keyword — matching the
// Go-style ASI rule used elsewhere (see the newline test in parsePrecedence), so
//
//     break
//     foo()
//
// stays two statements rather than becoming `break foo`.
std::unique_ptr<Stmt> Parser::breakStatement() {
    Token keyword = previous();
    std::string targetLabel;
    if (check(TokenType::IDENTIFIER) && peek().line == keyword.line) {
        targetLabel = advance().lexeme;
    }
    auto stmt = std::make_unique<BreakStmt>(keyword, targetLabel);
    match(TokenType::SEMICOLON);  // optional — `break` and `break;` are both valid
    return stmt;
}

std::unique_ptr<Stmt> Parser::continueStatement() {
    Token keyword = previous();
    std::string targetLabel;
    if (check(TokenType::IDENTIFIER) && peek().line == keyword.line) {
        targetLabel = advance().lexeme;
    }
    auto stmt = std::make_unique<ContinueStmt>(keyword, targetLabel);
    match(TokenType::SEMICOLON);  // optional — `continue` and `continue;` are both valid
    return stmt;
}

std::unique_ptr<Stmt> Parser::tryStatement() {
    Token tryToken = previous();  // the 'try' keyword

    std::unique_ptr<Stmt> tryBlock;
    if (!match(TokenType::LEFT_BRACE)) {
        error("Expected '{' after 'try'");
        tryBlock = std::make_unique<ErrorStmt>("Expected block after 'try'", tryToken);
    } else {
        tryBlock = blockStatement();
        // blockStatement() now never returns nullptr (returns partial block on error)
    }

    std::string catchVar;
    std::unique_ptr<Stmt> catchBlock;

    if (match(TokenType::CATCH)) {
        if (!match(TokenType::LEFT_PAREN)) {
            error("Expected '(' after 'catch'");
            // Continue anyway
        }

        if (!match(TokenType::IDENTIFIER)) {
            error("Expected catch variable name");
            catchVar = "?error?";
        } else {
            catchVar = previous().lexeme;
        }

        if (!match(TokenType::RIGHT_PAREN)) {
            error("Expected ')' after catch variable");
            // Continue anyway
        }

        if (!match(TokenType::LEFT_BRACE)) {
            error("Expected '{' after catch clause");
            catchBlock = std::make_unique<ErrorStmt>("Expected block for catch", peek());
        } else {
            catchBlock = blockStatement();
        }
    }

    std::unique_ptr<Stmt> finallyBlock;

    if (match(TokenType::FINALLY)) {
        if (!match(TokenType::LEFT_BRACE)) {
            error("Expected '{' after 'finally'");
            finallyBlock = std::make_unique<ErrorStmt>("Expected block for finally", peek());
        } else {
            finallyBlock = blockStatement();
        }
    }

    if (!catchBlock && !finallyBlock) {
        error("Expected 'catch' or 'finally' after 'try'");
        // Still return a partial TryStmt with just the try block.
    }

    return std::make_unique<TryStmt>(
        std::move(tryBlock),
        std::move(catchVar),
        std::move(catchBlock),
        std::move(finallyBlock)
    );
}

std::unique_ptr<Stmt> Parser::throwStatement() {
    Token keyword = previous();  // the 'throw' keyword

    auto value = expression();
    if (!value) {
        value = std::make_unique<ErrorExpr>("Expected expression after 'throw'", keyword);
    }

    return std::make_unique<ThrowStmt>(
        std::move(value),
        keyword
    );
}

// =========================
// IMPORT / EXPORT
// =========================

std::unique_ptr<Stmt> Parser::importStatement() {
    Token keyword = previous();  // 'import' token

    if (!match(TokenType::STRING)) {
        return errorStmt("Expected string path after 'import'");
    }

    std::string modulePath = previous().lexeme;

    // Determine the binding name.
    //  - `import "path" as alias`  →  alias
    //  - `import "path"`            →  basename(path)
    //
    // P0 #4 fix: when 'as' is omitted the basename must be a legal Vora
    // identifier — otherwise the import cannot be referenced by name (the
    // user wrote `import "foo-bar"` then accessed `foo-bar.V`, which the
    // parser split into `foo - bar.V` and silently swallowed the binding).
    // Either an explicit `as <name>` or `from "path" import <name>` lets
    // the user pick a valid binding; we surface a precise parser error
    // here instead of letting the runtime / LSP try to use the raw path
    // as a binding name.
    std::string alias;
    if (match(TokenType::AS)) {
        if (!match(TokenType::IDENTIFIER)) {
            return errorStmt("Expected identifier after 'as'");
        }
        alias = previous().lexeme;
    } else {
        std::string derived = deriveImportBaseName(modulePath);
        if (!isLegalIdentifier(derived)) {
            return errorStmt(
                "Cannot derive a binding name from module path \"" + modulePath +
                "\"; use `import \"" + modulePath + "\" as <name>` or "
                "`from \"" + modulePath + "\" import <name1>, ...` instead."
            );
        }
        alias = derived;
    }

    match(TokenType::SEMICOLON);  // optional

    return std::make_unique<ImportStmt>(modulePath, keyword, alias);
}

std::string Parser::deriveImportBaseName(const std::string& modulePath) {
    // Take everything after the last '/' or '\\' (whichever comes last).
    size_t lastSlash     = modulePath.find_last_of('/');
    size_t lastBackslash = modulePath.find_last_of('\\');
    size_t sep =
        (lastSlash == std::string::npos) ? lastBackslash :
        (lastBackslash == std::string::npos) ? lastSlash :
        (lastSlash > lastBackslash ? lastSlash : lastBackslash);
    std::string base = (sep == std::string::npos)
        ? modulePath
        : modulePath.substr(sep + 1);

    // Strip a trailing ".va" extension (case-sensitive — Vora file names
    // are lowercase by convention).
    if (base.size() > 3 &&
        base.compare(base.size() - 3, 3, ".va") == 0) {
        base.resize(base.size() - 3);
    }

    return base;
}

bool Parser::isLegalIdentifier(const std::string& name) {
    if (name.empty()) return false;
    char first = name[0];
    if (!(std::isalpha(static_cast<unsigned char>(first)) || first == '_')) {
        return false;
    }
    for (size_t i = 1; i < name.size(); i++) {
        char c = name[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            return false;
        }
    }
    return true;
}

std::unique_ptr<Stmt> Parser::fromImportStatement() {
    Token keyword = previous();  // 'from' token

    if (!match(TokenType::STRING)) {
        return errorStmt("Expected string path after 'from'");
    }

    std::string modulePath = previous().lexeme;

    if (!match(TokenType::IMPORT)) {
        return errorStmt("Expected 'import' after path in from-import");
    }

    // Parse comma-separated identifier list
    std::vector<std::string> names;
    do {
        if (!match(TokenType::IDENTIFIER)) {
            return errorStmt("Expected identifier in import list");
        }
        names.push_back(previous().lexeme);
    } while (match(TokenType::COMMA));

    if (names.empty()) {
        return errorStmt("Expected at least one identifier in from-import");
    }

    match(TokenType::SEMICOLON);  // optional

    return std::make_unique<ImportStmt>(modulePath, keyword, "", names);
}

std::unique_ptr<Stmt> Parser::exportStatement() {
    Token keyword = previous();  // 'export' token

    if (match(TokenType::FUNC)) {
        // export func name(...) { ... }
        if (check(TokenType::LEFT_PAREN)) {
            return errorStmt("Cannot export anonymous function");
        }
        auto stmt = funcStatement();
        if (!stmt) return errorStmt("Failed to parse exported function");
        return std::make_unique<ExportStmt>(std::move(stmt), keyword);
    }

    if (match(TokenType::LET)) {
        auto stmt = letStatement();
        if (!stmt) return errorStmt("Failed to parse exported let declaration");
        return std::make_unique<ExportStmt>(std::move(stmt), keyword);
    }

    if (match(TokenType::CONST)) {
        auto stmt = constStatement();
        if (!stmt) return errorStmt("Failed to parse exported const declaration");
        return std::make_unique<ExportStmt>(std::move(stmt), keyword);
    }

    if (match(TokenType::OBJ)) {
        auto stmt = objStatement();
        if (!stmt) return errorStmt("Failed to parse exported object");
        return std::make_unique<ExportStmt>(std::move(stmt), keyword);
    }

    return errorStmt("Expected func, let, const, or Obj after 'export'");
}

std::unique_ptr<Stmt> Parser::deferStatement() {
    Token keyword = previous();  // 'defer' token

    auto expr = expression();
    if (!expr) {
        return errorStmt("Expected expression after 'defer'");
    }

    match(TokenType::SEMICOLON);

    return std::make_unique<DeferStmt>(std::move(expr), keyword);
}

}
