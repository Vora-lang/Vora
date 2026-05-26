#include "lexer.h"

namespace vora {

const std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"let", TokenType::LET},
    {"func", TokenType::FUNC},
    {"return", TokenType::RETURN},
    {"if", TokenType::IF},
    {"else", TokenType::ELSE},
    {"while", TokenType::WHILE},
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
    return std::isalpha(c) || c == '_';
}

bool Lexer::isAlphaNumeric(char c) const {
    return isAlpha(c) || std::isdigit(c);
}

char Lexer::advance() {
    return source[current++];
}

char Lexer::peek() const {
    if (isAtEnd()) {
        return '\0';
    }

    return source[current];
}

bool Lexer::match(char expected) {
    if (isAtEnd()) return false;
    if (source[current] != expected) return false;

    current++;
    return true;
}

char Lexer::peekNext() const {
    if (current + 1 >= source.length()) {
        return '\0';
    }
    return source[current + 1];
}

void Lexer::addToken(TokenType type) {
    std::string text = source.substr(start, current - start);

    tokens.emplace_back(type, text, line);
}

void Lexer::number() {
    while (std::isdigit(peek())) {
        advance();
    }

    addToken(TokenType::NUMBER);
}

void Lexer::identifier() {
    while (isAlphaNumeric(peek())) {
        advance();
    }

    std::string text = source.substr(start, current - start);

    auto it = keywords.find(text);

    if (it != keywords.end()) {
        addToken(it->second);
    }
    else {
        addToken(TokenType::IDENTIFIER);
    }
}

void Lexer::string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') {
            line++;
        }
        advance();
    }

    if (isAtEnd()) {
        addToken(TokenType::INVALID);
        return;
    }

    // closing "
    advance();

    std::string value = source.substr(start + 1, current - start - 2);

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

        case '+':
            if (match('+')) {
                addToken(TokenType::PLUS_PLUS);
            } else {
                addToken(TokenType::PLUS);
            }
            break;

        case '-':
            if (match('-')) {
                addToken(TokenType::MINUS_MINUS);
            } else {
                addToken(TokenType::MINUS);
            }
            break;

        case '*':
            if (match('*')) {
                addToken(TokenType::POWER);
            } else {
                addToken(TokenType::MULTIPLY);
            }
            break;

        case '/':
            addToken(TokenType::DIVIDE);
            break;

        case '%':
            addToken(TokenType::MODULO);
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

        case ';':
            addToken(TokenType::SEMICOLON);
            break;

        case '=':
            if (match('=')) {
                addToken(TokenType::EQUAL_EQUAL);
            } else {
                addToken(TokenType::EQUAL);
            }
            break;

        case '<':
            if (match('=')) {
                addToken(TokenType::LESS_EQUAL);
            } else {
                addToken(TokenType::LESS);
            }
            break;

        case '>':
            if (match('=')) {
                addToken(TokenType::GREATER_EQUAL);
            } else {
                addToken(TokenType::GREATER);
            }
            break;

        case '!':
            if (match('=')) {
                addToken(TokenType::NOT_EQUAL);
            } else {
                addToken(TokenType::NOT);
            }
            break;

        case '&':
            if (match('&')) {
                addToken(TokenType::AND);
            } else {
                addToken(TokenType::INVALID);
            }
            break;

        case '|':
            if (match('|')) {
                addToken(TokenType::OR);
            } else {
                addToken(TokenType::INVALID);
            }
            break;

        case '"':
            string();
            break;

        case ' ':
        case '\r':
        case '\t':
            break;

        case '\n':
            line++;
            break;

        default:
            if (std::isdigit(c)) {
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

}
