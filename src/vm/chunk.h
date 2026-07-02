#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../runtime/value.h"

namespace vora {

/**
 * @file chunk.h
 * @brief Bytecode container with constant pool and RLE line/column tables.
 *
 * The Chunk class is the compiled output unit produced by the bytecode compiler
 * and consumed by the VM. It holds:
 * - A raw bytecode sequence (`code`) — a vector of uint8_t opcodes and operands.
 * - A constant pool (`constants`) — deduplicated Values referenced by
 *   OP_CONSTANT instructions.
 * - Run-length encoded line and column tables (`lines`, `columns`) for debug
 *   disassembly and runtime error reporting.
 * - The original source text (`source`) for source-line display in errors.
 *
 * RLE strategy: line/column numbers are stored only when they change, with
 * run-length counters in between. This keeps debug metadata compact while
 * allowing O(1) offset-to-position lookup via cumulative run-length scanning.
 */

/**
 * Print a source line with a caret marker at the given position.
 *
 * Produces output like:
 * @code
 *   --> line 3:
 *       let x = 1 / 0;
 *                 ^~~
 *   Error: Division by zero
 * @endcode
 *
 * @param out    Output stream (e.g. std::cerr).
 * @param source Full source text (may be empty — function prints nothing).
 * @param line   1-indexed line number.
 * @param column 1-indexed column within that line.
 * @param length Number of characters to underline (1 = single caret, >1 = tilde span).
 * @param label  Message label printed next to the caret line.
 * @param prefix Label for the --> line (default "Error").
 */
void printSourceLine(std::ostream& out,
                     const std::string& source,
                     int line, int column, int length,
                     const std::string& label,
                     const std::string& prefix = "Error");

/**
 * Convenience wrapper around printSourceLine() for single-point errors.
 *
 * Calls printSourceLine() with `length = 1` and `prefix = "Error"`.
 *
 * @param out     Output stream.
 * @param source  Full source text.
 * @param line    1-indexed line number.
 * @param column  1-indexed column number.
 * @param message Error message to display.
 */
inline void printSourceError(std::ostream& out,
                             const std::string& source,
                             int line, int column,
                             const std::string& message) {
    printSourceLine(out, source, line, column, 1, message, "Error");
}

/**
 * @brief Compiled bytecode container with constant pool and debug metadata.
 *
 * Chunk is the unit of compilation: the bytecode compiler emits instructions
 * into a Chunk, and the VM executes from it. Each function, method, constructor,
 * and the top-level script get their own Chunk (stored as FunctionPrototype in
 * the constant pool of the enclosing Chunk).
 *
 * The constant pool is deduplicated — addConstant() with the same Value returns
 * the same index. This saves space and allows value-based identity comparisons
 * (e.g. two OP_CONSTANT instructions loading the same string will share the
 * same pool index).
 *
 * Line/column metadata uses run-length encoding: a line or column number is
 * stored only when it differs from the previous byte's position. The `lines`
 * and `columns` vectors store interleaved (count, value) pairs.
 */
class Chunk {
public:
    /**
     * @brief Append a single byte to the bytecode.
     *
     * @param byte   The opcode or operand byte to emit.
     * @param line   1-indexed source line (for RLE debug table).
     * @param column 1-indexed source column (for RLE debug table).
     */
    void write(uint8_t byte, int line, int column);

    /**
     * @brief Append two bytes to the bytecode (common for 16-bit operands).
     *
     * Used for instructions like OP_CONSTANT (index operand), OP_JUMP
     * (offset operand), etc. Both bytes share the same source position.
     *
     * @param a      First byte (typically low byte of a 16-bit value).
     * @param b      Second byte (typically high byte of a 16-bit value).
     * @param line   1-indexed source line.
     * @param column 1-indexed source column.
     */
    void write(uint8_t a, uint8_t b, int line, int column);

    /**
     * @brief Overwrite a byte at a specific offset (used for jump back-patching).
     *
     * Does not affect the RLE line/column tables — those are only written on
     * append via write().
     *
     * @param offset Bytecode offset to overwrite.
     * @param byte   New byte value.
     */
    void writeAt(size_t offset, uint8_t byte);

    /**
     * @brief Add a constant to the pool, deduplicating by value.
     *
     * String, int64_t, and double constants are deduplicated in O(1) via
     * internal hash maps. Other types (nullptr_t, bool, Array, Dict, etc.)
     * are compared by linear scan — these are rare enough that the overhead
     * is negligible.
     *
     * @param value The constant value to add.
     * @return The index of the constant in the pool (suitable for OP_CONSTANT).
     */
    size_t addConstant(Value value);

    /**
     * @brief Add a constant and emit an OP_CONSTANT instruction for it.
     *
     * Equivalent to calling addConstant() followed by write() with OP_CONSTANT
     * and the two-byte index operand.
     *
     * @param value  The constant value.
     * @param line   1-indexed source line.
     * @param column 1-indexed source column.
     */
    void writeConstant(Value value, int line, int column);

    /**
     * @brief Disassemble the entire chunk to stdout with opcode mnemonics.
     *
     * Prints a header line with the chunk name, then each instruction on its
     * own line with offset, line number, and operand decoding.
     *
     * @param name Human-readable name for this chunk (e.g. "main", "test",
     *             or a function name).
     */
    void disassemble(const std::string& name) const;

    /**
     * @brief Disassemble a single instruction at the given offset.
     *
     * Decodes the opcode and operands, prints the disassembly line, and
     * returns the offset of the next instruction.
     *
     * @param offset Bytecode offset of the instruction to disassemble.
     * @return The bytecode offset of the following instruction.
     */
    size_t disassembleInstruction(size_t offset) const;

    /** @brief Raw bytecode: opcodes and their operands. */
    std::vector<uint8_t> code;

    /** @brief Constant pool: Values referenced by OP_CONSTANT instructions. */
    std::vector<Value> constants;

    /**
     * @brief RLE-encoded source line numbers.
     *
     * Format: interleaved (count, value) pairs. `count` bytes at consecutive
     * code offsets share the same `value` line number. Public so the VM can
     * look up source positions for runtime error reporting.
     */
    std::vector<int> lines;

    /**
     * @brief RLE-encoded source column numbers.
     *
     * Same interleaved (count, value) format as `lines`.
     */
    std::vector<int> columns;

    /**
     * @brief Original source text, stored for error message display.
     *
     * When a runtime error occurs, the VM uses this together with `lines`
     * and `columns` to print a source snippet with a caret pointing at the
     * offending code.
     */
    std::string source;

private:
    /**
     * @brief Append RLE entries for a (line, column) position.
     *
     * If the line or column matches the previous value, increments the
     * current run-length counter. Otherwise flushes the current run and
     * starts a new one.
     *
     * @param line   1-indexed source line.
     * @param column 1-indexed source column.
     */
    void writeLineColumn(int line, int column);

    /**
     * @brief Hash index for O(1) string constant deduplication.
     *
     * Maps string value to its index in `constants`. Without this, each
     * addConstant() call for a string would require an O(n) linear scan.
     */
    std::unordered_map<std::string, size_t> stringConstantIndices_;

    /**
     * @brief Hash index for O(1) int64_t constant deduplication.
     *
     * Integer constants are very common (loop bounds, small literals, array
     * indices), making O(1) dedup worthwhile.
     */
    std::unordered_map<int64_t, size_t> intConstantIndices_;

    /**
     * @brief Hash index for O(1) double constant deduplication.
     *
     * Frequent in numeric-heavy code. NaN values are excluded — per IEEE 754,
     * NaN != NaN, so a NaN can never match an existing entry and is always
     * treated as a new constant.
     */
    std::unordered_map<double, size_t> doubleConstantIndices_;

    // RLE state — tracks the most recently written position to decide
    // whether to extend the current run or start a new one.

    /** @brief Last line number written (for RLE run detection). */
    int lastLine = -1;

    /** @brief Last column number written (for RLE run detection). */
    int lastColumn = -1;

    /** @brief Starting offset of the current RLE run. */
    int lineRunStart = 0;
};

} // namespace vora
