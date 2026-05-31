#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../runtime/value.h"

namespace vora {

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

private:
    void writeLineColumn(int line, int column);

    // RLE state
    int lastLine = -1;
    int lastColumn = -1;
    int lineRunStart = 0;
};

} // namespace vora
