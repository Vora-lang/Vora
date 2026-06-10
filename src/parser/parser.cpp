#include "parser.h"
#include "../ast/stmt.h"
#include <iostream>

namespace vora {

Parser::Parser(std::vector<Token> tokens)
    : tokens(std::move(tokens)) {
}

std::unique_ptr<Program> Parser::parse() {

    std::vector<std::unique_ptr<Stmt>> statements;

    while (!isAtEnd()) {

        auto stmt = statement();

        if (!stmt) {
            // Synchronize and keep going so we report all errors,
            // not just the first one.
            synchronize();
            continue;
        }

        // Skip statements whose expression failed to parse (ExprStmt
        // with a null inner expression is a parse error marker).
        if (auto* es = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (!es->expression) {
                synchronize();
                continue;
            }
        }

        statements.push_back(
            std::move(stmt)
        );
    }

    if (hadError) {
        return nullptr;
    }

    return std::make_unique<Program>(
        std::move(statements)
    );
}

void Parser::error(const std::string& message) {
    hadError = true;
    std::cerr << "[line " << peek().line << "] Error: " << message << "\n";
}

void Parser::synchronize() {
    // Skip tokens until we hit a likely statement boundary.
    while (!isAtEnd()) {
        // If we just passed a semicolon or closing brace, we're at a boundary.
        if (previous().type == TokenType::SEMICOLON ||
            previous().type == TokenType::RIGHT_BRACE) {
            return;
        }

        // Statement-start keywords are safe resync points.
        switch (peek().type) {
            case TokenType::LET:
            case TokenType::FUNC:
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::FOR:
            case TokenType::RETURN:
            case TokenType::OBJ:
            case TokenType::BREAK:
            case TokenType::CONTINUE:
            case TokenType::TRY:
                return;
            default:
                break;
        }

        advance();
    }
}

// =========================
// ENTRY
// =========================

std::unique_ptr<Expr> Parser::expression() {
    // Pratt root: assignment is handled inside Pratt
    return parsePrecedence(0);
}

std::unique_ptr<Stmt> Parser::statement() {

    if (match(TokenType::LET)) {
        return letStatement();
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

    if (match(TokenType::FOR)) {
        return forStatement();
    }

    if (match(TokenType::FUNC)) {
        return funcStatement();
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

    auto expr = expression();

    match(TokenType::SEMICOLON);

    return std::make_unique<ExprStmt>(
        std::move(expr)
    );
}

std::unique_ptr<Stmt> Parser::letStatement() {

    if (!match(TokenType::IDENTIFIER)) {
        error("Expected variable name");
        return nullptr;
    }

    std::string name = previous().lexeme;

    // Optional type annotation: let x:int = ...
    std::string typeAnnotation;
    if (match(TokenType::COLON)) {
        if (!match(TokenType::IDENTIFIER)) {
            error("Expected type name after ':'");
            return nullptr;
        }
        typeAnnotation = previous().lexeme;
    }

    if (!match(TokenType::EQUAL)) {
        error("Expected '=' after variable name");
        return nullptr;
    }

    auto initializer = expression();

    match(TokenType::SEMICOLON);

    return std::make_unique<LetStmt>(
        name,
        std::move(initializer),
        typeAnnotation
    );
}

std::unique_ptr<BlockStmt> Parser::blockStatement() {

    std::vector<std::unique_ptr<Stmt>> statements;

    while (!isAtEnd() &&
           peek().type != TokenType::RIGHT_BRACE) {

        auto stmt = statement();

        if (!stmt) {
            return nullptr;
        }

        statements.push_back(
            std::move(stmt)
        );
    }

    if (!match(TokenType::RIGHT_BRACE)) {

                    error("Expected '}' after block\n");

        return nullptr;
    }

    return std::make_unique<BlockStmt>(
        std::move(statements)
    );
}

std::unique_ptr<Stmt> Parser::returnStatement() {

    auto value = expression();

    match(TokenType::SEMICOLON);

    return std::make_unique<ReturnStmt>(
        std::move(value)
    );
}

std::unique_ptr<Stmt> Parser::ifStatement() {

    if (!match(TokenType::LEFT_PAREN)) {

                    error("Expected '(' after if\n");

        return nullptr;
    }

    auto condition = expression();

    if (!match(TokenType::RIGHT_PAREN)) {

                    error("Expected ')' after condition\n");

        return nullptr;
    }

    auto thenBranch = statement();

    std::unique_ptr<Stmt> elseBranch;

    if (match(TokenType::ELSE)) {
        elseBranch = statement();
    }

    return std::make_unique<IfStmt>(
        std::move(condition),
        std::move(thenBranch),
        std::move(elseBranch)
    );
}

std::unique_ptr<Stmt> Parser::forStatement() {

    Token forToken = previous();

    if (!match(TokenType::IDENTIFIER)) {
                    error("Expected loop variable name after 'for'\n");
        return nullptr;
    }

    std::string variable = previous().lexeme;

    if (!match(TokenType::IN)) {
                    error("Expected 'in' after loop variable\n");
        return nullptr;
    }

    auto iterable = expression();

    if (!match(TokenType::LEFT_BRACE)) {
                    error("Expected '{' before for loop body\n");
        return nullptr;
    }

    auto body = blockStatement();

    if (!body) {
        return nullptr;
    }

    return std::make_unique<ForStmt>(
        std::move(variable),
        std::move(iterable),
        std::move(body),
        forToken
    );
}

std::unique_ptr<Stmt> Parser::funcStatement() {

    if (!match(TokenType::IDENTIFIER)) {

                    error("Expected function name\n");

        return nullptr;
    }

    std::string name = previous().lexeme;

    if (!match(TokenType::LEFT_PAREN)) {

                    error("Expected '(' after function name\n");

        return nullptr;
    }

    std::vector<std::string> params;

    if (!check(TokenType::RIGHT_PAREN)) {

        do {

            if (!match(TokenType::IDENTIFIER)) {

                            error("Expected parameter name\n");

                return nullptr;
            }

            params.push_back(
                previous().lexeme
            );
        }
        while (match(TokenType::COMMA));
    }

    if (!match(TokenType::RIGHT_PAREN)) {

                    error("Expected ')' after parameters\n");

        return nullptr;
    }

    if (!match(TokenType::LEFT_BRACE)) {

                    error("Expected '{' before function body\n");

        return nullptr;
    }

    auto body = blockStatement();

    if (!body) {
        return nullptr;
    }

    return std::make_unique<FuncStmt>(
        name,
        std::move(params),
        std::shared_ptr<BlockStmt>(std::move(body))
    );
}

std::unique_ptr<Stmt> Parser::objStatement() {

    if (!match(TokenType::IDENTIFIER)) {

                    error("Expected object name\n");

        return nullptr;
    }

    std::string name = previous().lexeme;

    // Optional inheritance: Obj Child : Parent (params) { ... }
    std::string parentName;
    if (match(TokenType::COLON)) {
        if (!match(TokenType::IDENTIFIER)) {
            error("Expected parent class name after ':'\n");
            return nullptr;
        }
        parentName = previous().lexeme;
    }

    if (!match(TokenType::LEFT_PAREN)) {

                    error("Expected '(' after object name\n");

        return nullptr;
    }

    std::vector<std::string> params;

    if (!check(TokenType::RIGHT_PAREN)) {

        do {

            if (!match(TokenType::IDENTIFIER)) {

                            error("Expected parameter name\n");

                return nullptr;
            }

            params.push_back(
                previous().lexeme
            );
        }
        while (match(TokenType::COMMA));
    }

    if (!match(TokenType::RIGHT_PAREN)) {

                    error("Expected ')' after parameters\n");

        return nullptr;
    }

    if (!match(TokenType::LEFT_BRACE)) {

                    error("Expected '{' before object body\n");

        return nullptr;
    }

    // Object body members are classified by statement type:
    // - FuncStmt nodes (from `func` keyword) → methods vector
    // - All other statements → constructor body (bodyStmts)
    // This convention means that `func` declarations inside an obj
    // are automatically treated as methods, while `let`, `if`,
    // `while`, expression-statements, etc. run in the constructor.
    // The separation relies on the parser dispatch in statement():
    // `func` → funcStatement() returns FuncStmt; everything else
    // returns a different Stmt subclass.
    std::vector<std::unique_ptr<Stmt>> methods;
    std::vector<std::unique_ptr<Stmt>> bodyStmts;

    while (!isAtEnd() &&
           peek().type != TokenType::RIGHT_BRACE) {

        auto stmt = statement();

        if (!stmt) {
            return nullptr;
        }

        if (dynamic_cast<FuncStmt*>(stmt.get())) {
            methods.push_back(std::move(stmt));
        } else {
            bodyStmts.push_back(std::move(stmt));
        }
    }

    if (!match(TokenType::RIGHT_BRACE)) {

                    error("Expected '}' after object body\n");

        return nullptr;
    }

    auto body = std::make_shared<BlockStmt>(
        std::move(bodyStmts)
    );

    return std::make_unique<ObjStmt>(
        name,
        parentName,
        std::move(params),
        std::move(methods),
        body
    );
}

std::unique_ptr<Stmt> Parser::whileStatement() {

    if (!match(TokenType::LEFT_PAREN)) {

                    error("Expected '(' after while\n");

        return nullptr;
    }

    auto condition = expression();

    if (!match(TokenType::RIGHT_PAREN)) {

                    error("Expected ')' after condition\n");

        return nullptr;
    }

    auto body = statement();

    return std::make_unique<WhileStmt>(
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
            return 1; // lowest, right-associative handled in Pratt

        case TokenType::OR:
            return 1;

        case TokenType::AND:
            return 2;

        case TokenType::QUESTION:
            return 2;

        case TokenType::EQUAL_EQUAL:
        case TokenType::NOT_EQUAL:
            return 3;

        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
            return 4;

        case TokenType::PLUS:
        case TokenType::MINUS:
            return 5;

        case TokenType::MULTIPLY:
        case TokenType::DIVIDE:
        case TokenType::MODULO:
            return 6;

        case TokenType::POWER:
            return 7;

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
        auto right = primary();

        if (!right) return nullptr;

        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    if (match(TokenType::NOT)) {
        Token op = previous();
        auto right = primary();

        if (!right) return nullptr;

        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    // prefix ++x / --x
    if (match(TokenType::PLUS_PLUS) || match(TokenType::MINUS_MINUS)) {
        Token op = previous();
        auto target = call();  // use call() so ++obj.prop chains work

        if (!target) return nullptr;

        // target must be assignable
        if (!dynamic_cast<VariableExpr*>(target.get()) &&
            !dynamic_cast<PropertyExpr*>(target.get())) {
            error("Invalid target for prefix " + op.lexeme);
            return nullptr;
        }

        return std::make_unique<IncDecExpr>(op, std::move(target), true);
    }

    if (match(TokenType::THIS)) {
        return std::make_unique<ThisExpr>(
            previous()
        );
    }

    if (match(TokenType::NUMBER)) {
        const std::string& lexeme = previous().lexeme;

        // Handle hex, octal, binary prefixes — always integer
        if (lexeme.size() >= 2 && lexeme[0] == '0') {
            switch (lexeme[1]) {
                case 'x': case 'X':
                    return std::make_unique<LiteralExpr>(
                        static_cast<int64_t>(std::stoull(lexeme, nullptr, 16))
                    );
                case 'o': case 'O':
                    return std::make_unique<LiteralExpr>(
                        static_cast<int64_t>(std::stoull(lexeme.substr(2), nullptr, 8))
                    );
                case 'b': case 'B':
                    return std::make_unique<LiteralExpr>(
                        static_cast<int64_t>(std::stoull(lexeme.substr(2), nullptr, 2))
                    );
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
            previous().lexeme
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

        std::vector<std::unique_ptr<Expr>> elements;

        if (!check(TokenType::RIGHT_BRACKET)) {
            do {
                auto element = expression();

                if (!element) {
                    return nullptr;
                }

                elements.push_back(std::move(element));
            }
            while (match(TokenType::COMMA));
        }

        if (!match(TokenType::RIGHT_BRACKET)) {
            error("Expected ']' after array literal");
            return nullptr;
        }

        return std::make_unique<ArrayExpr>(
            std::move(elements),
            leftBracket
        );
    }

    if (match(TokenType::LEFT_PAREN)) {
        auto expr = expression();

        if (!match(TokenType::RIGHT_PAREN)) {
            error("Expected ')' after expression");
            return nullptr;
        }

        return std::make_unique<GroupingExpr>(std::move(expr));
    }

    error("Unexpected token: " + peek().lexeme);
    advance();  // consume the bad token so we don't loop on it
    return std::make_unique<LiteralExpr>(nullptr);  // placeholder
}

std::unique_ptr<Expr> Parser::finishCall(
    std::unique_ptr<Expr> callee
) {

    std::vector<std::unique_ptr<Expr>> arguments;

    if (!check(TokenType::RIGHT_PAREN)) {

        do {

            auto argument = expression();

            if (!argument) {
                return nullptr;
            }

            arguments.push_back(
                std::move(argument)
            );
        }
        while (match(TokenType::COMMA));
    }

    if (!match(TokenType::RIGHT_PAREN)) {

        error("Expected ')' after arguments");

        return nullptr;
    }

    return std::make_unique<CallExpr>(
        std::move(callee),
        std::move(arguments),
        previous()
    );
}

std::unique_ptr<Expr> Parser::call() {

    auto expr = primary();

    if (!expr) {
        return nullptr;
    }

    while (true) {

        if (match(TokenType::LEFT_PAREN)) {

            expr = finishCall(
                std::move(expr)
            );

            if (!expr) {
                return nullptr;
            }

            continue;
        }

        if (match(TokenType::LEFT_BRACKET)) {

            auto indexExpr = expression();

            if (!indexExpr) {
                return nullptr;
            }

            if (!match(TokenType::RIGHT_BRACKET)) {
                error("Expected ']' after index expression");
                return nullptr;
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
                return nullptr;
            }

            std::string property = previous().lexeme;

            expr = std::make_unique<PropertyExpr>(
                std::move(expr),
                property,
                dot
            );
            continue;
        }

        // postfix x++ / x--  (only if lhs is an lvalue, otherwise
        // the ++/-- belongs to the next statement as a prefix operator)
        if ((check(TokenType::PLUS_PLUS) || check(TokenType::MINUS_MINUS)) &&
            (dynamic_cast<VariableExpr*>(expr.get()) ||
             dynamic_cast<PropertyExpr*>(expr.get()))) {
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
        error("Expected expression");
        return std::make_unique<LiteralExpr>(nullptr);  // placeholder
    }

    while (!isAtEnd() &&
           precedence < getPrecedence(peek().type)) {

        Token op = advance();
        int opPrec = getPrecedence(op.type);

        // =========================
        // TERNARY (precedence 2, right-associative)
        // =========================
        if (op.type == TokenType::QUESTION) {
            auto thenBranch = parsePrecedence(0);
            if (!thenBranch) {
                error("Expected expression after '?'");
                return nullptr;
            }
            if (!match(TokenType::COLON)) {
                error("Expected ':' in ternary expression");
                return nullptr;
            }
            // Right-associative: else-branch parsed at opPrec-1 so nested
            // ternaries in the else position bind to the right.
            auto elseBranch = parsePrecedence(opPrec - 1);
            if (!elseBranch) {
                error("Expected expression after ':'");
                return nullptr;
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
             op.type == TokenType::MODULO_EQUAL);

        int nextPrec = opPrec - (rightAssociative ? 1 : 0);

        auto right = parsePrecedence(nextPrec);

        if (!right) {
            error("Expected right-hand expression after operator");
            return nullptr;
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

            error("Invalid assignment target");
            return nullptr;
        }

        // =========================
        // COMPOUND ASSIGNMENT (+=, -=, *=, /=, %=)
        // Desugar: x += y  →  x = x + y
        // =========================
        if (op.type == TokenType::PLUS_EQUAL ||
            op.type == TokenType::MINUS_EQUAL ||
            op.type == TokenType::MULTIPLY_EQUAL ||
            op.type == TokenType::DIVIDE_EQUAL ||
            op.type == TokenType::MODULO_EQUAL) {

            // Map compound op to base op
            TokenType baseOp;
            switch (op.type) {
                case TokenType::PLUS_EQUAL:     baseOp = TokenType::PLUS;     break;
                case TokenType::MINUS_EQUAL:    baseOp = TokenType::MINUS;    break;
                case TokenType::MULTIPLY_EQUAL: baseOp = TokenType::MULTIPLY; break;
                case TokenType::DIVIDE_EQUAL:   baseOp = TokenType::DIVIDE;   break;
                case TokenType::MODULO_EQUAL:   baseOp = TokenType::MODULO;   break;
                default: break;
            }

            Token baseToken(baseOp, op.lexeme.substr(0, 1), op.line);

            // Build the binary: left op right
            auto bin = std::make_unique<BinaryExpr>(
                std::unique_ptr<Expr>(left->clone()),  // clone left for the read
                baseToken,
                std::move(right)
            );

            // Reuse the same assignment logic
            auto variable = dynamic_cast<VariableExpr*>(left.get());
            if (variable) {
                return std::make_unique<AssignmentExpr>(
                    variable->name,
                    std::move(bin),
                    variable->nameToken
                );
            }

            auto property = dynamic_cast<PropertyExpr*>(left.get());
            if (property) {
                return std::make_unique<PropertyAssignmentExpr>(
                    std::unique_ptr<Expr>(property->object->clone()),
                    property->property,
                    std::move(bin),
                    property->dot
                );
            }

            error("Invalid target for compound assignment");
            return nullptr;
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

std::unique_ptr<Stmt> Parser::breakStatement() {
    return std::make_unique<BreakStmt>(previous());
}

std::unique_ptr<Stmt> Parser::continueStatement() {
    return std::make_unique<ContinueStmt>(previous());
}

std::unique_ptr<Stmt> Parser::tryStatement() {
    Token tryToken = previous();  // the 'try' keyword

    if (!match(TokenType::LEFT_BRACE)) {
        error("Expected '{' after 'try'");
        return nullptr;
    }

    auto tryBlock = blockStatement();
    if (!tryBlock) {
        return nullptr;
    }

    std::string catchVar;
    std::unique_ptr<Stmt> catchBlock;

    if (match(TokenType::CATCH)) {
        if (!match(TokenType::LEFT_PAREN)) {
            error("Expected '(' after 'catch'");
            return nullptr;
        }

        if (!match(TokenType::IDENTIFIER)) {
            error("Expected catch variable name");
            return nullptr;
        }

        catchVar = previous().lexeme;

        if (!match(TokenType::RIGHT_PAREN)) {
            error("Expected ')' after catch variable");
            return nullptr;
        }

        if (!match(TokenType::LEFT_BRACE)) {
            error("Expected '{' after catch clause");
            return nullptr;
        }

        catchBlock = blockStatement();
        if (!catchBlock) {
            return nullptr;
        }
    }

    std::unique_ptr<Stmt> finallyBlock;

    if (match(TokenType::FINALLY)) {
        if (!match(TokenType::LEFT_BRACE)) {
            error("Expected '{' after 'finally'");
            return nullptr;
        }

        finallyBlock = blockStatement();
        if (!finallyBlock) {
            return nullptr;
        }
    }

    if (!catchBlock && !finallyBlock) {
        error("Expected 'catch' or 'finally' after 'try'");
        return nullptr;
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
        return nullptr;
    }

    return std::make_unique<ThrowStmt>(
        std::move(value),
        keyword
    );
}

}
