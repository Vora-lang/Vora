#include "chunk.h"
#include "opcode.h"
#include "compiler.h"

#include <cstdio>
#include <cmath>

namespace vora {

// =========================================================================
// OpCode name table
// =========================================================================

static const char* opcodeName(OpCode op) {
    switch (op) {
        case OpCode::OP_CONSTANT:       return "OP_CONSTANT";
        case OpCode::OP_NULL:           return "OP_NULL";
        case OpCode::OP_TRUE:           return "OP_TRUE";
        case OpCode::OP_FALSE:          return "OP_FALSE";
        case OpCode::OP_POP:            return "OP_POP";
        case OpCode::OP_POPN:           return "OP_POPN";
        case OpCode::OP_NEGATE:         return "OP_NEGATE";
        case OpCode::OP_NOT:            return "OP_NOT";
        case OpCode::OP_ADD:            return "OP_ADD";
        case OpCode::OP_SUBTRACT:       return "OP_SUBTRACT";
        case OpCode::OP_MULTIPLY:       return "OP_MULTIPLY";
        case OpCode::OP_DIVIDE:         return "OP_DIVIDE";
        case OpCode::OP_MODULO:         return "OP_MODULO";
        case OpCode::OP_POWER:          return "OP_POWER";
        case OpCode::OP_EQUAL:          return "OP_EQUAL";
        case OpCode::OP_NOT_EQUAL:      return "OP_NOT_EQUAL";
        case OpCode::OP_LESS:           return "OP_LESS";
        case OpCode::OP_LESS_EQUAL:     return "OP_LESS_EQUAL";
        case OpCode::OP_GREATER:        return "OP_GREATER";
        case OpCode::OP_GREATER_EQUAL:  return "OP_GREATER_EQUAL";
        case OpCode::OP_GET_LOCAL:      return "OP_GET_LOCAL";
        case OpCode::OP_SET_LOCAL:      return "OP_SET_LOCAL";
        case OpCode::OP_DEFINE_GLOBAL:  return "OP_DEFINE_GLOBAL";
        case OpCode::OP_GET_GLOBAL:     return "OP_GET_GLOBAL";
        case OpCode::OP_SET_GLOBAL:     return "OP_SET_GLOBAL";
        case OpCode::OP_JUMP:           return "OP_JUMP";
        case OpCode::OP_JUMP_IF_FALSE:  return "OP_JUMP_IF_FALSE";
        case OpCode::OP_JUMP_IF_TRUE:   return "OP_JUMP_IF_TRUE";
        case OpCode::OP_LOOP:           return "OP_LOOP";
        case OpCode::OP_CALL:           return "OP_CALL";
        case OpCode::OP_CLOSURE:        return "OP_CLOSURE";
        case OpCode::OP_RETURN:         return "OP_RETURN";
        case OpCode::OP_ARRAY:          return "OP_ARRAY";
        case OpCode::OP_INDEX:          return "OP_INDEX";
        case OpCode::OP_SET_INDEX:      return "OP_SET_INDEX";
        case OpCode::OP_GET_PROPERTY:   return "OP_GET_PROPERTY";
        case OpCode::OP_SET_PROPERTY:   return "OP_SET_PROPERTY";
        case OpCode::OP_CLASS:          return "OP_CLASS";
        case OpCode::OP_PUSH_CATCH:     return "OP_PUSH_CATCH";
        case OpCode::OP_POP_CATCH:      return "OP_POP_CATCH";
        case OpCode::OP_THROW:          return "OP_THROW";
        case OpCode::OP_FINALLY_END:    return "OP_FINALLY_END";
    }
    return "OP_UNKNOWN";
}

// =========================================================================
// Simple constant value printing for disassembly
// =========================================================================

static std::string constantToString(const Value& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) return "null";
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? "true" : "false";
    if (std::holds_alternative<double>(value)) {
        double d = std::get<double>(value);
        if (std::floor(d) == d) return std::to_string(static_cast<int64_t>(d));
        return std::to_string(d);
    }
    if (std::holds_alternative<std::string>(value)) {
        return "'" + std::get<std::string>(value) + "'";
    }
    if (std::holds_alternative<std::shared_ptr<FunctionPrototype>>(value)) {
        return "<proto " + std::get<std::shared_ptr<FunctionPrototype>>(value)->name + ">";
    }
    if (std::holds_alternative<std::shared_ptr<ClassData>>(value)) {
        return "<class " + std::get<std::shared_ptr<ClassData>>(value)->name + ">";
    }
    return "[object]";
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
    for (size_t i = 0; i < constants.size(); i++) {
        const auto& existing = constants[i];
        if (existing.index() != value.index()) continue;

        if (std::holds_alternative<std::nullptr_t>(value)) { constants.push_back(value); return constants.size() - 1; }
        if (std::holds_alternative<bool>(value) && std::get<bool>(existing) == std::get<bool>(value)) return i;
        if (std::holds_alternative<double>(value) && std::get<double>(existing) == std::get<double>(value)) return i;
        if (std::holds_alternative<std::string>(value) && std::get<std::string>(existing) == std::get<std::string>(value)) return i;
    }

    constants.push_back(value);
    return constants.size() - 1;
}

void Chunk::writeConstant(Value value, int line, int column) {
    size_t index = addConstant(value);
    write(static_cast<uint8_t>(OpCode::OP_CONSTANT), line, column);
    write(static_cast<uint8_t>(index), line, column);
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
            std::printf("'%s'\n", constantToString(constants[index]).c_str());
            return offset + 2;
        }
        case OpCode::OP_GET_LOCAL:
        case OpCode::OP_SET_LOCAL:
        case OpCode::OP_POPN: {
            uint8_t index = code[offset + 1];
            std::printf("%-16s %4d\n", opcodeName(instruction), index);
            return offset + 2;
        }
        case OpCode::OP_DEFINE_GLOBAL:
        case OpCode::OP_GET_GLOBAL:
        case OpCode::OP_SET_GLOBAL:
        case OpCode::OP_GET_PROPERTY:
        case OpCode::OP_SET_PROPERTY: {
            uint8_t index = code[offset + 1];
            std::printf("%-16s %4d ", opcodeName(instruction), index);
            if (index < constants.size()) {
                std::printf("'%s'\n", constantToString(constants[index]).c_str());
            } else {
                std::printf("\n");
            }
            return offset + 2;
        }
        case OpCode::OP_CLOSURE:
        case OpCode::OP_CLASS:
        case OpCode::OP_CALL:
        case OpCode::OP_ARRAY: {
            uint8_t val = code[offset + 1];
            std::printf("%-16s %4d\n", opcodeName(instruction), val);
            return offset + 2;
        }
        case OpCode::OP_JUMP:
        case OpCode::OP_JUMP_IF_FALSE:
        case OpCode::OP_JUMP_IF_TRUE: {
            uint16_t jumpOffset = static_cast<uint16_t>(code[offset + 1])
                                | (static_cast<uint16_t>(code[offset + 2]) << 8);
            std::printf("%-16s %4d -> %zu\n", opcodeName(instruction),
                       jumpOffset, offset + 3 + jumpOffset);
            return offset + 3;
        }
        case OpCode::OP_PUSH_CATCH: {
            uint16_t catchOffset = static_cast<uint16_t>(code[offset + 1])
                                  | (static_cast<uint16_t>(code[offset + 2]) << 8);
            std::printf("%-16s %4d -> %zu\n", opcodeName(instruction),
                       catchOffset, offset + 3 + catchOffset);
            return offset + 3;
        }
        case OpCode::OP_LOOP: {
            uint16_t loopOffset = static_cast<uint16_t>(code[offset + 1])
                                | (static_cast<uint16_t>(code[offset + 2]) << 8);
            std::printf("%-16s %4d -> %zu\n", opcodeName(instruction),
                       loopOffset, offset + 3 - loopOffset);
            return offset + 3;
        }
        default:
            std::printf("%s\n", opcodeName(instruction));
            return offset + 1;
    }
}

} // namespace vora
