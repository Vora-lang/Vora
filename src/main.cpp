#include <iostream>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast_printer.h"

int main() {

    std::string source = "1 + 2 * 3";

    vora::Lexer lexer(source);

    auto tokens = lexer.scanTokens();

    vora::Parser parser(tokens);

    auto expr = parser.parse();

    vora::ASTPrinter printer;

    std::cout << printer.print(expr.get()) << std::endl;

    return 0;
}
