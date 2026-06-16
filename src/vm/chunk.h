#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../runtime/value.h"

namespace vora {

// Print a source line with a caret marker at the given position.
// `source`  = full source text (may be empty — function prints nothing)
// `line`    = 1-indexed line number
// `column`  = 1-indexed column within that line
// `length`  = number of characters to underline (1 = single caret, >1 = tilde span)
// `label`   = message label printed next to the caret line (e.g. "Error: Division by zero")
// `prefix`  = "Error" / "Warning" / etc. for the --> line
void printSourceLine(std::ostream& out,
                     const std::string& source,
                     int line, int column, int length,
                     const std::string& label,
                     const std::string& prefix = "Error");

// Convenience: print source snippet for a single-point error.
inline void printSourceError(std::ostream& out,
                             const std::string& source,
                             int line, int column,
                             const std::string& message) {
    printSourceLine(out, source, line, column, 1, message, "Error");
}

// Chunk holds a compiled bytecode sequence: code bytes, constant pool, and
// run-length-encoded line/column tables for debug disassembly.
class Chunk {
public:
    void write(uint8_t byte, int line, int column);
    void write(uint8_t a, uint8_t b, int line, int column);
    void writeAt(size_t offset, uint8_t byte);

    // Add a constant to the pool (deduplicates by value). Returns the index.
    size_t addConstant(Value value);

    // Convenience: add constant + emit OP_CONSTANT <index>.
    void writeConstant(Value value, int line, int column);

    // Disassembly (debug output).
    void disassemble(const std::string& name) const;
    size_t disassembleInstruction(size_t offset) const;

    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<int> lines;   // RLE line numbers (public for VM runtime errors)
    std::vector<int> columns; // RLE column numbers
    std::string source;       // original source text (for error display)

private:
    // Hash index for O(1) string constant dedup (prevents O(n²) linear scan).
    // Maps string value → index in `constants`.
    std::unordered_map<std::string, size_t> stringConstantIndices_;

    // O(1) dedup for int64_t (very common as small loop bounds / literals).
    std::unordered_map<int64_t, size_t> intConstantIndices_;

public:

private:
    void writeLineColumn(int line, int column);

    // RLE state
    int lastLine = -1;
    int lastColumn = -1;
    int lineRunStart = 0;
};

} // namespace vora
