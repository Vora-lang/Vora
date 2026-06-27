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
    OP_CALL_KW,        // call with keyword (named) arguments
                       // Format: OP_CALL_KW <posCount:u8> <kwCount:u8> <nameIdx:u8>...
    OP_CLOSURE,        // create closure from function prototype (operand: uint8_t constIndex, then N upvalue pairs)
    OP_RETURN,         // return from function

    // Call-site spread
    OP_PUSH_SPREAD,    // push new spread count entry (0) onto spreadCountStack
    OP_SPREAD,         // pop array, push elements, increment spread count
    OP_CALL_N,         // call with dynamic arg count from spread (operand: uint8_t fixedCount)
    OP_YIELD,           // yield from generator — saves state, returns to caller

    // Upvalues (closures)
    OP_GET_UPVALUE,    // push upvalue (operand: uint8_t upvalue index)
    OP_SET_UPVALUE,    // set upvalue, leave value on stack (operand: uint8_t upvalue index)
    OP_CLOSE_UPVALUE,  // move local to heap (operand: uint8_t local slot)

    // Defer
    OP_DEFER_PUSH,     // pop closure from operand stack, push onto frame's defer stack
    OP_DEFER_FLUSH,    // pop all defer closures from frame's defer stack (LIFO), call each

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

    // Null safety
    OP_JUMP_IF_NULL,   // pop + conditional jump if top is null (operand: uint16_t offset)

    // Module system
    OP_IMPORT,         // import module → push module dict (operand: uint8_t pathConstIndex, uint8_t nameConstIndex)

    // Globals — wide (16-bit slot index for >256 globals)
    // Placed at end to avoid shifting existing opcode numeric values.
    OP_DEFINE_GLOBAL_WIDE,  // define global var (operand: uint16_t slot, LE)
    OP_GET_GLOBAL_WIDE,     // push global var (operand: uint16_t slot, LE)
    OP_SET_GLOBAL_WIDE,     // set global var, leave value on stack (operand: uint16_t slot, LE)

    // Type conversion for annotated declarations (let x:float = ...)
    OP_CONVERT,     // convert top of stack to type (operand: uint8_t type tag)

    // ── Wide (16-bit constant pool index) variants ──────────────────────────
    // Placed at end to avoid shifting existing opcode values.
    OP_GET_PROPERTY_WIDE,   // property access (operand: uint16_t nameIndex, LE)
    OP_SET_PROPERTY_WIDE,   // property assignment (operand: uint16_t nameIndex, LE)
    OP_GET_SUPER_WIDE,      // super.method (operand: uint16_t nameIndex, LE)
    OP_CLOSURE_WIDE,        // closure from prototype (operand: uint16_t protoIndex, LE, then N upvalue pairs)
    OP_CLASS_WIDE,          // class from prototype (operand: uint16_t classIndex, LE)
    OP_IMPORT_WIDE,         // import module (operand: uint16_t pathIndex LE, uint16_t nameIndex LE)
    OP_GET_GLOBAL_SAFE_WIDE,// safe global get (operand: uint16_t slot LE, uint16_t fallbackIndex LE)
    OP_CALL_KW_WIDE,        // named-arg call (operand: u8 posCount, u8 kwCount, uint16_t nameIdx... LE)

    // Null-safe property/index access for optional chaining (?. and ?.[])
    OP_GET_PROPERTY_SAFE,       // like OP_GET_PROPERTY but returns null on missing key
    OP_GET_PROPERTY_SAFE_WIDE,  // 16-bit nameIndex variant
    OP_INDEX_SAFE,              // like OP_INDEX but returns null on key/index not found
};

} // namespace vora
