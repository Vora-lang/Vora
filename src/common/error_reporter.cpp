#include "error_reporter.h"

#include "../vm/chunk.h"  // for printSourceLine()
#include <iostream>

namespace vora {

void StderrErrorReporter::report(int line, int column, int length,
                                  const std::string& message, Severity severity) {
    if (severity == Severity::Error) {
        hadError_ = true;
    }

    const char* prefix = (severity == Severity::Error)   ? "Error"
                       : (severity == Severity::Warning) ? "Warning"
                       :                                   "Hint";

    printSourceLine(std::cerr, source_, line, column, length, message, prefix);
}

} // namespace vora
