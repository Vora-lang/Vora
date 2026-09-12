#include "lexer.h"

#include <cctype>

namespace vora {

    const std::unordered_map<std::string, TokenType> Lexer::keywords = {
        {"const", TokenType::CONST},
        {"let", TokenType::LET},
        {"func", TokenType::FUNC},
        {"return", TokenType::RETURN},
        {"if", TokenType::IF},
        {"else", TokenType::ELSE},
        {"while", TokenType::WHILE},
        {"for", TokenType::FOR},
        {"in", TokenType::IN},
        {"class", TokenType::CLASS},
        {"this", TokenType::THIS},
        {"super", TokenType::SUPER},
        {"break", TokenType::BREAK},
        {"continue", TokenType::CONTINUE},
        {"try", TokenType::TRY},
        {"catch", TokenType::CATCH},
        {"finally", TokenType::FINALLY},
        {"throw", TokenType::THROW},
        {"import", TokenType::IMPORT},
        {"export", TokenType::EXPORT},
        {"as", TokenType::AS},
        {"from", TokenType::FROM},
        {"yield", TokenType::YIELD},
        {"async", TokenType::ASYNC},
        {"await", TokenType::AWAIT},
        {"match", TokenType::MATCH},
        {"defer", TokenType::DEFER},
        {"do", TokenType::DO},
        {"true", TokenType::TRUE},
        {"false", TokenType::FALSE},
        {"null", TokenType::NULL_TOKEN},
        {"and", TokenType::AND},
        {"or", TokenType::OR},
        {"not", TokenType::NOT},
    };

    Lexer::Lexer(std::string source, ErrorReporter& reporter)
        : source(std::move(source)), reporter_(reporter) {
    }

    std::vector<Token> Lexer::scanTokens() {
        while (!isAtEnd()) {
            start = current;
            startColumn = column;
            scanToken();
        }

        tokens.emplace_back(TokenType::END_OF_FILE, "", line, column);
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
        column++;
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

    char Lexer::peekAt(size_t offset) const {
        if (current + offset >= source.length()) {
            return '\0';
        }
        return source[current + offset];
    }

    bool Lexer::match(char expected) {
        if (isAtEnd()) return false;
        if (source[current] != expected) return false;

        current++;
        column++;
        return true;
    }

    void Lexer::error(const std::string& message) {
        reporter_.error(line, column, 1, message);
    }

    void Lexer::addToken(TokenType type) {
        std::string text = source.substr(start, current - start);
        tokens.emplace_back(type, text, line, startColumn);
    }

    void Lexer::number() {
        // Handle 0x / 0o / 0b prefixes (hex, octal, binary)
        if (source[start] == '0' && current - start == 1) {
            if (peek() == 'x' || peek() == 'X') {
                advance(); // consume 'x'
                while (std::isxdigit(static_cast<unsigned char>(peek()))) {
                    advance();
                }
                // Reject "0x" with no following digits (would crash parser).
                if (current - start == 2) {
                    error("Malformed hex literal: expected digits after 0x");
                    return;
                }
                addToken(TokenType::NUMBER);
                return;
            }
            if (peek() == 'o' || peek() == 'O') {
                advance(); // consume 'o'
                while (peek() >= '0' && peek() <= '7') {
                    advance();
                }
                if (current - start == 2) {
                    error("Malformed octal literal: expected digits after 0o");
                    return;
                }
                addToken(TokenType::NUMBER);
                return;
            }
            if (peek() == 'b' || peek() == 'B') {
                advance(); // consume 'b'
                while (peek() == '0' || peek() == '1') {
                    advance();
                }
                if (current - start == 2) {
                    error("Malformed binary literal: expected digits after 0b");
                    return;
                }
                addToken(TokenType::NUMBER);
                return;
            }
        }

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

        // Optional exponent: e/E [sign] digit+ . The lookahead demands a
        // digit (possibly behind a sign), so `1e` still lexes as NUMBER(1)
        // followed by IDENTIFIER(e) instead of swallowing the identifier,
        // and a trailing `.e` can never be read as an exponent.
        if (peek() == 'e' || peek() == 'E') {
            const char after = peekNext();
            const bool signThenDigit =
                (after == '+' || after == '-') &&
                std::isdigit(static_cast<unsigned char>(peekAt(2)));
            if (std::isdigit(static_cast<unsigned char>(after)) || signThenDigit) {
                advance();  // consume e / E
                if (peek() == '+' || peek() == '-') advance();
                while (std::isdigit(static_cast<unsigned char>(peek()))) {
                    advance();
                }
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

        // Depth of an open `${ ... }` interpolation region. While inside one the
        // closing delimiter does not terminate the string, and nested string
        // literals and braces are copied verbatim so that `"${f("x")}"` and
        // `"${ {a: 1}.a }"` scan as a single string literal. The compiler parses
        // the region's text as an expression afterwards.
        int interpolationDepth = 0;

        while (!isAtEnd()) {
            if (interpolationDepth == 0 && peek() == delimiter) break;

            if (peek() == '\n') {
                line++;
                column = 1;
            }

            char c = advance();

            if (interpolationDepth > 0) {
                if (c == '"' || c == '\'') {
                    // Nested string literal inside the interpolation: copy it
                    // whole, so its quotes and any braces inside it are not
                    // mistaken for the end of the region.
                    value += c;
                    while (!isAtEnd() && peek() != c) {
                        if (peek() == '\n') {
                            line++;
                            column = 1;
                        }
                        char inner = advance();
                        value += inner;
                        if (inner == '\\' && !isAtEnd()) {
                            value += advance();
                        }
                    }
                    if (isAtEnd()) break;
                    value += advance();  // closing quote of the nested literal
                    continue;
                }
                if (c == '{') {
                    interpolationDepth++;
                } else if (c == '}') {
                    interpolationDepth--;
                }
                value += c;
                continue;
            }

            if (c == '\\') {
                if (isAtEnd()) {
                    error("Unterminated escape sequence in string");
                    break;  // emit what we have so far
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
                    case '$':
                        // Escaped dollar: store the sentinel so the compiler
                        // does not treat the following `{` as interpolation.
                        // See kEscapedDollar in token.h.
                        value += kEscapedDollar;
                        break;
                    default:
                        // Unrecognized escape: keep both characters
                        value += '\\';
                        value += escaped;
                        break;
                }
            } else if (c == '$' && peek() == '{') {
                value += c;
                value += advance();  // '{'
                interpolationDepth = 1;
            } else {
                value += c;
            }
        }

        if (interpolationDepth > 0) {
            error("Unterminated `${` interpolation in string");
        }

        if (isAtEnd()) {
            error("Unterminated string");
            // Best-effort: emit what we have so far
            tokens.emplace_back(TokenType::STRING, value, line, startColumn);
            return;
        }

        // closing quote
        advance();

        tokens.emplace_back(TokenType::STRING, value, line, startColumn);
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
            if (peek() == '.') {
                advance();  // second dot
                if (peek() == '=') {
                    advance();
                    addToken(TokenType::DOT_DOT_EQUAL);
                } else if (peek() == '.') {
                    advance();  // third dot
                    addToken(TokenType::DOT_DOT_DOT);
                } else {
                    addToken(TokenType::DOT_DOT);
                }
            } else {
                addToken(TokenType::DOT);
            }
            break;

        case ':':
            addToken(TokenType::COLON);
            break;

        case '?':
            if (match('?')) {
                addToken(TokenType::QUESTION_QUESTION);
            } else if (match('.')) {
                addToken(TokenType::QUESTION_DOT);
            } else {
                addToken(TokenType::QUESTION);
            }
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
                if (match('=')) {
                    addToken(TokenType::POWER_EQUAL);
                }
                else {
                    addToken(TokenType::POWER);
                }
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
            } else if (match('>')) {
                addToken(TokenType::FAT_ARROW);
            } else {
                addToken(TokenType::EQUAL);
            }
            break;

        case '<':
            if (match('<')) {
                addToken(TokenType::LESS_LESS);
            }
            else if (match('=')) {
                addToken(TokenType::LESS_EQUAL);
            }
            else {
                addToken(TokenType::LESS);
            }
            break;

        case '>':
            if (match('>')) {
                addToken(TokenType::GREATER_GREATER);
            }
            else if (match('=')) {
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
                // Single '&' is bitwise AND (P1-F). '&&' stays logical AND.
                addToken(TokenType::AMPERSAND);
            }
            break;

        case '|':
            if (match('|')) {
                addToken(TokenType::OR);
            } else {
                // Single '|' is dual-purpose:
                //   - bitwise OR in expressions (`a | b`, P1-F)
                //   - match-arm or-pattern separator (`1 | 2 | 3 => ...`, P1-B)
                // The parser disambiguates by context; the lexer emits one
                // token for both.
                addToken(TokenType::PIPE);
            }
            break;

        case '^':
            addToken(TokenType::CARET);
            break;

        case '~':
            addToken(TokenType::TILDE);
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
            column = 1;
            break;

        default:
            if (std::isdigit(static_cast<unsigned char>(c))) {
                number();
            }
            else if (isAlpha(c)) {
                identifier();
            }
            else {
                error("Unexpected character: '" + std::string(1, c) + "'");
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
                column = 1;
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
        // If we reach end of file with depth > 0, the comment was never closed.
        if (depth > 0) {
            error("Unterminated block comment (missing */)");
        }
    }

} // namespace vora