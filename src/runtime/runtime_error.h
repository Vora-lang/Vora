#pragma once

#include <stdexcept>
#include "../lexer/token.h"

namespace vora {

class RuntimeError : public std::runtime_error {
public:
    RuntimeError(
        const std::string& message,
        Token token
    );

    const Token& token() const noexcept;

    int line() const noexcept;
    int column() const noexcept;

private:
    Token token_;
};

}
