/**
 * @file opcode.h
 * @brief Bytecode instruction set for the Vora stack-based virtual machine.
 *
 * @details
 * Defines the OpCode enum with 50+ instructions covering constants, stack
 * manipulation, arithmetic, locals/globals, control flow, function calls,
 * closures, objects, exceptions, modules, and type conversion.  Includes
 * wide (16-bit operand) variants and superinstruction pairs for reduced
 * dispatch overhead.
 *
 * Each opcode is a single byte (`uint8_t`). Most instructions consume zero
 * or more operands from the bytecode stream immediately following the
 * opcode. Operands are encoded in little-endian byte order for multi-byte
 * values.
 *
 * @see Compiler (emits these opcodes), VM::run() (dispatches on them)
 */

#pragma once

#include <cstdint>

namespace vora {

/**
 * @brief Bytecode instruction opcodes for the Vora virtual machine.
 *
 * The opcode set is partitioned into logical groups:
 * - **Constants & literals** (OP_CONSTANT .. OP_FALSE): push constant values.
 * - **Stack** (OP_POP .. OP_PRINT_POP): manipulate the evaluation stack.
 * - **Unary** (OP_NEGATE, OP_NOT): single-operand operations.
 * - **Binary arithmetic** (OP_ADD .. OP_POWER): general arithmetic.
 * - **Fast numeric-only arithmetic** (OP_SUB_NN .. OP_GREATER_EQ_NN):
 *   type-specialized arithmetic that assumes `double` operands.
 * - **Comparison** (OP_EQUAL .. OP_LESS): equality and ordering.
 * - **Locals** (OP_GET_LOCAL .. OP_POPN): local variable access and bulk pop.
 * - **Superinstructions** (OP_GET_LOCAL_PROP .. OP_GET_GLOBAL_PROP_WIDE):
 *   fused opcode pairs that reduce dispatch overhead.
 * - **Globals** (OP_DEFINE_GLOBAL .. OP_GET_GLOBAL_SAFE_WIDE):
 *   global variable definition and access.
 * - **Control flow** (OP_JUMP .. OP_LOOP): branching and looping.
 * - **Functions** (OP_CALL .. OP_YIELD): calls, closures, returns, generators.
 * - **Upvalues** (OP_GET_UPVALUE .. OP_CLOSE_UPVALUE): closure captured variables.
 * - **Defer** (OP_DEFER_PUSH, OP_DEFER_FLUSH): deferred function execution.
 * - **Arrays / Dicts** (OP_ARRAY .. OP_SET_INDEX): container creation and access.
 * - **Objects** (OP_GET_PROPERTY .. OP_GET_SUPER): property access and class creation.
 * - **Default parameters** (OP_DEFAULT_PARAM): optional parameter support.
 * - **Exception handling** (OP_PUSH_CATCH .. OP_FINALLY_END): try/catch/finally.
 * - **Null safety** (OP_JUMP_IF_NULL): null-check branching.
 * - **Module system** (OP_IMPORT, OP_IMPORT_WIDE): module loading.
 * - **Wide globals** (OP_DEFINE_GLOBAL_WIDE .. OP_SET_GLOBAL_WIDE):
 *   16-bit global slot variants for programs with more than 256 globals.
 * - **Type conversion** (OP_CONVERT): type-annotated declaration coercion.
 * - **Wide variants** (OP_GET_PROPERTY_WIDE .. OP_CALL_KW_WIDE):
 *   16-bit constant pool index variants for large programs.
 * - **Null-safe access** (OP_GET_PROPERTY_SAFE .. OP_INDEX_SAFE):
 *   optional chaining property/index access.
 */
enum class OpCode : uint8_t {
    // ─────────────────────────────────────────────────────────────────────
    // Constants & literals
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Push a constant from the constant pool onto the stack.
    /// @details Operand: `uint8_t` index into the chunk's constant pool.
    OP_CONSTANT,

    /// @brief Push a constant from the constant pool using a 16-bit index.
    /// @details Operand: `uint16_t` index (little-endian) into the chunk's
    /// constant pool. Used when the pool exceeds 256 entries.
    OP_CONSTANT_LONG,

    /// @brief Push the `null` value onto the stack.
    OP_NULL,

    /// @brief Push the boolean `true` value onto the stack.
    OP_TRUE,

    /// @brief Push the boolean `false` value onto the stack.
    OP_FALSE,

    // ─────────────────────────────────────────────────────────────────────
    // Stack manipulation
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Pop the top value off the stack and discard it.
    OP_POP,

    /// @brief Duplicate the top-of-stack value.
    OP_DUP,

    /// @brief Print the top-of-stack value (if non-null) and then pop it.
    /// @details Used by the REPL to display expression results. No-op if the
    /// value is `null`.
    OP_PRINT_POP,

    // ─────────────────────────────────────────────────────────────────────
    // Unary operators
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Negate the top-of-stack value (numeric negation, unary `-`).
    OP_NEGATE,

    /// @brief Logical NOT the top-of-stack value (unary `!`).
    OP_NOT,

    // ─────────────────────────────────────────────────────────────────────
    // Binary arithmetic (general)
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Add two values (`a + b`).
    /// @details General-purpose addition: numbers (arithmetic), strings
    /// (concatenation), arrays (concatenation).
    OP_ADD,

    /// @brief Divide two values (`a / b`). General-purpose division.
    OP_DIVIDE,

    /// @brief Modulo of two values (`a % b`). General-purpose remainder.
    OP_MODULO,

    /// @brief Exponentiation (`a ** b`). General-purpose power.
    OP_POWER,

    // ─────────────────────────────────────────────────────────────────────
    // Fast numeric-only arithmetic
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Subtract two numbers (`number - number`).
    /// @details Type-specialized: assumes both operands are `double`. Skips
    /// runtime type checks for performance.
    OP_SUB_NN,

    /// @brief Multiply two numbers (`number * number`).
    /// @details Type-specialized: assumes both operands are `double`.
    OP_MUL_NN,

    /// @brief Divide two numbers (`number / number`).
    /// @details Type-specialized: assumes both operands are `double`.
    OP_DIV_NN,

    /// @brief Modulo of two numbers (`number % number`).
    /// @details Type-specialized: assumes both operands are `double`.
    OP_MOD_NN,

    /// @brief Less-than comparison of two numbers (`number < number`).
    /// @details Type-specialized: assumes both operands are `double`.
    OP_LESS_NN,

    /// @brief Less-than-or-equal comparison of two numbers (`number <= number`).
    /// @details Type-specialized: assumes both operands are `double`.
    OP_LESS_EQ_NN,

    /// @brief Greater-than comparison of two numbers (`number > number`).
    /// @details Type-specialized: assumes both operands are `double`.
    OP_GREATER_NN,

    /// @brief Greater-than-or-equal comparison of two numbers (`number >= number`).
    /// @details Type-specialized: assumes both operands are `double`.
    OP_GREATER_EQ_NN,

    // ─────────────────────────────────────────────────────────────────────
    // Comparison
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Equality comparison (`a == b`).
    /// @details General-purpose: handles all Value types with structural
    /// equality for containers.
    OP_EQUAL,

    /// @brief Inequality comparison (`a != b`).
    OP_NOT_EQUAL,

    /// @brief Less-than comparison (`a < b`).
    /// @details General-purpose comparison (retained for compatibility with
    /// non-numeric types; prefer OP_LESS_NN for numeric-only contexts).
    OP_LESS,

    // ─────────────────────────────────────────────────────────────────────
    // Local variables
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Push a local variable onto the stack.
    /// @details Operand: `uint8_t` slot index into the current frame's local
    /// variable array.
    OP_GET_LOCAL,

    /// @brief Set a local variable and leave the value on the stack.
    /// @details Operand: `uint8_t` slot index. The top-of-stack value is
    /// copied into the local slot; the value remains on the stack for use
    /// as an expression result.
    OP_SET_LOCAL,

    /// @brief Pop N values from the stack.
    /// @details Operand: `uint8_t` count of values to discard. Used for
    /// bulk cleanup (e.g., expression-statement results).
    OP_POPN,

    // ─────────────────────────────────────────────────────────────────────
    // Superinstructions — fused opcode pairs
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Fused `OP_GET_LOCAL` + `OP_GET_PROPERTY`.
    /// @details Reduces dispatch overhead for the common `local.prop` pattern.
    /// Operands: `uint8_t` localSlot, `uint8_t` nameIndex.
    OP_GET_LOCAL_PROP,

    /// @brief Fused `OP_GET_LOCAL` + `OP_GET_PROPERTY_WIDE` (16-bit name).
    /// @details Operands: `uint8_t` localSlot, `uint16_t` nameIndex (LE).
    OP_GET_LOCAL_PROP_WIDE,

    /// @brief Fused `OP_GET_GLOBAL` + `OP_GET_PROPERTY`.
    /// @details Reduces dispatch overhead for the common `global.prop` pattern.
    /// Operands: `uint8_t` globalSlot, `uint8_t` nameIndex.
    OP_GET_GLOBAL_PROP,

    /// @brief Fused `OP_GET_GLOBAL_WIDE` + `OP_GET_PROPERTY`.
    /// @details Operands: `uint16_t` globalSlot (LE), `uint8_t` nameIndex.
    OP_GET_GLOBAL_PROP_WIDE,

    // ─────────────────────────────────────────────────────────────────────
    // Global variables
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Define a new global variable.
    /// @details Operand: `uint8_t` name index into the constant pool.
    /// Registers the slot and initializes the value to `null`.
    OP_DEFINE_GLOBAL,

    /// @brief Push a global variable's value onto the stack.
    /// @details Operand: `uint8_t` slot index. Runtime error if the slot
    /// has not been defined.
    OP_GET_GLOBAL,

    /// @brief Set a global variable and leave the value on the stack.
    /// @details Operand: `uint8_t` slot index. The value is stored and also
    /// remains on the stack for use as an expression result.
    OP_SET_GLOBAL,

    /// @brief Push a global variable, falling back to an alternative if undefined.
    /// @details Operands: `uint8_t` nameIndex, `uint8_t` fallbackIndex.
    /// If the nameIndex slot is defined, its value is pushed; otherwise the
    /// fallbackIndex value is used. Used to implement the `||` default pattern.
    OP_GET_GLOBAL_SAFE,

    // ─────────────────────────────────────────────────────────────────────
    // Control flow
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Unconditional forward jump.
    /// @details Operand: `int16_t` byte offset (little-endian) from the
    /// instruction's end to the target.
    OP_JUMP,

    /// @brief Pop the top-of-stack and conditionally jump forward.
    /// @details Operand: `int16_t` byte offset (little-endian). If the
    /// popped value is falsy, jump to the target; otherwise fall through.
    OP_JUMP_IF_FALSE,

    /// @brief Unconditional backward jump (loop back-edge).
    /// @details Operand: `int16_t` negative byte offset (little-endian).
    /// The offset points backward from the instruction's end to the loop head.
    OP_LOOP,

    // ─────────────────────────────────────────────────────────────────────
    // Functions
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Call a function or callable value.
    /// @details Operand: `uint8_t` argument count. Pops the callee and
    /// `argCount` arguments from the stack, then pushes the return value.
    OP_CALL,

    /// @brief Tail-call a function, reusing the current stack frame.
    /// @details Operand: `uint8_t` argument count. Instead of pushing a new
    /// call frame, the current frame is replaced. Used for tail-call
    /// optimization to avoid stack overflow in recursive functions.
    OP_TAIL_CALL,

    /// @brief Call a function with keyword (named) arguments.
    /// @details Operands: `uint8_t` posCount, `uint8_t` kwCount, followed by
    /// `kwCount` `uint8_t` name indices into the constant pool. Pops the
    /// callee, positional args and keyword args from the stack.
    OP_CALL_KW,

    /// @brief Create a closure from a function prototype.
    /// @details Operand: `uint8_t` constant pool index of the
    /// `FunctionPrototype`, followed by `N` upvalue descriptor pairs. Each
    /// pair is `(uint8_t isLocal, uint8_t index)` specifying whether the
    /// captured variable is local (1) or an upvalue of the enclosing
    /// function (0) and its index.
    OP_CLOSURE,

    /// @brief Return from the current function.
    /// @details If the stack is non-empty, pops the top value as the return
    /// value. Otherwise returns `null`. Restores the caller's frame and
    /// resumes execution after the corresponding `OP_CALL`.
    OP_RETURN,

    // ─────────────────────────────────────────────────────────────────────
    // Call-site spread
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Push a new spread-count entry (initialized to 0) onto the
    /// spread-count stack.
    /// @details Marks the beginning of a call-site spread expression group.
    /// Used in conjunction with OP_SPREAD and OP_CALL_N.
    OP_PUSH_SPREAD,

    /// @brief Pop an array from the stack and push its elements individually.
    /// @details Increments the topmost spread-count entry by the array's
    /// length. Used for `f(...args)` call-site spreading.
    OP_SPREAD,

    /// @brief Call a function with a dynamic argument count (from spread).
    /// @details Operand: `uint8_t` fixedCount — the number of positional
    /// arguments before any spread arguments. The total argument count is
    /// `fixedCount + topSpreadCount`.
    OP_CALL_N,

    /// @brief Yield from a generator function.
    /// @details Saves the current execution state (IP, stack), pops the
    /// yield value, and returns control to the caller. The next call to the
    /// generator resumes at the instruction following this opcode.
    OP_YIELD,

    // ─────────────────────────────────────────────────────────────────────
    // Upvalues (closure captured variables)
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Push an upvalue onto the stack.
    /// @details Operand: `uint8_t` upvalue index in the current function's
    /// upvalue array. Upvalues are variables captured from enclosing scopes.
    OP_GET_UPVALUE,

    /// @brief Set an upvalue and leave the value on the stack.
    /// @details Operand: `uint8_t` upvalue index. The value is stored into
    /// the upvalue slot and remains on the stack.
    OP_SET_UPVALUE,

    /// @brief Close an upvalue by moving a local variable to the heap.
    /// @details Operand: `uint8_t` local slot index. When a scope exits,
    /// any local variable still referenced by a closure must be moved from
    /// the stack to a heap-allocated `Upvalue` object so it outlives the
    /// stack frame.
    OP_CLOSE_UPVALUE,

    // ─────────────────────────────────────────────────────────────────────
    // Defer
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Pop a closure from the operand stack and push it onto the
    /// current frame's defer stack.
    /// @details Deferred closures are executed in LIFO order when the
    /// enclosing scope exits (via OP_DEFER_FLUSH). Used to implement
    /// `defer { ... }` blocks.
    OP_DEFER_PUSH,

    /// @brief Pop all deferred closures from the current frame's defer
    /// stack (LIFO order) and call each one.
    /// @details Emitted at scope exit points (end of block, return, throw
    /// through scope) to run deferred cleanup code.
    OP_DEFER_FLUSH,

    // ─────────────────────────────────────────────────────────────────────
    // Arrays / Dicts
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Create an array from the top N stack values.
    /// @details Operand: `uint8_t` count. Pops `count` values, creates an
    /// `Array` containing them (in stack order, so the deepest value becomes
    /// element 0), and pushes the array.
    OP_ARRAY,

    /// @brief Create a dict from the top N*2 stack values.
    /// @details Operand: `uint8_t` pairCount. Pops `pairCount * 2` values
    /// as alternating key-value pairs (key first, then value), creates a
    /// `Dict`, and pushes it.
    OP_DICT,

    /// @brief Index access (`a[b]`).
    /// @details Pops the index, then pops the target (array/dict/string),
    /// and pushes the result. Runtime error if the target does not support
    /// indexing.
    OP_INDEX,

    /// @brief Index assignment (`a[b] = v`).
    /// @details Pops the value, then the index, then the target. Stores the
    /// value into the container and pushes the value back onto the stack.
    OP_SET_INDEX,

    // ─────────────────────────────────────────────────────────────────────
    // Objects
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Property access (`obj.prop`).
    /// @details Operand: `uint8_t` name index into the constant pool. Pops
    /// the object, looks up the property, and pushes the result. Falls back
    /// to indexing (`obj["prop"]`) if no named property is found.
    OP_GET_PROPERTY,

    /// @brief Property assignment (`obj.prop = v`).
    /// @details Operand: `uint8_t` name index into the constant pool. Pops
    /// the value and the object, sets the property, and pushes the value
    /// back.
    OP_SET_PROPERTY,

    /// @brief Create a class from a prototype definition.
    /// @details Operand: `uint8_t` constant pool index of the
    /// `ClassDefinition`. Resolves parent classes, computes the C3 MRO
    /// (method resolution order), and pushes the resulting class object.
    OP_CLASS,

    /// @brief Resolve a method from a parent class (`super.method`).
    /// @details Operand: `uint8_t` name index. Looks up the method on the
    /// superclass, bypassing the current class's own methods. Pops the
    /// instance and pushes the bound method.
    OP_GET_SUPER,

    // ─────────────────────────────────────────────────────────────────────
    // Default parameters
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Check whether a parameter was passed; skip default evaluation
    /// if so.
    /// @details Operands: `uint8_t` local slot index, `uint16_t` skip
    /// offset (little-endian). If the specified local slot is not `null`
    /// (meaning the caller provided a value), jump forward by the skip
    /// offset past the default-value evaluation code.
    OP_DEFAULT_PARAM,

    // ─────────────────────────────────────────────────────────────────────
    // Exception handling
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Register a catch handler on the catch-handler stack.
    /// @details Operand: `uint16_t` forward byte offset (little-endian) from
    /// the current IP to the catch block entry point. When `OP_THROW` is
    /// executed, the VM walks the handler stack to find the innermost
    /// handler and jumps to its target offset.
    OP_PUSH_CATCH,

    /// @brief Pop the topmost catch handler from the catch-handler stack.
    /// @details Used on normal exit from a try block and at catch block
    /// entry for housekeeping.
    OP_POP_CATCH,

    /// @brief Clear the `exceptionInFlight` VM flag.
    /// @details Emitted at catch block entry. After this, the caught
    /// exception value is on the stack and normal execution resumes.
    /// This prevents `OP_FINALLY_END` from re-throwing a caught exception.
    OP_CLEAR_EXCEPTION,

    /// @brief Throw an exception.
    /// @details Pops the exception value from the stack top, walks the
    /// catch-handler stack to find the innermost handler, sets the IP to
    /// the handler's target, pushes the exception value onto the stack,
    /// and sets `exceptionInFlight = true`.
    OP_THROW,

    /// @brief Marker for the end of a finally block.
    /// @details If `exceptionInFlight` is true, pops the exception value and
    /// re-throws it (propagating to the next outer handler). Otherwise
    /// (normal flow), this is a no-op.
    OP_FINALLY_END,

    // ─────────────────────────────────────────────────────────────────────
    // Null safety
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Pop the top-of-stack and conditionally jump forward if it is
    /// `null`.
    /// @details Operand: `uint16_t` byte offset (little-endian). Used for
    /// null-coalescing and optional-chaining control flow.
    OP_JUMP_IF_NULL,

    // ─────────────────────────────────────────────────────────────────────
    // Module system
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Import a module and push the module dict onto the stack.
    /// @details Operands: `uint8_t` pathConstIndex (constant pool index of
    /// the module path string), `uint8_t` nameConstIndex (constant pool
    /// index of the import identifier). Loads and caches the module.
    OP_IMPORT,

    // ─────────────────────────────────────────────────────────────────────
    // Globals — wide variants (16-bit slot index for >256 globals)
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Define a global variable with a 16-bit slot index.
    /// @details Operand: `uint16_t` slot index (little-endian). Used when
    /// the number of global variables exceeds 255.
    OP_DEFINE_GLOBAL_WIDE,

    /// @brief Push a global variable with a 16-bit slot index.
    /// @details Operand: `uint16_t` slot index (little-endian).
    OP_GET_GLOBAL_WIDE,

    /// @brief Set a global variable with a 16-bit slot index, leaving the
    /// value on the stack.
    /// @details Operand: `uint16_t` slot index (little-endian).
    OP_SET_GLOBAL_WIDE,

    // ─────────────────────────────────────────────────────────────────────
    // Type conversion
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Convert the top-of-stack value to a specified type.
    /// @details Operand: `uint8_t` type tag. Used for type-annotated
    /// declarations (e.g., `let x: float = ...`) to coerce the initializer
    /// value to the declared type.
    OP_CONVERT,

    // ─────────────────────────────────────────────────────────────────────
    // Wide (16-bit constant pool index) variants
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Property access with a 16-bit name index.
    /// @details Operand: `uint16_t` name index (little-endian) into the
    /// constant pool. Used when the constant pool exceeds 256 entries.
    OP_GET_PROPERTY_WIDE,

    /// @brief Property assignment with a 16-bit name index.
    /// @details Operand: `uint16_t` name index (little-endian).
    OP_SET_PROPERTY_WIDE,

    /// @brief Super method resolution with a 16-bit name index.
    /// @details Operand: `uint16_t` name index (little-endian).
    OP_GET_SUPER_WIDE,

    /// @brief Closure creation with a 16-bit prototype constant index.
    /// @details Operand: `uint16_t` prototype index (little-endian),
    /// followed by N upvalue descriptor pairs.
    OP_CLOSURE_WIDE,

    /// @brief Class creation with a 16-bit class-definition constant index.
    /// @details Operand: `uint16_t` constant pool index (little-endian).
    OP_CLASS_WIDE,

    /// @brief Module import with 16-bit path and name constant indices.
    /// @details Operands: `uint16_t` path index (little-endian),
    /// `uint16_t` name index (little-endian).
    OP_IMPORT_WIDE,

    /// @brief Safe global get with 16-bit slot and fallback indices.
    /// @details Operands: `uint16_t` slot index (little-endian),
    /// `uint16_t` fallback index (little-endian).
    OP_GET_GLOBAL_SAFE_WIDE,

    /// @brief Keyword-argument call with 16-bit name indices.
    /// @details Operands: `uint8_t` posCount, `uint8_t` kwCount, followed by
    /// `kwCount` `uint16_t` name indices (little-endian).
    OP_CALL_KW_WIDE,

    // ─────────────────────────────────────────────────────────────────────
    // Null-safe property/index access (optional chaining)
    // ─────────────────────────────────────────────────────────────────────

    /// @brief Null-safe property access (`obj?.prop`).
    /// @details Operand: `uint8_t` name index. If the object is `null`,
    /// pushes `null` instead of throwing an error. Otherwise behaves like
    /// `OP_GET_PROPERTY`.
    OP_GET_PROPERTY_SAFE,

    /// @brief Null-safe property access with a 16-bit name index.
    /// @details Operand: `uint16_t` name index (little-endian).
    OP_GET_PROPERTY_SAFE_WIDE,

    /// @brief Null-safe index access (`obj?.[key]`).
    /// @details If the target is `null`, pushes `null` instead of throwing
    /// an error. Otherwise behaves like `OP_INDEX`.
    OP_INDEX_SAFE,
};

} // namespace vora
