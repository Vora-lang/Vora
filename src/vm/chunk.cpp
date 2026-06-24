#include "chunk.h"
#include "opcode.h"
#include "compiler.h"

#include "../runtime/callable.h"

#include <cstdio>
#include <cmath>

namespace vora {

// =========================================================================
// OpCode name table
// =========================================================================

static const char* opcodeName(OpCode op) {
    switch (op) {
        case OpCode::OP_CONSTANT:       return "OP_CONSTANT";
        case OpCode::OP_CONSTANT_LONG: return "OP_CONSTANT_LONG";
        case OpCode::OP_NULL:           return "OP_NULL";
        case OpCode::OP_TRUE:           return "OP_TRUE";
        case OpCode::OP_FALSE:          return "OP_FALSE";
        case OpCode::OP_POP:            return "OP_POP";
        case OpCode::OP_DUP:            return "OP_DUP";
        case OpCode::OP_PRINT_POP:      return "OP_PRINT_POP";
        case OpCode::OP_POPN:           return "OP_POPN";
        case OpCode::OP_NEGATE:         return "OP_NEGATE";
        case OpCode::OP_NOT:            return "OP_NOT";
        case OpCode::OP_ADD:            return "OP_ADD";
        case OpCode::OP_DIVIDE:         return "OP_DIVIDE";
        case OpCode::OP_MODULO:         return "OP_MODULO";
        case OpCode::OP_POWER:          return "OP_POWER";
        case OpCode::OP_SUB_NN:         return "OP_SUB_NN";
        case OpCode::OP_MUL_NN:         return "OP_MUL_NN";
        case OpCode::OP_DIV_NN:         return "OP_DIV_NN";
        case OpCode::OP_MOD_NN:         return "OP_MOD_NN";
        case OpCode::OP_LESS_NN:        return "OP_LESS_NN";
        case OpCode::OP_LESS_EQ_NN:     return "OP_LESS_EQ_NN";
        case OpCode::OP_GREATER_NN:     return "OP_GREATER_NN";
        case OpCode::OP_GREATER_EQ_NN:  return "OP_GREATER_EQ_NN";
        case OpCode::OP_EQUAL:          return "OP_EQUAL";
        case OpCode::OP_NOT_EQUAL:      return "OP_NOT_EQUAL";
        case OpCode::OP_LESS:           return "OP_LESS";
        case OpCode::OP_GET_LOCAL:      return "OP_GET_LOCAL";
        case OpCode::OP_SET_LOCAL:      return "OP_SET_LOCAL";
        case OpCode::OP_DEFINE_GLOBAL:  return "OP_DEFINE_GLOBAL";
        case OpCode::OP_GET_GLOBAL:      return "OP_GET_GLOBAL";
        case OpCode::OP_SET_GLOBAL:      return "OP_SET_GLOBAL";
        case OpCode::OP_GET_GLOBAL_SAFE: return "OP_GET_GLOBAL_SAFE";
        case OpCode::OP_DEFINE_GLOBAL_WIDE: return "OP_DEFINE_GLOBAL_WIDE";
        case OpCode::OP_GET_GLOBAL_WIDE:    return "OP_GET_GLOBAL_WIDE";
        case OpCode::OP_SET_GLOBAL_WIDE:    return "OP_SET_GLOBAL_WIDE";
        case OpCode::OP_JUMP:           return "OP_JUMP";
        case OpCode::OP_JUMP_IF_FALSE:  return "OP_JUMP_IF_FALSE";
        case OpCode::OP_LOOP:           return "OP_LOOP";
        case OpCode::OP_CALL:           return "OP_CALL";
        case OpCode::OP_TAIL_CALL:     return "OP_TAIL_CALL";
        case OpCode::OP_CLOSURE:        return "OP_CLOSURE";
        case OpCode::OP_RETURN:         return "OP_RETURN";
        case OpCode::OP_YIELD:          return "OP_YIELD";
        case OpCode::OP_PUSH_SPREAD:   return "OP_PUSH_SPREAD";
        case OpCode::OP_SPREAD:        return "OP_SPREAD";
        case OpCode::OP_CALL_N:        return "OP_CALL_N";
        case OpCode::OP_ARRAY:          return "OP_ARRAY";
        case OpCode::OP_DICT:           return "OP_DICT";
        case OpCode::OP_INDEX:          return "OP_INDEX";
        case OpCode::OP_SET_INDEX:      return "OP_SET_INDEX";
        case OpCode::OP_GET_PROPERTY:   return "OP_GET_PROPERTY";
        case OpCode::OP_SET_PROPERTY:   return "OP_SET_PROPERTY";
        case OpCode::OP_CLASS:          return "OP_CLASS";
        case OpCode::OP_GET_SUPER:      return "OP_GET_SUPER";
        case OpCode::OP_DEFAULT_PARAM:  return "OP_DEFAULT_PARAM";
        case OpCode::OP_PUSH_CATCH:     return "OP_PUSH_CATCH";
        case OpCode::OP_POP_CATCH:      return "OP_POP_CATCH";
        case OpCode::OP_CLEAR_EXCEPTION: return "OP_CLEAR_EXCEPTION";
        case OpCode::OP_THROW:          return "OP_THROW";
        case OpCode::OP_FINALLY_END:    return "OP_FINALLY_END";
        case OpCode::OP_JUMP_IF_NULL:   return "OP_JUMP_IF_NULL";
        case OpCode::OP_IMPORT:         return "OP_IMPORT";
        case OpCode::OP_CONVERT:       return "OP_CONVERT";
    }
    return "OP_UNKNOWN";
}

// =========================================================================
// Simple constant value printing for disassembly
// =========================================================================

static std::string constantToString(const Value& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) return "null";
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? "true" : "false";
    if (std::holds_alternative<int64_t>(value)) {
        return std::to_string(std::get<int64_t>(value));
    }
    if (std::holds_alternative<double>(value)) {
        double d = std::get<double>(value);
        if (std::floor(d) == d) return std::to_string(static_cast<int64_t>(d));
        return std::to_string(d);
    }
    if (std::holds_alternative<GcPtr<GcString>>(value)) {
        return "'" + std::get<GcPtr<GcString>>(value)->value + "'";
    }
    if (std::holds_alternative<GcPtr<FunctionPrototype>>(value)) {
        return "<proto " + std::get<GcPtr<FunctionPrototype>>(value)->name + ">";
    }
    if (std::holds_alternative<GcPtr<ClassDefinition>>(value)) {
        return "<class " + std::get<GcPtr<ClassDefinition>>(value)->name + ">";
    }
    if (std::holds_alternative<GcPtr<Array>>(value)) {
        return "[array]";
    }
    if (std::holds_alternative<GcPtr<Set>>(value)) {
        return "[set]";
    }
    if (std::holds_alternative<GcPtr<Map>>(value)) {
        return "[map]";
    }
    if (std::holds_alternative<GcPtr<Callable>>(value)) {
        return "<callable>";
    }
    if (std::holds_alternative<GcPtr<ObjectInstance>>(value)) {
        return "<instance " + std::get<GcPtr<ObjectInstance>>(value)->className + ">";
    }
    return "[unknown]";
}

// =========================================================================
// Chunk implementation
// =========================================================================

void Chunk::writeLineColumn(int line, int column) {
    if (lines.empty()) {
        lines.push_back(1);
        lines.push_back(line);
        columns.push_back(1);
        columns.push_back(column);
        lastLine = line;
        lastColumn = column;
        lineRunStart = 0;
        return;
    }

    if (line == lastLine && column == lastColumn) {
        lines[lineRunStart]++;
        columns[lineRunStart]++;
    } else {
        lines.push_back(1);
        lines.push_back(line);
        columns.push_back(1);
        columns.push_back(column);
        lineRunStart = lines.size() - 2;
        lastLine = line;
        lastColumn = column;
    }
}

void Chunk::write(uint8_t byte, int line, int column) {
    code.push_back(byte);
    writeLineColumn(line, column);
}

void Chunk::write(uint8_t a, uint8_t b, int line, int column) {
    code.push_back(a);
    writeLineColumn(line, column);
    code.push_back(b);
    writeLineColumn(line, column);
}

void Chunk::writeAt(size_t offset, uint8_t byte) {
    if (offset < code.size()) {
        code[offset] = byte;
    }
}

size_t Chunk::addConstant(Value value) {
    // O(1) hash lookup for string constants.
    if (auto* s = std::get_if<GcPtr<GcString>>(&value)) {
        auto it = stringConstantIndices_.find((*s)->value);
        if (it != stringConstantIndices_.end()) return it->second;
    }

    // O(1) hash lookup for int64_t constants (very common).
    if (auto* iv = std::get_if<int64_t>(&value)) {
        auto it = intConstantIndices_.find(*iv);
        if (it != intConstantIndices_.end()) return it->second;
    }

    // O(1) hash lookup for double constants.
    // NaN is excluded — NaN != NaN per IEEE 754, so it can never be a duplicate.
    if (auto* dv = std::get_if<double>(&value)) {
        if (!std::isnan(*dv)) {
            auto it = doubleConstantIndices_.find(*dv);
            if (it != doubleConstantIndices_.end()) return it->second;
        }
    }

    // Linear scan only for nullptr_t and bool — these are at most 1 null + 2
    // bools total, so O(n) is bounded by a tiny constant. GcPtr types (except
    // GcString handled above) are pointer-unique by nature — no two distinct
    // FunctionPrototype/ClassDefinition/etc. objects will ever compare equal,
    // so the linear scan would waste cycles without ever finding a match.
    // double and int64_t are handled exclusively by their hash maps above.
    if (value.index() <= 3) {  // nullptr_t(0), double(1), int64_t(2), bool(3)
        for (size_t i = 0; i < constants.size(); i++) {
            const auto& existing = constants[i];
            if (existing.index() != value.index()) continue;

            if (std::holds_alternative<std::nullptr_t>(value)) {
                return i;
            }
            if (std::holds_alternative<bool>(value) &&
                std::get<bool>(existing) == std::get<bool>(value)) {
                return i;
            }
        }
    }

    // Insert and update hash indices.
    size_t idx = constants.size();
    constants.push_back(value);

    if (auto* s = std::get_if<GcPtr<GcString>>(&constants[idx])) {
        stringConstantIndices_[(*s)->value] = idx;
    } else if (auto* iv = std::get_if<int64_t>(&constants[idx])) {
        intConstantIndices_[*iv] = idx;
    } else if (auto* dv = std::get_if<double>(&constants[idx])) {
        if (!std::isnan(*dv)) {
            doubleConstantIndices_[*dv] = idx;
        }
    }

    return idx;
}

void Chunk::writeConstant(Value value, int line, int column) {
    size_t index = addConstant(value);
    if (index <= UINT8_MAX) {
        write(static_cast<uint8_t>(OpCode::OP_CONSTANT), line, column);
        write(static_cast<uint8_t>(index), line, column);
    } else {
        write(static_cast<uint8_t>(OpCode::OP_CONSTANT_LONG), line, column);
        write(static_cast<uint8_t>(index & 0xFF), line, column);        // lo byte
        write(static_cast<uint8_t>((index >> 8) & 0xFF), line, column); // hi byte
    }
}

// =========================================================================
// Disassembler
// =========================================================================

void Chunk::disassemble(const std::string& name) const {
    std::printf("== %s ==\n", name.c_str());

    for (size_t offset = 0; offset < code.size();) {
        offset = disassembleInstruction(offset);
    }
}

size_t Chunk::disassembleInstruction(size_t offset) const {
    std::printf("%04zu ", offset);

    int line = 0;
    size_t pos = 0;
    for (size_t i = 0; i < lines.size(); i += 2) {
        int runLen = lines[i];
        if (offset < pos + static_cast<size_t>(runLen)) {
            line = lines[i + 1];
            break;
        }
        pos += runLen;
    }
    if (offset > 0) {
        int prevLine = 0;
        pos = 0;
        for (size_t i = 0; i < lines.size(); i += 2) {
            int runLen = lines[i];
            if (offset - 1 < pos + static_cast<size_t>(runLen)) {
                prevLine = lines[i + 1];
                break;
            }
            pos += runLen;
        }
        if (line == prevLine) {
            std::printf("   | ");
        } else {
            std::printf("%4d ", line);
        }
    } else {
        std::printf("%4d ", line);
    }

    if (offset >= code.size()) return offset + 1;

    OpCode instruction = static_cast<OpCode>(code[offset]);

    switch (instruction) {
        case OpCode::OP_CONSTANT: {
            uint8_t index = code[offset + 1];
            std::printf("%-16s %4d ", opcodeName(instruction), index);
            if (index < constants.size()) {
                std::printf("'%s'\n", constantToString(constants[index]).c_str());
            } else {
                std::printf("'<invalid index %d>'\n", index);
            }
            return offset + 2;
        }
        case OpCode::OP_CONSTANT_LONG: {
            uint16_t index = static_cast<uint16_t>(code[offset + 1])
                           | (static_cast<uint16_t>(code[offset + 2]) << 8);
            std::printf("%-16s %4d ", opcodeName(instruction), index);
            if (index < constants.size()) {
                std::printf("'%s'\n", constantToString(constants[index]).c_str());
            } else {
                std::printf("'<invalid index %d>'\n", index);
            }
            return offset + 3;
        }
        case OpCode::OP_GET_LOCAL:
        case OpCode::OP_SET_LOCAL:
        case OpCode::OP_GET_UPVALUE:
        case OpCode::OP_SET_UPVALUE:
        case OpCode::OP_CLOSE_UPVALUE: {
            uint8_t index = code[offset + 1];
            std::printf("%-16s %4d\n", opcodeName(instruction), index);
            return offset + 2;
        }
        case OpCode::OP_POPN: {
            uint8_t index = code[offset + 1];
            std::printf("%-16s %4d\n", opcodeName(instruction), index);
            return offset + 2;
        }
        case OpCode::OP_DEFINE_GLOBAL:
        case OpCode::OP_GET_GLOBAL:
        case OpCode::OP_SET_GLOBAL: {
            uint8_t slot = code[offset + 1];
            std::printf("%-16s slot %d\n", opcodeName(instruction), slot);
            return offset + 2;
        }
        case OpCode::OP_DEFINE_GLOBAL_WIDE:
        case OpCode::OP_GET_GLOBAL_WIDE:
        case OpCode::OP_SET_GLOBAL_WIDE: {
            uint16_t slot = static_cast<uint16_t>(code[offset + 1]) |
                           (static_cast<uint16_t>(code[offset + 2]) << 8);
            std::printf("%-16s slot %d\n", opcodeName(instruction), slot);
            return offset + 3;
        }
        case OpCode::OP_GET_PROPERTY:
        case OpCode::OP_SET_PROPERTY:
        case OpCode::OP_GET_SUPER: {
            uint8_t index = code[offset + 1];
            std::printf("%-16s %4d ", opcodeName(instruction), index);
            if (index < constants.size()) {
                std::printf("'%s'\n", constantToString(constants[index]).c_str());
            } else {
                std::printf("'<invalid index %d>'\n", index);
            }
            return offset + 2;
        }
        case OpCode::OP_GET_GLOBAL_SAFE: {
            uint8_t slot = code[offset + 1];
            uint8_t fallbackIndex = code[offset + 2];
            std::printf("%-16s slot %d fallback=%d", opcodeName(instruction),
                       slot, fallbackIndex);
            if (fallbackIndex < constants.size()) {
                std::printf(" -> '%s'", constantToString(constants[fallbackIndex]).c_str());
            }
            std::printf("\n");
            return offset + 3;
        }
        case OpCode::OP_CLOSURE: {
            uint8_t protoIndex = code[offset + 1];
            uint8_t upvalueCount = code[offset + 2];
            std::printf("%-16s %4d upvalues=%d", opcodeName(instruction), protoIndex, upvalueCount);
            if (protoIndex < constants.size()) {
                std::printf(" %s", constantToString(constants[protoIndex]).c_str());
            }
            std::printf("\n");
            // Skip protoIndex + upvalueCount + upvalue descriptors (2 bytes each)
            return offset + 2 + 1 + static_cast<size_t>(upvalueCount) * 2;
        }
        case OpCode::OP_CLASS: {
            uint8_t classIndex = code[offset + 1];
            uint8_t methodCount = code[offset + 2];
            std::printf("%-16s %4d methods=%d", opcodeName(instruction), classIndex, methodCount);
            if (classIndex < constants.size()) {
                std::printf(" %s", constantToString(constants[classIndex]).c_str());
            }
            std::printf("\n");
            // Skip classIndex + methodCount + method indices
            return offset + 2 + 1 + static_cast<size_t>(methodCount);
        }
        case OpCode::OP_CALL:
        case OpCode::OP_TAIL_CALL:
        case OpCode::OP_CALL_N:
        case OpCode::OP_ARRAY:
        case OpCode::OP_DICT: {
            uint8_t val = code[offset + 1];
            std::printf("%-16s %4d\n", opcodeName(instruction), val);
            return offset + 2;
        }
        case OpCode::OP_DEFAULT_PARAM: {
            uint8_t slot = code[offset + 1];
            uint16_t skipOffset = static_cast<uint16_t>(code[offset + 2])
                                | (static_cast<uint16_t>(code[offset + 3]) << 8);
            std::printf("%-16s slot %d skip=%d -> %zu\n", opcodeName(instruction),
                       slot, skipOffset, offset + 4 + skipOffset);
            return offset + 4;
        }
        case OpCode::OP_JUMP:
        case OpCode::OP_JUMP_IF_FALSE:
        case OpCode::OP_JUMP_IF_NULL: {
            uint16_t jumpOffset = static_cast<uint16_t>(code[offset + 1])
                                | (static_cast<uint16_t>(code[offset + 2]) << 8);
            std::printf("%-16s %4d -> %zu\n", opcodeName(instruction),
                       jumpOffset, offset + 3 + jumpOffset);
            return offset + 3;
        }
        case OpCode::OP_PUSH_CATCH: {
            uint8_t localCount = code[offset + 1];
            uint16_t catchOffset = static_cast<uint16_t>(code[offset + 2])
                                  | (static_cast<uint16_t>(code[offset + 3]) << 8);
            std::printf("%-16s %u %4d -> %zu\n", opcodeName(instruction),
                       localCount, catchOffset, offset + 4 + catchOffset);
            return offset + 4;
        }
        case OpCode::OP_LOOP: {
            uint16_t loopOffset = static_cast<uint16_t>(code[offset + 1])
                                | (static_cast<uint16_t>(code[offset + 2]) << 8);
            std::printf("%-16s %4d -> %zu\n", opcodeName(instruction),
                       loopOffset, offset + 3 - loopOffset);
            return offset + 3;
        }
        case OpCode::OP_CONVERT: {
            uint8_t typeTag = code[offset + 1];
            static const char* typeNames[] = {"float", "int", "bool", "str"};
            const char* name = (typeTag < 4) ? typeNames[typeTag] : "?";
            std::printf("%-16s %s\n", opcodeName(instruction), name);
            return offset + 2;
        }
        default:
            std::printf("%s\n", opcodeName(instruction));
            return offset + 1;
    }
}

// =========================================================================
// Source line printer — used by VM, compiler, parser, and lexer errors
// =========================================================================

void printSourceLine(std::ostream& out,
                     const std::string& source,
                     int line, int column, int length,
                     const std::string& label,
                     const std::string& prefix) {
    if (source.empty() || line < 1 || column < 1) return;

    // Extract line `line` from source (1-indexed).
    size_t pos = 0;
    int currentLine = 1;
    while (currentLine < line && pos < source.size()) {
        if (source[pos] == '\n') currentLine++;
        pos++;
    }
    if (currentLine != line) return;  // line not found

    size_t lineStart = pos;
    size_t lineEnd = source.find('\n', lineStart);
    if (lineEnd == std::string::npos) lineEnd = source.size();
    std::string sourceLine = source.substr(lineStart, lineEnd - lineStart);

    // Replace tabs with spaces in display to keep caret alignment correct.
    // We keep a parallel translation so column mapping works.
    std::string displayLine;
    int displayCol = 1;
    for (size_t i = 0; i < sourceLine.size(); i++) {
        if (sourceLine[i] == '\t') {
            displayLine += "    ";  // 4-space tab stop
            if (static_cast<int>(i + 1) < column) displayCol += 4;
        } else {
            displayLine += sourceLine[i];
            if (static_cast<int>(i + 1) < column) displayCol++;
        }
    }

    int caretCol = displayCol;

    // Print header
    out << "  --> " << line << ":" << column << "\n";

    // Print gutter + ruler line
    out << "     |\n";

    // Print line number + source
    // Right-align line number in 3-char field, but limit to 999 lines.
    if (line < 1000) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%3d", line);
        out << " " << buf << " | " << displayLine << "\n";
    } else {
        out << " ... | " << displayLine << "\n";
    }

    // Print caret(s)
    out << "     | ";
    for (int i = 1; i < caretCol; i++) out << ' ';
    if (length <= 1) {
        out << "^\n";
    } else {
        out << '^';
        for (int i = 1; i < length && (caretCol + i) <= static_cast<int>(displayLine.size()) + 1; i++) {
            out << '~';
        }
        out << '\n';
    }

    // Print label
    out << "     = " << prefix << ": " << label << "\n";
}

} // namespace vora
