#include "runtime_error.h"

namespace vora {

RuntimeError::RuntimeError(
    const std::string& message,
    Token token
)
    : std::runtime_error(message),
      token_(std::move(token)) {
}

const Token& RuntimeError::token() const noexcept {
    return token_;
}

int RuntimeError::line() const noexcept {
    return token_.line;
}

}
