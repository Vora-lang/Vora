#include <iostream>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast_printer.h"

int main() {

    // std::string source = "let a = 1 + 2 * 3";
    std::string source = R"(
        if (a) {
            return b;
        }
        else {
            return c;
        }

        while (a < b) {
            a = a + 1;
        }
    )";

    vora::Lexer lexer(source);

    auto tokens = lexer.scanTokens();

    vora::Parser parser(tokens);

    // auto expr = parser.parse();
    // auto stmt = parser.parse();

    // vora::ASTPrinter printer;

    // std::cout << printer.print(stmt.get()) << std::endl;

    auto program = parser.parse();

    vora::ASTPrinter printer;

    std::cout
        << printer.print(program.get())
        << std::endl;

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
