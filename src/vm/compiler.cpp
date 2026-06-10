#include "compiler.h"

#include <cmath>
#include <iostream>
#include <variant>

#include "../runtime/vora_function.h"

namespace vora {

// =========================================================================
// Helpers: bytecode emission
// =========================================================================

void Compiler::emitByte(uint8_t byte) {
    chunk.write(byte, currentLine, currentColumn);
}

void Compiler::emitBytes(uint8_t a, uint8_t b) {
    chunk.write(a, b, currentLine, currentColumn);
}

void Compiler::emitConstant(Value value) {
    chunk.writeConstant(value, currentLine, currentColumn);
}

size_t Compiler::emitJump(OpCode instruction) {
    emitByte(static_cast<uint8_t>(instruction));
    size_t offset = chunk.code.size();
    emitByte(0xFF);
    emitByte(0xFF);
    return offset;
}

void Compiler::patchJump(size_t operandOffset) {
    size_t jumpEnd = chunk.code.size();
    size_t jumpSize = jumpEnd - operandOffset - 2;

    if (jumpSize > UINT16_MAX) {
        std::cerr << "Compiler error: jump offset " << jumpSize
                  << " exceeds 16-bit limit (UINT16_MAX=" << UINT16_MAX
                  << "). Function body too large." << std::endl;
        std::exit(1);
    }

    chunk.writeAt(operandOffset, static_cast<uint8_t>(jumpSize & 0xFF));
    chunk.writeAt(operandOffset + 1, static_cast<uint8_t>((jumpSize >> 8) & 0xFF));
}

void Compiler::emitLoop(size_t loopStart) {
    emitByte(static_cast<uint8_t>(OpCode::OP_LOOP));

    size_t offset = chunk.code.size() - loopStart + 2;
    if (offset > UINT16_MAX) {
        std::cerr << "Compiler error: loop offset " << offset
                  << " exceeds 16-bit limit. Loop body too large." << std::endl;
        std::exit(1);
    }

    emitByte(static_cast<uint8_t>(offset & 0xFF));
    emitByte(static_cast<uint8_t>((offset >> 8) & 0xFF));
}

uint8_t Compiler::makeConstant(Value value) {
    size_t index = chunk.addConstant(value);
    // Guard against constant pool overflow (> 256 entries).
    // A future OP_CONSTANT_LONG (16-bit index) would lift this limit.
    if (index > UINT8_MAX) {
        std::cerr << "Compiler error: constant pool overflow ("
                  << index << " entries, max 256)." << std::endl;
        std::exit(1);
    }
    return static_cast<uint8_t>(index);
}

uint8_t Compiler::identifierConstant(const std::string& name) {
    return makeConstant(name);
}

uint8_t Compiler::addFunctionPrototype(FunctionPrototype proto) {
    // Store as a shared_ptr in the constant pool
    auto ptr = std::make_shared<FunctionPrototype>(std::move(proto));
    return makeConstant(ptr);
}

void Compiler::compileExpr(const Expr& expr) {
    expr.accept(*this);
}

// =========================================================================
// Local variable management
// =========================================================================

void Compiler::beginScope() {
    scopeDepth++;
    scopeLocalCounts.push_back(0);
}

void Compiler::endScope() {
    int count = scopeLocalCounts.back();
    scopeLocalCounts.pop_back();

    if (count > 0) {
        // Emit OP_CLOSE_UPVALUE for each captured local that is going out of scope
        for (int i = 0; i < count; i++) {
            int localIdx = static_cast<int>(locals.size()) - count + i;
            if (localIdx >= 0 && locals[static_cast<size_t>(localIdx)].captured) {
                emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSE_UPVALUE),
                          static_cast<uint8_t>(localIdx));
            }
        }
        // Emit POPN to remove locals from stack
        emitBytes(static_cast<uint8_t>(OpCode::OP_POPN),
                  static_cast<uint8_t>(count));
        // Remove locals from tracking
        for (int i = 0; i < count; i++) {
            locals.pop_back();
        }
    }

    scopeDepth--;
}

void Compiler::addLocal(const std::string& name) {
    // Check for redefinition in current scope
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth == -1 || locals[i].depth < scopeDepth) break;
        if (locals[i].name == name) {
            // Error: redefinition in same scope (silently ignore for now)
            return;
        }
    }

    locals.push_back({name, scopeDepth, false});
    scopeLocalCounts.back()++;
}

int Compiler::resolveLocal(const std::string& name) const {
    // Search from innermost to outermost
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth != -1 && locals[i].name == name) {
            return i;
        }
    }
    return -1; // Not a local
}

int Compiler::currentLocalCount() const {
    return static_cast<int>(locals.size());
}

int Compiler::resolveGlobal(const std::string& name) {
    // Walk up the enclosing compiler chain to find the root compiler.
    // All global slot assignments MUST happen in the root compiler so
    // that nested function compilers share the same numbering.
    Compiler* root = this;
    while (root->enclosing) {
        root = root->enclosing;
    }
    // Linear scan — global count is small.
    for (size_t i = 0; i < root->globalNames.size(); i++) {
        if (root->globalNames[i] == name) return static_cast<int>(i);
    }
    int slot = static_cast<int>(root->globalNames.size());
    root->globalNames.push_back(name);
    return slot;
}

int Compiler::resolveUpvalue(Compiler* compiler, const std::string& name) {
    // Walk up the enclosing compiler chain to find the variable
    if (compiler->enclosing == nullptr) return -1;

    uint8_t resolvedIndex;
    bool resolvedIsLocal;

    // Try to resolve as a local in the immediate enclosing function
    int localSlot = compiler->enclosing->resolveLocal(name);
    if (localSlot >= 0) {
        // Mark the local as captured in the enclosing function
        compiler->enclosing->locals[static_cast<size_t>(localSlot)].captured = true;
        resolvedIndex = static_cast<uint8_t>(localSlot);
        resolvedIsLocal = true;
    } else {
        // Try to resolve as an upvalue in the immediate enclosing function
        int upvalueIdx = compiler->enclosing->resolveUpvalue(compiler->enclosing, name);
        if (upvalueIdx >= 0) {
            resolvedIndex = static_cast<uint8_t>(upvalueIdx);
            resolvedIsLocal = false;
        } else {
            return -1;
        }
    }

    // Check if this upvalue is already captured (deduplicate)
    for (int i = 0; i < static_cast<int>(upvalues.size()); i++) {
        if (upvalues[i].index == resolvedIndex &&
            upvalues[i].isLocal == resolvedIsLocal) {
            return i;  // already captured, reuse existing index
        }
    }

    // Add new upvalue descriptor
    int idx = static_cast<int>(upvalues.size());
    upvalues.push_back({resolvedIndex, resolvedIsLocal});
    return idx;
}

// =========================================================================
// Main entry: compile a Program into a Chunk
// =========================================================================

Chunk Compiler::compile(const Program* program) {
    program->accept(*this);

    // Implicit return null at end of program
    emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

    return std::move(chunk);
}

// =========================================================================
// ProgramVisitor
// =========================================================================

void Compiler::visitProgram(const Program& program) {
    for (const auto& stmt : program.statements) {
        stmt->accept(*this);
    }
}

// =========================================================================
// ExprVisitor — expression compilation
// =========================================================================

void Compiler::visitLiteralExpr(const LiteralExpr& expr) {
    const auto& v = expr.value;
    if (std::holds_alternative<std::nullptr_t>(v)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    } else if (std::holds_alternative<bool>(v)) {
        emitByte(std::get<bool>(v)
            ? static_cast<uint8_t>(OpCode::OP_TRUE)
            : static_cast<uint8_t>(OpCode::OP_FALSE));
    } else if (std::holds_alternative<double>(v) || std::holds_alternative<int64_t>(v)) {
        emitConstant(v);
    } else if (std::holds_alternative<std::string>(v)) {
        const auto& str = std::get<std::string>(v);
        // Check for ${...} interpolation patterns
        if (str.find("${") != std::string::npos) {
            compileInterpolatedString(str);
        } else {
            emitConstant(v);
        }
    }
}

void Compiler::compileInterpolatedString(const std::string& str) {
    // Parse ${...} patterns at compile time and emit bytecode for
    // string building via concatenation.
    //
    // For literal segments:     OP_CONSTANT
    // For variable segments:    OP_GET_LOCAL/OP_GET_GLOBAL + stringify via ("" + value)
    // For dotted segments:      OP_GET_LOCAL/OP_GET_GLOBAL + OP_GET_PROPERTY chain + stringify
    // All segments concatenated with OP_ADD.

    std::vector<std::string> parts;   // literal or variable name (with dots)
    std::vector<bool> isLiteral;      // true = literal string, false = variable ref

    size_t i = 0;
    while (i < str.length()) {
        if (i + 1 < str.length() && str[i] == '$' && str[i + 1] == '{') {
            i += 2;  // skip ${
            std::string varName;
            while (i < str.length() && str[i] != '}') {
                varName += str[i];
                i++;
            }
            if (i < str.length()) i++;  // skip }
            parts.push_back(varName);
            isLiteral.push_back(false);
        } else {
            std::string literal;
            while (i < str.length()) {
                if (i + 1 < str.length() && str[i] == '$' && str[i + 1] == '{') break;
                literal += str[i];
                i++;
            }
            parts.push_back(literal);
            isLiteral.push_back(true);
        }
    }

    if (parts.empty()) {
        emitConstant(std::string(""));
        return;
    }

    // Emit first part
    if (isLiteral[0]) {
        emitConstant(parts[0]);
    } else {
        compileVariableOrPropertyRef(parts[0]);
        // Stringify: push "", then OP_ADD
        emitConstant(std::string(""));
        emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
    }

    // Emit remaining parts and concatenate
    for (size_t j = 1; j < parts.size(); j++) {
        if (isLiteral[j]) {
            emitConstant(parts[j]);
        } else {
            compileVariableOrPropertyRef(parts[j]);
            // Stringify non-literal values
            emitConstant(std::string(""));
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
    }
}

void Compiler::compileVariableOrPropertyRef(const std::string& name) {
    // Handles "varName" or "obj.prop.subprop"
    size_t dot = name.find('.');
    std::string base = name.substr(0, dot);

    // Resolve base variable
    int localSlot = resolveLocal(base);
    if (localSlot >= 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                  static_cast<uint8_t>(localSlot));
    } else {
        // Use safe global lookup: if the global doesn't exist, push the
        // literal ${...} text as a fallback (matching interpreter behavior).
        int slot = resolveGlobal(base);
        std::string fallback = "${" + name + "}";
        uint8_t fallbackIndex = identifierConstant(fallback);
        emitByte(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL_SAFE));
        emitByte(static_cast<uint8_t>(slot));
        emitByte(fallbackIndex);
        return;  // property chains on safe globals not yet supported
    }

    // Walk property chain (only reached for locals)
    while (dot != std::string::npos) {
        size_t start = dot + 1;
        dot = name.find('.', start);
        std::string prop = name.substr(start, (dot == std::string::npos)
            ? std::string::npos : dot - start);
        uint8_t propIndex = identifierConstant(prop);
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), propIndex);
    }
}

void Compiler::visitBinaryExpr(const BinaryExpr& expr) {
    // Short-circuit logical operators
    if (expr.op.type == TokenType::AND) {
        expr.left->accept(*this);
        size_t jump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        expr.right->accept(*this);
        patchJump(jump);
        return;
    }

    if (expr.op.type == TokenType::OR) {
        expr.left->accept(*this);
        size_t elseJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        size_t endJump = emitJump(OpCode::OP_JUMP);
        patchJump(elseJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        expr.right->accept(*this);
        patchJump(endJump);
        return;
    }

    // Constant folding: if both operands are literal constants, evaluate at
    // compile time and emit a single constant instead of runtime bytecode.
    auto* leftLit = dynamic_cast<const LiteralExpr*>(expr.left.get());
    auto* rightLit = dynamic_cast<const LiteralExpr*>(expr.right.get());
    if (leftLit && rightLit) {
        const auto& lv = leftLit->value;
        const auto& rv = rightLit->value;
        if (isNumeric(lv) && isNumeric(rv)) {
            bool intOp = std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv);
            if (intOp) {
                int64_t a = std::get<int64_t>(lv);
                int64_t b = std::get<int64_t>(rv);
                switch (expr.op.type) {
                    case TokenType::PLUS:     emitConstant(a + b); return;
                    case TokenType::MINUS:    emitConstant(a - b); return;
                    case TokenType::MULTIPLY: emitConstant(a * b); return;
                    case TokenType::DIVIDE:
                        if (b == 0) break;
                        emitConstant(static_cast<double>(a) / static_cast<double>(b)); return;
                    case TokenType::MODULO:
                        if (b == 0) break;
                        emitConstant(static_cast<double>(std::fmod(static_cast<double>(a), static_cast<double>(b)))); return;
                    default: break;
                }
            } else {
                double a = toDouble(lv);
                double b = toDouble(rv);
                switch (expr.op.type) {
                    case TokenType::PLUS:     emitConstant(a + b); return;
                    case TokenType::MINUS:    emitConstant(a - b); return;
                    case TokenType::MULTIPLY: emitConstant(a * b); return;
                    case TokenType::DIVIDE:
                        if (b == 0.0) break;
                        emitConstant(a / b); return;
                    case TokenType::MODULO:
                        if (b == 0.0) break;
                        emitConstant(std::fmod(a, b)); return;
                    default: break;
                }
            }
        }
        if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv)) {
            // String concatenation at compile time
            if (expr.op.type == TokenType::PLUS) {
                emitConstant(std::get<std::string>(lv) + std::get<std::string>(rv));
                return;
            }
        }
    }

    // Regular binary operators
    expr.left->accept(*this);
    expr.right->accept(*this);

    switch (expr.op.type) {
        // OP_ADD is kept general (handles strings, arrays, numbers)
        case TokenType::PLUS:             emitByte(static_cast<uint8_t>(OpCode::OP_ADD)); break;
        // Fast numeric-only opcodes — skip type checks at runtime
        case TokenType::MINUS:            emitByte(static_cast<uint8_t>(OpCode::OP_SUB_NN)); break;
        case TokenType::MULTIPLY:         emitByte(static_cast<uint8_t>(OpCode::OP_MUL_NN)); break;
        case TokenType::DIVIDE:           emitByte(static_cast<uint8_t>(OpCode::OP_DIV_NN)); break;
        case TokenType::MODULO:           emitByte(static_cast<uint8_t>(OpCode::OP_MOD_NN)); break;
        case TokenType::POWER:            emitByte(static_cast<uint8_t>(OpCode::OP_POWER)); break;
        case TokenType::EQUAL_EQUAL:      emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL)); break;
        case TokenType::NOT_EQUAL:        emitByte(static_cast<uint8_t>(OpCode::OP_NOT_EQUAL)); break;
        case TokenType::LESS:             emitByte(static_cast<uint8_t>(OpCode::OP_LESS_NN)); break;
        case TokenType::LESS_EQUAL:       emitByte(static_cast<uint8_t>(OpCode::OP_LESS_EQ_NN)); break;
        case TokenType::GREATER:          emitByte(static_cast<uint8_t>(OpCode::OP_GREATER_NN)); break;
        case TokenType::GREATER_EQUAL:    emitByte(static_cast<uint8_t>(OpCode::OP_GREATER_EQ_NN)); break;
        default: break;
    }
}

void Compiler::visitGroupingExpr(const GroupingExpr& expr) {
    expr.expression->accept(*this);
}

void Compiler::visitUnaryExpr(const UnaryExpr& expr) {
    // Constant folding: -literal
    if (auto* lit = dynamic_cast<const LiteralExpr*>(expr.right.get())) {
        if (expr.op.type == TokenType::MINUS &&
            isNumeric(lit->value)) {
            if (std::holds_alternative<int64_t>(lit->value))
                emitConstant(-std::get<int64_t>(lit->value));
            else
                emitConstant(-std::get<double>(lit->value));
            return;
        }
        if (expr.op.type == TokenType::NOT &&
            std::holds_alternative<bool>(lit->value)) {
            emitByte(std::get<bool>(lit->value)
                ? static_cast<uint8_t>(OpCode::OP_FALSE)
                : static_cast<uint8_t>(OpCode::OP_TRUE));
            return;
        }
    }

    expr.right->accept(*this);

    switch (expr.op.type) {
        case TokenType::MINUS:
            emitByte(static_cast<uint8_t>(OpCode::OP_NEGATE));
            break;
        case TokenType::NOT:
            emitByte(static_cast<uint8_t>(OpCode::OP_NOT));
            break;
        default:
            break;
    }
}

void Compiler::visitVariableExpr(const VariableExpr& expr) {
    currentLine = expr.nameToken.line;

    // Check locals first
    int localSlot = resolveLocal(expr.name);
    if (localSlot >= 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                  static_cast<uint8_t>(localSlot));
    } else {
        // Check upvalues (captured from enclosing function)
        int upvalueIdx = resolveUpvalue(this, expr.name);
        if (upvalueIdx >= 0) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_UPVALUE),
                      static_cast<uint8_t>(upvalueIdx));
        } else {
            int slot = resolveGlobal(expr.name);
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(slot));
        }
    }
}

void Compiler::visitAssignmentExpr(const AssignmentExpr& expr) {
    currentLine = expr.nameToken.line;
    expr.value->accept(*this);

    // Check locals first
    int localSlot = resolveLocal(expr.name);
    if (localSlot >= 0) {
        // Value is on stack; copy and assign to local slot
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                  static_cast<uint8_t>(localSlot));
    } else {
        // Check upvalues
        int upvalueIdx = resolveUpvalue(this, expr.name);
        if (upvalueIdx >= 0) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE),
                      static_cast<uint8_t>(upvalueIdx));
        } else {
            int slot = resolveGlobal(expr.name);
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL),
                      static_cast<uint8_t>(slot));
        }
    }
}

void Compiler::visitCallExpr(const CallExpr& expr) {
    currentLine = expr.paren.line;
    // Compile callee first (goes below arguments on stack)
    expr.callee->accept(*this);
    // Compile arguments (pushed on top of callee)
    for (const auto& arg : expr.arguments) {
        arg->accept(*this);
    }
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL),
              static_cast<uint8_t>(expr.arguments.size()));
}

void Compiler::visitArrayExpr(const ArrayExpr& expr) {
    currentLine = expr.leftBracket.line;
    for (const auto& elem : expr.elements) {
        elem->accept(*this);
    }
    emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY),
              static_cast<uint8_t>(expr.elements.size()));
}

void Compiler::visitIndexExpr(const IndexExpr& expr) {
    currentLine = expr.bracket.line;
    expr.array->accept(*this);
    expr.index->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
}

void Compiler::visitPropertyExpr(const PropertyExpr& expr) {
    currentLine = expr.dot.line;
    expr.object->accept(*this);
    uint8_t nameIndex = identifierConstant(expr.property);
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), nameIndex);
}

void Compiler::visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) {
    currentLine = expr.dot.line;
    expr.object->accept(*this);
    expr.value->accept(*this);
    uint8_t nameIndex = identifierConstant(expr.property);
    emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY), nameIndex);
}

void Compiler::visitThisExpr(const ThisExpr& expr) {
    currentLine = expr.keyword.line;
    // 'this' is always a local variable at slot 0 in method scope
    int localSlot = resolveLocal("this");
    if (localSlot >= 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                  static_cast<uint8_t>(localSlot));
    } else {
        // Fall back to global (shouldn't happen in valid code)
        int slot = resolveGlobal("this");
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                  static_cast<uint8_t>(slot));
    }
}

void Compiler::visitIncDecExpr(const IncDecExpr& expr) {
    currentLine = expr.op.line;
    Value delta = (expr.op.type == TokenType::PLUS_PLUS)
        ? Value(static_cast<int64_t>(1))
        : Value(static_cast<int64_t>(-1));

    if (auto var = dynamic_cast<const VariableExpr*>(expr.target.get())) {
        // Get current value onto stack
        var->accept(*this);

        // Resolve the variable's location: local → upvalue → global
        int localSlot = resolveLocal(var->name);
        bool isLocal = (localSlot >= 0);
        int upvalueIdx = -1;
        int globalSlot = -1;
        if (!isLocal) {
            upvalueIdx = resolveUpvalue(this, var->name);
            if (upvalueIdx < 0) {
                globalSlot = resolveGlobal(var->name);
            }
        }

        if (expr.isPrefix) {
            // Prefix: ++x → increment on stack, store back, leave new value.
            emitConstant(delta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            if (isLocal) {
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(localSlot));
            } else if (upvalueIdx >= 0) {
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE),
                          static_cast<uint8_t>(upvalueIdx));
            } else {
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL),
                          static_cast<uint8_t>(globalSlot));
            }
        } else {
            // Postfix: x++ → save old value (DUP), increment, store, pop new,
            // leaving old value on stack as the expression result.
            emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
            emitConstant(delta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            if (isLocal) {
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(localSlot));
            } else if (upvalueIdx >= 0) {
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE),
                          static_cast<uint8_t>(upvalueIdx));
            } else {
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL),
                          static_cast<uint8_t>(globalSlot));
            }
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        return;
    }

    // Property target: ++obj.prop / obj.prop++
    if (auto prop = dynamic_cast<const PropertyExpr*>(expr.target.get())) {
        uint8_t propIndex = identifierConstant(prop->property);

        if (expr.isPrefix) {
            // Prefix: ++obj.prop → dup obj, get old, inc, set, leave new.
            prop->object->accept(*this);
            emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), propIndex);
            emitConstant(delta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY), propIndex);
        } else {
            // Postfix: obj.prop++ → return old value, store new value.
            // Evaluate obj once (DUP) to avoid double-executing side effects.
            // Stack sequence (delta = ±1):
            //   obj → DUP → [obj, obj]
            //   GET_PROPERTY → [obj, old]
            //   ADD delta → [obj, new]
            //   SET_PROPERTY → [new]          (stores obj.prop = new)
            //   ADD (-delta) → [old]           (result: new - delta = old)
            prop->object->accept(*this);
            emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), propIndex);
            emitConstant(delta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY), propIndex);
            Value negDelta = (expr.op.type == TokenType::PLUS_PLUS)
                ? Value(static_cast<int64_t>(-1))
                : Value(static_cast<int64_t>(1));
            emitConstant(negDelta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
        }
        return;
    }
}

void Compiler::visitTernaryExpr(const TernaryExpr& expr) {
    expr.condition->accept(*this);
    size_t thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Then branch
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    expr.thenBranch->accept(*this);
    size_t elseJump = emitJump(OpCode::OP_JUMP);

    // Else branch
    patchJump(thenJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    expr.elseBranch->accept(*this);

    // End
    patchJump(elseJump);
}

// =========================================================================
// StmtVisitor — statement compilation
// =========================================================================

void Compiler::visitExprStmt(const ExprStmt& stmt) {
    stmt.expression->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
}

void Compiler::visitLetStmt(const LetStmt& stmt) {
    // Compile initializer (or null if none)
    if (stmt.initializer) {
        stmt.initializer->accept(*this);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }

    if (scopeDepth == 0) {
        // Global variable — use interned slot ID
        int slot = resolveGlobal(stmt.name);
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                  static_cast<uint8_t>(slot));
    } else {
        // Local variable — the initializer value is already on the stack.
        // It stays there as the local's slot. Just record the slot.
        addLocal(stmt.name);
        // The value is already on the stack at the local slot position.
        // No OP_SET_LOCAL needed — the initializer result IS the local.
    }
}

void Compiler::visitBlockStmt(const BlockStmt& stmt) {
    // Enter new scope
    beginScope();

    for (const auto& s : stmt.statements) {
        s->accept(*this);
    }

    // Exit scope (emits OP_POPN to clean up locals)
    endScope();
}

void Compiler::visitReturnStmt(const ReturnStmt& stmt) {
    // Pop catch handlers for any enclosing try blocks before returning
    for (int t = 0; t < tryNesting; t++) {
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // Push return value (must be on stack before finally runs)
    if (stmt.value) {
        stmt.value->accept(*this);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }

    if (finallyNesting > 0) {
        // Instead of OP_RETURN, emit OP_JUMP that will be re-routed through
        // the finally block by visitTryStmt. The return value stays on stack.
        size_t jump = emitJump(OpCode::OP_JUMP);
        pendingReturnJumps.push_back(jump);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    }
}

void Compiler::visitIfStmt(const IfStmt& stmt) {
    // Compile condition
    stmt.condition->accept(*this);
    size_t thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Then branch
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    stmt.thenBranch->accept(*this);

    if (stmt.elseBranch) {
        size_t elseJump = emitJump(OpCode::OP_JUMP);
        patchJump(thenJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        stmt.elseBranch->accept(*this);
        patchJump(elseJump);
    } else {
        // Jump over the false-path OP_POP so the true path
        // doesn't fall through and pop a local variable value.
        size_t skipJump = emitJump(OpCode::OP_JUMP);
        patchJump(thenJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        patchJump(skipJump);
    }
}

void Compiler::visitWhileStmt(const WhileStmt& stmt) {
    size_t loopStart = chunk.code.size();

    // Push loop context for break/continue.
    // While loops: continueTarget == loopStart (backward jump to condition)
    loopStack.push_back({loopStart, loopStart, {}, {}, scopeDepth});

    // Condition
    stmt.condition->accept(*this);
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Body
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    stmt.body->accept(*this);

    // Loop back to condition
    emitLoop(loopStart);

    // Patch exit
    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Patch all break jumps
    for (size_t jumpOffset : loopStack.back().breakJumps) {
        patchJump(jumpOffset);
        // break already includes OP_POP in visitBreakStmt... actually no.
        // The break jumps to after the loop. By the time we get here,
        // the condition might or might not be on the stack.
        // We just jump. But we need to handle the stack:
        // If break is inside the body, the condition is NOT on stack
        // (body pops condition first).
        // Actually, the condition is popped in the body entry. And break
        // might leave some expression values. This is complex.
        // For Phase 2: break just jumps past the loop.
    }

    loopStack.pop_back();
}

void Compiler::visitForStmt(const ForStmt& stmt) {
    // Desugar for-in into a while loop using local variables:
    //
    //   for x in iterable { body }
    // becomes:
    //   {
    //     let _iter = iterable
    //     let _i = 0
    //     while (_i < _length(_iter)) {
    //       let x = _iter[_i]
    //       body
    //       _i += 1
    //     }
    //   }
    //
    // For strings, indexing returns a single-char string.
    // The _length is computed via a builtin or by checking array/string size.

    beginScope();

    // Compile iterable expression and store as local _iter
    stmt.iterable->accept(*this);
    addLocal("_iter");

    // Initialize index _i = 0
    emitConstant(0.0);
    addLocal("_i");

    // Call _vora_len to get the length of _iter
    int lenSlot = resolveGlobal("_vora_len");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
              static_cast<uint8_t>(lenSlot));

    int iterSlot = resolveLocal("_iter");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iterSlot));
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
    addLocal("_len");

    // Loop structure:
    //   loopStart:  condition (_i < _len)
    //   body (with beginScope/endScope)
    //   continueTarget: increment (_i += 1)
    //   loop back to loopStart
    //
    // continue requires increment before looping back. We store
    // continueJumps and patch them to the continueTarget.

    size_t loopStart = chunk.code.size();
    // continueTarget = SIZE_MAX signals "for-in loop: use forward jump,
    // target will be set later". extraLocalsToPop = 3 accounts for the
    // auto-generated _iter, _i, _len locals that must be cleaned up on break.
    loopStack.push_back({loopStart, SIZE_MAX, {}, {}, scopeDepth, 3});

    // --- Condition: _i < _len ---
    int iSlot = resolveLocal("_i");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iSlot));

    int lenLocalSlot = resolveLocal("_len");
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(lenLocalSlot));

    emitByte(static_cast<uint8_t>(OpCode::OP_LESS));
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // --- Body ---
    beginScope();

    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iterSlot));
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iSlot));
    emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
    addLocal(stmt.variable);

    stmt.body->accept(*this);

    endScope();

    // --- Increment _i (continue target) ---
    // Record this position for continue jumps
    loopStack.back().continueTarget = chunk.code.size();

    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iSlot));
    emitConstant(1.0);
    emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
    emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
              static_cast<uint8_t>(iSlot));
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // --- Loop back ---
    emitLoop(loopStart);

    // Patch exit (condition false → exit loop)
    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Patch continue jumps to the continueTarget (the increment section).
    // Must be done BEFORE endScope() because continue re-enters the loop
    // and needs _iter/_i/_len still on the stack.
    for (size_t jumpOffset : loopStack.back().continueJumps) {
        size_t target = loopStack.back().continueTarget;
        size_t jumpSize = target - jumpOffset - 2;
        if (jumpSize > UINT16_MAX) jumpSize = UINT16_MAX;
        chunk.writeAt(jumpOffset, static_cast<uint8_t>(jumpSize & 0xFF));
        chunk.writeAt(jumpOffset + 1, static_cast<uint8_t>((jumpSize >> 8) & 0xFF));
    }

    // Save break jumps before popping the loop context. They must be
    // patched AFTER endScope() because break already handled cleanup of
    // _iter/_i/_len via extraLocalsToPop — landing before endScope()
    // would double-pop those locals.
    std::vector<size_t> savedBreakJumps = std::move(loopStack.back().breakJumps);

    loopStack.pop_back();

    // End outer scope (pops _iter, _i, _len for normal exit path)
    endScope();

    // Patch break jumps after endScope() — break path skips the
    // endScope() cleanup since it already handled it.
    for (size_t jumpOffset : savedBreakJumps) {
        patchJump(jumpOffset);
    }
}

void Compiler::visitFuncStmt(const FuncStmt& stmt) {
    // Create a separate compiler for the function body
    Compiler fnCompiler;
    fnCompiler.enclosing = this;

    // Function body starts a new scope with parameters as locals
    fnCompiler.beginScope();

    // Add parameters as locals (they'll be set by OP_CALL binding)
    for (const auto& param : stmt.params) {
        fnCompiler.addLocal(param);
    }

    // Compile the function body
    for (const auto& s : stmt.body->statements) {
        s->accept(fnCompiler);
    }

    // Implicit return null at end of function
    fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

    // End parameter scope (will POPN params)
    fnCompiler.endScope();

    // Build function prototype (copy upvalues before moving)
    auto capturedUpvalues = fnCompiler.upvalues;  // copy before move
    FunctionPrototype proto;
    proto.name = stmt.name;
    proto.arity = static_cast<int>(stmt.params.size());
    proto.upvalues = std::move(fnCompiler.upvalues);
    proto.chunk = std::move(fnCompiler.chunk);

    // Store prototype in constant pool
    uint8_t protoIndex = addFunctionPrototype(std::move(proto));

    // Emit OP_CLOSURE to create the runtime VoraFunction
    // Format: OP_CLOSURE <protoIndex> <upvalueCount> {<isLocal> <index>}*
    emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE), protoIndex);
    emitByte(static_cast<uint8_t>(capturedUpvalues.size()));
    for (const auto& uv : capturedUpvalues) {
        emitByte(uv.isLocal ? 1 : 0);
        emitByte(uv.index);
    }

    // Bind to name
    if (scopeDepth == 0) {
        // Global function — use interned slot ID
        int slot = resolveGlobal(stmt.name);
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                  static_cast<uint8_t>(slot));
    } else {
        // Local function — the OP_CLOSURE result stays on stack as local slot
        addLocal(stmt.name);
    }
}

void Compiler::visitObjStmt(const ObjStmt& stmt) {
    // Phase 3: Object compilation.
    // Strategy: emit OP_CLASS with all methods pre-compiled, then call
    // the class constructor. The class object stores method prototypes
    // and parent info. At runtime, OP_CLASS + constructor call creates
    // an instance.

    // For now, we need to handle this properly. Let me implement it.

    // Build class prototype info
    // The class constant contains: name, parent name, params,
    // compiled methods map, and constructor body

    // First, compile each method
    std::vector<std::shared_ptr<FunctionPrototype>> methodProtos;
    for (const auto& methodStmt : stmt.methods) {
        if (auto funcStmt = dynamic_cast<const FuncStmt*>(methodStmt.get())) {
            Compiler methodCompiler;
            methodCompiler.enclosing = this;

            methodCompiler.beginScope();
            // Add 'this' as local at slot 0
            methodCompiler.addLocal("this");
            // Add parameters
            for (const auto& param : funcStmt->params) {
                methodCompiler.addLocal(param);
            }
            // Compile method body
            for (const auto& s : funcStmt->body->statements) {
                s->accept(methodCompiler);
            }

            methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
            methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

            methodCompiler.endScope();

            auto proto = std::make_shared<FunctionPrototype>();
            proto->name = funcStmt->name;
            proto->arity = static_cast<int>(funcStmt->params.size());
            proto->chunk = std::move(methodCompiler.chunk);
            methodProtos.push_back(proto);
        }
    }

    // Compile constructor body
    Compiler ctorCompiler;
    ctorCompiler.enclosing = this;
    ctorCompiler.beginScope();
    ctorCompiler.addLocal("this");  // slot 0
    for (const auto& param : stmt.params) {
        ctorCompiler.addLocal(param);
    }
    for (const auto& s : stmt.body->statements) {
        s->accept(ctorCompiler);
    }
    ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    ctorCompiler.endScope();

    auto ctorProto = std::make_shared<FunctionPrototype>();
    ctorProto->name = stmt.name;
    ctorProto->arity = static_cast<int>(stmt.params.size());
    ctorProto->chunk = std::move(ctorCompiler.chunk);

    // Store class data in constant pool
    auto classData = std::make_shared<ClassData>();
    classData->name = stmt.name;
    classData->parentName = stmt.parentName;
    classData->params = stmt.params;
    classData->ctor = ctorProto;
    classData->methods = methodProtos;

    uint8_t classIndex = makeConstant(classData);

    // Emit OP_CLASS to create the class constructor callable
    emitBytes(static_cast<uint8_t>(OpCode::OP_CLASS), classIndex);

    // Bind to name
    if (scopeDepth == 0) {
        int slot = resolveGlobal(stmt.name);
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                  static_cast<uint8_t>(slot));
    } else {
        addLocal(stmt.name);
    }
}

void Compiler::visitBreakStmt(const BreakStmt& /*stmt*/) {
    if (loopStack.empty()) {
        return;
    }

    // Pop catch handlers for any enclosing try blocks (they are skipped by
    // the break jump and would otherwise leak on the VM's catch handler stack).
    for (int t = 0; t < tryNesting; t++) {
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // Pop locals from current scope down to the loop's enclosing scope
    int localsToPop = 0;
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth > loopStack.back().enclosingScopeDepth) {
            localsToPop++;
        } else {
            break;
        }
    }
    // For-in loops generate _iter, _i, _len at enclosingScopeDepth which
    // must also be cleaned up on break (they persist through continue).
    localsToPop += loopStack.back().extraLocalsToPop;

    if (localsToPop > 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_POPN),
                  static_cast<uint8_t>(localsToPop));
    }

    // Emit jump to after the loop. Patch later.
    size_t jumpOffset = emitJump(OpCode::OP_JUMP);
    loopStack.back().breakJumps.push_back(jumpOffset);
}

void Compiler::visitContinueStmt(const ContinueStmt& /*stmt*/) {
    if (loopStack.empty()) {
        return;
    }

    // Pop catch handlers for any enclosing try blocks.
    for (int t = 0; t < tryNesting; t++) {
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // Pop locals from current scope down to the loop's enclosing scope
    int localsToPop = 0;
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth > loopStack.back().enclosingScopeDepth) {
            localsToPop++;
        } else {
            break;
        }
    }
    if (localsToPop > 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_POPN),
                  static_cast<uint8_t>(localsToPop));
    }

    // For while loops (continueTarget == loopStart): emit OP_LOOP backward.
    // For for-in loops (continueTarget == SIZE_MAX, not yet set): emit OP_JUMP
    // forward placeholder, patched later when continueTarget is known.
    if (loopStack.back().continueTarget == loopStack.back().loopStart) {
        emitLoop(loopStack.back().loopStart);
    } else {
        size_t jumpOffset = emitJump(OpCode::OP_JUMP);
        loopStack.back().continueJumps.push_back(jumpOffset);
    }
}

void Compiler::visitTryStmt(const TryStmt& stmt) {
    // Emit OP_PUSH_CATCH with placeholder (patched later to catch block)
    size_t pushCatchPlaceholder = emitJump(OpCode::OP_PUSH_CATCH);

    // Track try nesting so break/continue/return inside emit OP_POP_CATCH
    tryNesting++;

    // =========================================================================
    // Snapshot loop break/continue counts and return jump count BEFORE try body
    // =========================================================================
    std::vector<size_t> savedBreakCounts;
    std::vector<size_t> savedContinueCounts;
    for (const auto& lc : loopStack) {
        savedBreakCounts.push_back(lc.breakJumps.size());
        savedContinueCounts.push_back(lc.continueJumps.size());
    }
    size_t savedReturnCount = pendingReturnJumps.size();

    // Track finally nesting for return routing
    if (stmt.finallyBlock) finallyNesting++;

    // Compile try body (break/continue/return inside may emit new jumps)
    stmt.tryBlock->accept(*this);

    if (stmt.finallyBlock) finallyNesting--;
    tryNesting--;

    // =========================================================================
    // Capture new break/continue jumps emitted inside the try body.
    // They will be re-routed through the finally block (if present).
    // =========================================================================
    struct CapturedJump { size_t offset; int loopIdx; };
    std::vector<CapturedJump> capturedBreaks;
    std::vector<CapturedJump> capturedContinues;
    for (size_t li = 0; li < loopStack.size(); li++) {
        auto& lc = loopStack[li];
        while (lc.breakJumps.size() > savedBreakCounts[li]) {
            capturedBreaks.push_back({lc.breakJumps.back(), static_cast<int>(li)});
            lc.breakJumps.pop_back();
        }
        while (lc.continueJumps.size() > savedContinueCounts[li]) {
            capturedContinues.push_back({lc.continueJumps.back(), static_cast<int>(li)});
            lc.continueJumps.pop_back();
        }
    }
    // Capture return jumps emitted inside try body
    std::vector<size_t> capturedReturns;
    while (pendingReturnJumps.size() > savedReturnCount) {
        capturedReturns.push_back(pendingReturnJumps.back());
        pendingReturnJumps.pop_back();
    }

    // =========================================================================
    // Normal try exit: OP_POP_CATCH
    // =========================================================================
    emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));

    if (stmt.catchBlock) {
        // Skip catch block in normal flow
        size_t skipCatchJump = emitJump(OpCode::OP_JUMP);

        // Record catch block position and patch PUSH_CATCH to jump here
        size_t catchTarget = chunk.code.size();
        size_t catchJumpSize = catchTarget - pushCatchPlaceholder - 2;
        if (catchJumpSize > UINT16_MAX) catchJumpSize = UINT16_MAX;
        chunk.writeAt(pushCatchPlaceholder,
                      static_cast<uint8_t>(catchJumpSize & 0xFF));
        chunk.writeAt(pushCatchPlaceholder + 1,
                      static_cast<uint8_t>((catchJumpSize >> 8) & 0xFF));

        // Clear exceptionInFlight — the catch block handles the exception,
        // so any following OP_FINALLY_END should NOT re-throw.
        emitByte(static_cast<uint8_t>(OpCode::OP_CLEAR_EXCEPTION));
        // Pop catch handler (entered via exception path)
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));

        // Enter catch scope and bind exception variable
        beginScope();
        addLocal(stmt.catchVar);
        stmt.catchBlock->accept(*this);
        endScope();

        // Patch the skip-catch jump from normal flow
        patchJump(skipCatchJump);
    } else {
        // No catch — patch PUSH_CATCH to skip to after try
        patchJump(pushCatchPlaceholder);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // =========================================================================
    // Finally block — compiled once for the normal flow, then bytecode is
    // captured and replayed at each captured break/continue/return site.
    // =========================================================================
    if (stmt.finallyBlock) {
        // Record the finally bytecode (before OP_FINALLY_END)
        size_t finallyStart = chunk.code.size();
        stmt.finallyBlock->accept(*this);
        size_t finallyEnd = chunk.code.size();
        std::vector<uint8_t> finallyBytes(
            chunk.code.begin() + finallyStart,
            chunk.code.begin() + finallyEnd);
        emitByte(static_cast<uint8_t>(OpCode::OP_FINALLY_END));

        // Push finally bytecode onto stack (for nested finally blocks, the
        // outermost finally bytecode is replayed first at capture sites).
        finallyBytecodeStack.push_back(std::move(finallyBytes));

        // --- Route captured break jumps through finally ---
        for (const auto& cj : capturedBreaks) {
            // Patch the break's OP_JUMP placeholder to point to current position
            patchJump(cj.offset);
            // Replay finally bytecode
            for (uint8_t b : finallyBytecodeStack.back()) emitByte(b);
            emitByte(static_cast<uint8_t>(OpCode::OP_FINALLY_END));
            // New jump — re-added to the loop's breakJumps for normal patching
            size_t newJump = emitJump(OpCode::OP_JUMP);
            loopStack[static_cast<size_t>(cj.loopIdx)].breakJumps.push_back(newJump);
        }

        // --- Route captured continue jumps through finally ---
        for (const auto& cj : capturedContinues) {
            patchJump(cj.offset);
            for (uint8_t b : finallyBytecodeStack.back()) emitByte(b);
            emitByte(static_cast<uint8_t>(OpCode::OP_FINALLY_END));
            size_t newJump = emitJump(OpCode::OP_JUMP);
            loopStack[static_cast<size_t>(cj.loopIdx)].continueJumps.push_back(newJump);
        }

        // --- Route captured return jumps through finally ---
        for (size_t jumpOffset : capturedReturns) {
            // The return value is already on the stack (pushed by visitReturnStmt
            // before the OP_JUMP). After finally runs, it'll still be at peek(0).
            patchJump(jumpOffset);
            for (uint8_t b : finallyBytecodeStack.back()) emitByte(b);
            emitByte(static_cast<uint8_t>(OpCode::OP_FINALLY_END));
            emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
        }

        // Pop the finally bytecode stack entry
        finallyBytecodeStack.pop_back();
    }
}

void Compiler::visitThrowStmt(const ThrowStmt& stmt) {
    // Compile the value to throw
    stmt.value->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_THROW));
}

} // namespace vora
