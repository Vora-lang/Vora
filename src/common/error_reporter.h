#pragma once

#include <string>

namespace vora {

// Severity of a diagnostic message.
enum class Severity {
    Error,
    Warning,
    Hint
};

// A diagnostic message with source location.
struct Diagnostic {
    int line = 0;        // 1-indexed
    int column = 0;      // 1-indexed
    int length = 1;      // underline length (characters)
    std::string message;
    Severity severity = Severity::Error;
};

// Abstract error reporter — decouples error/warning emission from
// display/storage. CLI uses StderrErrorReporter; LSP will use a
// collector that publishes via textDocument/publishDiagnostics.
class ErrorReporter {
public:
    virtual ~ErrorReporter() = default;

    // Report a diagnostic at the given source location.
    virtual void report(int line, int column, int length,
                        const std::string& message, Severity severity) = 0;

    // True if at least one error (not warning) has been reported.
    virtual bool hadError() const = 0;

    // Convenience: report an error.
    void error(int line, int column, int length, const std::string& message) {
        report(line, column, length, message, Severity::Error);
    }

    // Convenience: report a warning.
    void warning(int line, int column, int length, const std::string& message) {
        report(line, column, length, message, Severity::Warning);
    }
};

// Error reporter that prints formatted messages to stderr.
// Preserves the current CLI behavior via printSourceLine().
class StderrErrorReporter : public ErrorReporter {
public:
    explicit StderrErrorReporter(const std::string& source) : source_(source) {}

    void report(int line, int column, int length,
                const std::string& message, Severity severity) override;
    bool hadError() const override { return hadError_; }

    // Update the source text (needed when the VM switches chunks).
    void setSource(const std::string& source) { source_ = source; }

private:
    std::string source_;
    bool hadError_ = false;
};

} // namespace vora
