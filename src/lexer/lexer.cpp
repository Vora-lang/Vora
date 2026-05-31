#include "lexer.h"

#include <cctype>

namespace vora {

    const std::unordered_map<std::string, TokenType> Lexer::keywords = {
        {"let", TokenType::LET},
        {"func", TokenType::FUNC},
        {"return", TokenType::RETURN},
        {"if", TokenType::IF},
        {"else", TokenType::ELSE},
        {"while", TokenType::WHILE},
        {"for", TokenType::FOR},
        {"in", TokenType::IN},
        {"Obj", TokenType::OBJ},
        {"this", TokenType::THIS},
        {"break", TokenType::BREAK},
        {"continue", TokenType::CONTINUE},
        {"try", TokenType::TRY},
        {"catch", TokenType::CATCH},
        {"finally", TokenType::FINALLY},
        {"throw", TokenType::THROW},
        {"true", TokenType::TRUE},
        {"false", TokenType::FALSE},
        {"null", TokenType::NULL_TOKEN},
        {"and", TokenType::AND},
        {"or", TokenType::OR},
    };

    Lexer::Lexer(std::string source)
        : source(std::move(source)) {
    }

    std::vector<Token> Lexer::scanTokens() {
        while (!isAtEnd()) {
            start = current;
            scanToken();
        }

        tokens.emplace_back(TokenType::END_OF_FILE, "", line);
        return tokens;
    }

    bool Lexer::isAtEnd() const {
        return current >= source.length();
    }

    bool Lexer::isAlpha(char c) const {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
            static_cast<unsigned char>(c) > 127;  // Unicode (UTF-8 multi-byte)
    }

    bool Lexer::isAlphaNumeric(char c) const {
        return isAlpha(c) ||
            std::isdigit(static_cast<unsigned char>(c));
    }

    char Lexer::advance() {
        return source[current++];
    }

    char Lexer::peek() const {
        if (isAtEnd()) return '\0';
        return source[current];
    }

    char Lexer::peekNext() const {
        if (current + 1 >= source.length()) {
            return '\0';
        }
        return source[current + 1];
    }

    bool Lexer::match(char expected) {
        if (isAtEnd()) return false;
        if (source[current] != expected) return false;

        current++;
        return true;
    }

    void Lexer::addToken(TokenType type) {
        std::string text = source.substr(start, current - start);
        tokens.emplace_back(type, text, line);
    }

    void Lexer::number() {
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }

        // Handle decimal point
        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
            advance(); // consume the '.'
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        addToken(TokenType::NUMBER);
    }

    void Lexer::identifier() {
        while (isAlphaNumeric(peek())) {
            advance();
        }

        std::string text =
            source.substr(start, current - start);

        auto it = keywords.find(text);

        if (it != keywords.end()) {
            addToken(it->second);
        }
        else {
            addToken(TokenType::IDENTIFIER);
        }
    }

    void Lexer::string(char delimiter) {
        std::string value;

        while (peek() != delimiter && !isAtEnd()) {
            if (peek() == '\n') {
                line++;
            }

            char c = advance();

            if (c == '\\') {
                if (isAtEnd()) {
                    addToken(TokenType::INVALID);
                    return;
                }

                char escaped = advance();
                switch (escaped) {
                    case 'n':  value += '\n'; break;
                    case 't':  value += '\t'; break;
                    case 'r':  value += '\r'; break;
                    case '0':  value += '\0'; break;
                    case '\\': value += '\\'; break;
                    case '"':  value += '"';  break;
                    case '\'': value += '\''; break;
                    default:
                        // Unrecognized escape: keep both characters
                        value += '\\';
                        value += escaped;
                        break;
                }
            } else {
                value += c;
            }
        }

        if (isAtEnd()) {
            addToken(TokenType::INVALID);
            return;
        }

        // closing quote
        advance();

        tokens.emplace_back(TokenType::STRING, value, line);
    }

    void Lexer::scanToken() {
        char c = advance();

        switch (c) {

        case '(':
            addToken(TokenType::LEFT_PAREN);
            break;

        case ')':
            addToken(TokenType::RIGHT_PAREN);
            break;

        case '{':
            addToken(TokenType::LEFT_BRACE);
            break;

        case '}':
            addToken(TokenType::RIGHT_BRACE);
            break;

        case '[':
            addToken(TokenType::LEFT_BRACKET);
            break;

        case ']':
            addToken(TokenType::RIGHT_BRACKET);
            break;

        case ',':
            addToken(TokenType::COMMA);
            break;

        case '.':
            addToken(TokenType::DOT);
            break;

        case ':':
            addToken(TokenType::COLON);
            break;

        case '?':
            addToken(TokenType::QUESTION);
            break;

        case ';':
            addToken(TokenType::SEMICOLON);
            break;

        case '+':
            if (match('+')) {
                addToken(TokenType::PLUS_PLUS);
            }
            else if (match('=')) {
                addToken(TokenType::PLUS_EQUAL);
            }
            else {
                addToken(TokenType::PLUS);
            }
            break;

        case '-':
            if (match('-')) {
                addToken(TokenType::MINUS_MINUS);
            }
            else if (match('=')) {
                addToken(TokenType::MINUS_EQUAL);
            }
            else {
                addToken(TokenType::MINUS);
            }
            break;

        case '*':
            if (match('*')) {
                addToken(TokenType::POWER);
            }
            else if (match('=')) {
                addToken(TokenType::MULTIPLY_EQUAL);
            }
            else {
                addToken(TokenType::MULTIPLY);
            }
            break;

        case '/':
            if (match('/')) {
                lineComment();
            }
            else if (match('*')) {
                blockComment();
            }
            else if (match('=')) {
                addToken(TokenType::DIVIDE_EQUAL);
            }
            else {
                addToken(TokenType::DIVIDE);
            }
            break;

        case '%':
            if (match('=')) {
                addToken(TokenType::MODULO_EQUAL);
            }
            else {
                addToken(TokenType::MODULO);
            }
            break;

        case '=':
            if (match('=')) {
                addToken(TokenType::EQUAL_EQUAL);
            }
            else {
                addToken(TokenType::EQUAL);
            }
            break;

        case '<':
            if (match('=')) {
                addToken(TokenType::LESS_EQUAL);
            }
            else {
                addToken(TokenType::LESS);
            }
            break;

        case '>':
            if (match('=')) {
                addToken(TokenType::GREATER_EQUAL);
            }
            else {
                addToken(TokenType::GREATER);
            }
            break;

        case '!':
            if (match('=')) {
                addToken(TokenType::NOT_EQUAL);
            }
            else {
                addToken(TokenType::NOT);
            }
            break;

        case '&':
            if (match('&')) {
                addToken(TokenType::AND);
            }
            else {
                addToken(TokenType::INVALID);
            }
            break;

        case '|':
            if (match('|')) {
                addToken(TokenType::OR);
            }
            else {
                addToken(TokenType::INVALID);
            }
            break;

        case '"':
            string('"');
            break;

        case '\'':
            string('\'');
            break;

        case ' ':
        case '\r':
        case '\t':
            break;

        case '\n':
            line++;
            break;

        default:
            if (std::isdigit(static_cast<unsigned char>(c))) {
                number();
            }
            else if (isAlpha(c)) {
                identifier();
            }
            else {
                addToken(TokenType::INVALID);
            }
            break;
        }
    }

    void Lexer::lineComment() {
        while (peek() != '\n' && !isAtEnd()) {
            advance();
        }
    }

    void Lexer::blockComment() {
        int depth = 1;  // opening /* already consumed

        while (!isAtEnd()) {
            if (peek() == '\n') {
                line++;
            }

            if (peek() == '/' && peekNext() == '*') {
                advance();  // consume /
                advance();  // consume *
                depth++;
                continue;
            }

            if (peek() == '*' && peekNext() == '/') {
                advance();  // consume *
                advance();  // consume /
                depth--;
                if (depth == 0) {
                    return;
                }
                continue;
            }

            advance();
        }
    }

} // namespace vora