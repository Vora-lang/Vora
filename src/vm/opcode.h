#pragma once

#include <cstdint>

namespace vora {

enum class OpCode : uint8_t {
    // Constants & literals
    OP_CONSTANT,       // push constant from pool (operand: uint8_t index)
    OP_CONSTANT_LONG,  // push constant from pool (operand: uint16_t index, little-endian)
    OP_NULL,           // push null
    OP_TRUE,        // push true
    OP_FALSE,       // push false

    // Stack
    OP_POP,         // pop top of stack
    OP_DUP,         // duplicate top of stack
    OP_PRINT_POP,   // print top of stack (if non-null) and pop — used by REPL

    // Unary
    OP_NEGATE,      // negate top of stack (number)
    OP_NOT,         // logical not top of stack

    // Binary arithmetic
    OP_ADD,         // a + b  (general: numbers, strings, arrays)
    OP_DIVIDE,      // a / b
    OP_MODULO,      // a % b
    OP_POWER,       // a ** b

    // Fast numeric-only arithmetic (skip type checks, operands must be double)
    OP_SUB_NN,      // number - number
    OP_MUL_NN,      // number * number
    OP_DIV_NN,      // number / number
    OP_MOD_NN,      // number % number
    OP_LESS_NN,     // number < number
    OP_LESS_EQ_NN,  // number <= number
    OP_GREATER_NN,  // number > number
    OP_GREATER_EQ_NN, // number >= number

    // Comparison
    OP_EQUAL,       // a == b
    OP_NOT_EQUAL,   // a != b
    OP_LESS,        // a < b  (general, kept for compatibility)

    // Locals
    OP_GET_LOCAL,      // push local var (operand: uint8_t slot index)
    OP_SET_LOCAL,      // set local var, leave value on stack (operand: uint8_t slot index)
    OP_POPN,           // pop N values from stack (operand: uint8_t count)

    // Globals
    OP_DEFINE_GLOBAL,     // define global var (operand: uint8_t name index)
    OP_GET_GLOBAL,        // push global var (operand: uint8_t name index)
    OP_SET_GLOBAL,        // set global var, leave value on stack (operand: uint8_t name index)
    OP_GET_GLOBAL_SAFE,   // push global var, or fallback if undefined
                          // (operand: uint8_t nameIndex, uint8_t fallbackIndex)

    // Control flow
    OP_JUMP,           // unconditional jump (operand: int16_t offset)
    OP_JUMP_IF_FALSE,  // pop + conditional jump (operand: int16_t offset)
    OP_LOOP,           // jump backward (operand: int16_t offset)

    // Functions
    OP_CALL,           // call function (operand: uint8_t argCount)
    OP_TAIL_CALL,      // tail call — reuses current frame (operand: uint8_t argCount)
    OP_CLOSURE,        // create closure from function prototype (operand: uint8_t constIndex, then N upvalue pairs)
    OP_RETURN,         // return from function
    OP_YIELD,           // yield from generator — saves state, returns to caller

    // Upvalues (closures)
    OP_GET_UPVALUE,    // push upvalue (operand: uint8_t upvalue index)
    OP_SET_UPVALUE,    // set upvalue, leave value on stack (operand: uint8_t upvalue index)
    OP_CLOSE_UPVALUE,  // move local to heap (operand: uint8_t local slot)

    // Arrays / Dicts
    OP_ARRAY,          // create array from N stack values (operand: uint8_t count)
    OP_DICT,           // create dict from N*2 stack values (operand: uint8_t pairCount)
    OP_INDEX,          // index access a[b] — pops index, pops target, pushes result
    OP_SET_INDEX,      // index assignment a[b] = v — pops value, index, target, pushes value

    // Objects
    OP_GET_PROPERTY,   // property access (operand: uint8_t name index)
    OP_SET_PROPERTY,   // property assignment (operand: uint8_t name index)
    OP_CLASS,          // create class from prototype (operand: uint8_t constIndex)
    OP_GET_SUPER,      // super.method — resolve method from parent class (operand: uint8_t nameIndex)

    // Default parameters
    OP_DEFAULT_PARAM,  // check if param was passed, skip default eval if so
                       // (operand: uint8_t slot, uint16_t skipOffset)

    // Exception handling
    OP_PUSH_CATCH,     // push catch handler info (operand: uint16_t catch offset)
    OP_POP_CATCH,      // pop catch handler
    OP_CLEAR_EXCEPTION, // clear exceptionInFlight flag (catch block entry)
    OP_THROW,          // throw exception (value on stack top)
    OP_FINALLY_END,    // marker for finally block end

    // Module system
    OP_IMPORT,         // import module → push module dict (operand: uint8_t pathConstIndex, uint8_t nameConstIndex)
};

} // namespace vora
