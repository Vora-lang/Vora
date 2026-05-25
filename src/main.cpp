#include <iostream>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast_printer.h"

int main() {

    std::string source = "a = b + c * d";

    vora::Lexer lexer(source);

    auto tokens = lexer.scanTokens();

    vora::Parser parser(tokens);

    auto expr = parser.parse();

    vora::ASTPrinter printer;

    std::cout << printer.print(expr.get()) << std::endl;

    for (const auto& token : tokens) {
        std::cout
            << tokenTypeToString(token.type)
            << "("
            << token.lexeme
            << ")"
            << std::endl;
    }

    return 0;
}
