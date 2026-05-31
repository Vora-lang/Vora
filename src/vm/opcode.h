#pragma once

#include <cstdint>

namespace vora {

enum class OpCode : uint8_t {
    // Constants & literals
    OP_CONSTANT,    // push constant from pool (operand: uint8_t index)
    OP_NULL,        // push null
    OP_TRUE,        // push true
    OP_FALSE,       // push false

    // Stack
    OP_POP,         // pop top of stack

    // Unary
    OP_NEGATE,      // negate top of stack (number)
    OP_NOT,         // logical not top of stack

    // Binary arithmetic
    OP_ADD,         // a + b
    OP_SUBTRACT,    // a - b
    OP_MULTIPLY,    // a * b
    OP_DIVIDE,      // a / b
    OP_MODULO,      // a % b
    OP_POWER,       // a ** b

    // Comparison
    OP_EQUAL,       // a == b
    OP_NOT_EQUAL,   // a != b
    OP_LESS,        // a < b
    OP_LESS_EQUAL,  // a <= b
    OP_GREATER,     // a > b
    OP_GREATER_EQUAL, // a >= b

    // Globals
    OP_DEFINE_GLOBAL,  // define global var (operand: uint8_t name index)
    OP_GET_GLOBAL,     // push global var (operand: uint8_t name index)
    OP_SET_GLOBAL,     // set global var, leave value on stack (operand: uint8_t name index)

    // Control flow
    OP_JUMP,           // unconditional jump (operand: int16_t offset)
    OP_JUMP_IF_FALSE,  // pop + conditional jump (operand: int16_t offset)
    OP_LOOP,           // jump backward (operand: int16_t offset)

    // Functions (Phase 2)
    OP_CALL,        // call function (operand: uint8_t argCount)
    OP_RETURN,      // return from function

    // Arrays (Phase 1+)
    OP_ARRAY,       // create array from N stack values (operand: uint8_t count)
    OP_INDEX,       // index access a[b] — pops index, pops target, pushes result
    OP_SET_INDEX,   // index assignment a[b] = v — pops value, index, target, pushes value

    // Objects (Phase 3)
    OP_GET_PROPERTY,   // property access (operand: uint8_t name index)
    OP_SET_PROPERTY,   // property assignment (operand: uint8_t name index)
};

} // namespace vora
