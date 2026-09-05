/**
 * @file src/common/error_reporter.h
 * @brief Abstract error-reporting interface for decoupling diagnostic emission
 *        from display/storage.
 *
 * Provides the Severity enum, Diagnostic value struct, and the ErrorReporter
 * abstract base class.  The CLI uses StderrErrorReporter to print formatted
 * messages to the terminal; the LSP server uses a diagnostic collector that
 * publishes via textDocument/publishDiagnostics.  This decoupling lets the
 * lexer, parser, and compiler emit errors through a single interface without
 * knowledge of how those errors are ultimately presented to the user.
 */

#pragma once

#include <string>

namespace vora {

/**
 * @brief Severity level of a diagnostic message.
 *
 * Used by ErrorReporter and Diagnostic to classify the importance of a
 * reported issue.  Only Error-level diagnostics set the hadError flag.
 */
enum class Severity {
    Error,   ///< Compilation cannot proceed correctly (sets hadError).
    Warning, ///< Potential issue that does not block compilation.
    Hint     ///< Informational suggestion (style, best-practice, etc.).
};

/**
 * @brief A single diagnostic message anchored to a source location.
 *
 * Carries a 1-indexed line/column span, a human-readable message, and a
 * severity level.  The length field controls how many characters are
 * underlined when the diagnostic is rendered in source context.
 */
struct Diagnostic {
    int line = 0;              ///< 1-indexed line number.
    int column = 0;            ///< 1-indexed column (byte offset within line).
    int length = 1;            ///< Underline length in characters.
    std::string message;       ///< Human-readable diagnostic text.
    Severity severity = Severity::Error;  ///< Severity classification.
};

/**
 * @brief Abstract error reporter — decouples error/warning emission from
 *        display/storage.
 *
 * The CLI uses StderrErrorReporter; the LSP server uses a collector that
 * publishes via textDocument/publishDiagnostics.  All compiler front-end
 * components (lexer, parser, compiler) report diagnostics through this
 * interface without knowing how they are ultimately rendered.
 */
class ErrorReporter {
public:
    /// @brief Virtual destructor.
    virtual ~ErrorReporter() = default;

    /**
     * @brief Report a diagnostic at the given source location.
     *
     * @param line    1-indexed line number.
     * @param column  1-indexed column (byte offset within line).
     * @param length  Number of characters to underline.
     * @param message Human-readable diagnostic text.
     * @param severity Severity classification (Error, Warning, or Hint).
     */
    virtual void report(int line, int column, int length,
                        const std::string& message, Severity severity) = 0;

    /**
     * @brief Check whether at least one error has been reported.
     *
     * Only Error-level diagnostics set this flag; warnings and hints do not.
     *
     * @return true if at least one error has been reported.
     */
    virtual bool hadError() const = 0;

    /**
     * @brief Convenience method: report an error-level diagnostic.
     *
     * Equivalent to `report(line, column, length, message, Severity::Error)`.
     *
     * @param line    1-indexed line number.
     * @param column  1-indexed column.
     * @param length  Underline length in characters.
     * @param message Human-readable error message.
     */
    void error(int line, int column, int length, const std::string& message) {
        report(line, column, length, message, Severity::Error);
    }

    /**
     * @brief Convenience method: report a warning-level diagnostic.
     *
     * Equivalent to `report(line, column, length, message, Severity::Warning)`.
     *
     * @param line    1-indexed line number.
     * @param column  1-indexed column.
     * @param length  Underline length in characters.
     * @param message Human-readable warning message.
     */
    void warning(int line, int column, int length, const std::string& message) {
        report(line, column, length, message, Severity::Warning);
    }
};

/**
 * @brief Error reporter that prints formatted diagnostics to stderr.
 *
 * Preserves the current CLI behavior: errors and warnings are printed with
 * source-line context via printSourceLine().  The source text is stored
 * internally and can be updated when the VM switches chunks.
 */
class StderrErrorReporter : public ErrorReporter {
public:
    /**
     * @brief Construct a reporter with the given source text.
     * @param source The full source code string (used for source-line context).
     */
    explicit StderrErrorReporter(const std::string& source) : source_(source) {}

    /**
     * @brief Print a formatted diagnostic to stderr with source-line context.
     *
     * @param line    1-indexed line number.
     * @param column  1-indexed column.
     * @param length  Underline length in characters.
     * @param message Human-readable diagnostic text.
     * @param severity Severity classification.
     */
    void report(int line, int column, int length,
                const std::string& message, Severity severity) override;

    /**
     * @brief Check whether at least one error has been reported.
     * @return true if an error was reported since construction or last reset.
     */
    bool hadError() const override { return hadError_; }

    /**
     * @brief Update the source text (needed when the VM switches chunks).
     *
     * The StderrErrorReporter uses the source text to print the offending
     * line with a caret underline.  When the VM compiles or executes a
     * different chunk (e.g., via import), the source reference must be
     * updated so diagnostics point at the correct file.
     *
     * @param source The new source code string.
     */
    void setSource(const std::string& source) { source_ = source; }

private:
    std::string source_;   ///< The source text used for line-context rendering.
    bool hadError_ = false; ///< Error flag set by report() on Severity::Error.
};

} // namespace vora
