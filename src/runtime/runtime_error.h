/**
 * @file runtime_error.h
 * @brief Exception type for Vora runtime errors.
 *
 * RuntimeError extends std::runtime_error with source-location
 * information (Token line/column) so that error messages reported to the
 * user can pinpoint the offending source position.
 */

#pragma once

#include <stdexcept>
#include "../lexer/token.h"

namespace vora {

/**
 * @brief Exception thrown when a runtime error occurs during VM execution.
 *
 * Carries a human-readable error message together with the Token where the
 * error originated, enabling precise line:column reporting.  Caught by the
 * VM's try/catch mechanism and by the top-level REPL/script runner.
 *
 * Inherits from std::runtime_error so it can be caught by generic
 * std::exception handlers in embedding applications.
 */
class RuntimeError : public std::runtime_error {
public:
    /**
     * @brief Construct a RuntimeError with a message and source location.
     * @param message Human-readable error description.
     * @param token   The Token whose line/column identifies the error site.
     */
    RuntimeError(
        const std::string& message,
        Token token
    );

    /**
     * @brief Get the source token associated with this error.
     * @return Const reference to the Token.
     */
    const Token& token() const noexcept;

    /**
     * @brief Get the source line number of the error.
     * @return 1-based line number.
     */
    int line() const noexcept;

    /**
     * @brief Get the source column number of the error.
     * @return 1-based column number.
     */
    int column() const noexcept;

private:
    Token token_;
};

}
