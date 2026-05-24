#include <iostream>

#include "lexer/lexer.h"
#include "lexer/token.h"

int main() {
    std::string source = R"(
        let value = test123
        func add
        return value
        let a = "hello"
        let b = a == "hello"
        a++
        b--
        a!=
        a<=
        b>=
        ||
        &&
        **
        !
        +
        -
        *
        /
        %
    )";

    nyra::Lexer lexer(source);

    auto tokens = lexer.scanTokens();

    for (const auto& token : tokens) {
        std::cout
            << nyra::tokenTypeToString(token.type)
            << "("
            << token.lexeme
            << ")"
            << std::endl;
    }

    return 0;
}
