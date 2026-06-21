#include "compiler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <variant>

#include "../gc/gc_heap.h"
#include "../runtime/vora_function.h"

namespace vora {
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
    } else if (std::holds_alternative<GcPtr<GcString>>(v)) {
        const auto& str = std::get<GcPtr<GcString>>(v)->value;
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
        emitConstant(GcHeap::instance().alloc<GcString>(""));
        return;
    }

    // Emit first part
    if (isLiteral[0]) {
        emitConstant(GcHeap::instance().alloc<GcString>(parts[0]));
    } else {
        compileVariableOrPropertyRef(parts[0]);
        // Stringify: push "", then OP_ADD
        emitConstant(GcHeap::instance().alloc<GcString>(""));
        emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
    }

    // Emit remaining parts and concatenate
    for (size_t j = 1; j < parts.size(); j++) {
        if (isLiteral[j]) {
            emitConstant(GcHeap::instance().alloc<GcString>(parts[j]));
        } else {
            compileVariableOrPropertyRef(parts[j]);
            // Stringify non-literal values
            emitConstant(GcHeap::instance().alloc<GcString>(""));
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
    }
}

void Compiler::compileVariableOrPropertyRef(const std::string& name) {
    // Handles "varName" or "obj.prop.subprop"
    size_t dot = name.find('.');
    std::string base = name.substr(0, dot);
    bool hasPropertyChain = (dot != std::string::npos);

    // Resolve base variable
    int localSlot = resolveLocal(base);
    if (localSlot >= 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                  static_cast<uint8_t>(localSlot));
    } else {
        if (hasPropertyChain) {
            // When there's a property chain on a global (e.g. ${obj.prop}),
            // use a direct global lookup. If the global doesn't exist it's
            // a runtime error — better than silently falling back to the
            // literal ${...} text.
            int slot = resolveGlobal(base);
            emitByte(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL));
            emitByte(static_cast<uint8_t>(slot));
        } else {
            // Simple global: use safe lookup with fallback to literal text.
            int slot = resolveGlobal(base);
            std::string fallback = "${" + name + "}";
            uint8_t fallbackIndex = identifierConstant(fallback);
            emitByte(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL_SAFE));
            emitByte(static_cast<uint8_t>(slot));
            emitByte(fallbackIndex);
            return;  // no property chain to walk
        }
    }

    // Walk property chain
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
        if (std::holds_alternative<GcPtr<GcString>>(lv) && std::holds_alternative<GcPtr<GcString>>(rv)) {
            // String concatenation at compile time
            if (expr.op.type == TokenType::PLUS) {
                emitConstant(GcHeap::instance().alloc<GcString>(std::get<GcPtr<GcString>>(lv)->value + std::get<GcPtr<GcString>>(rv)->value));
                return;
            }
        }
    }

    // Regular binary operators
    expr.left->accept(*this);
    expr.right->accept(*this);

    currentLine = expr.op.line;
    currentColumn = expr.op.column;

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
    currentColumn = expr.nameToken.column;

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
    currentColumn = expr.nameToken.column;

    // Check const before compiling the value (avoid wasted work)
    int localSlot = resolveLocal(expr.name);
    if (localSlot >= 0 && isLocalConst(expr.name)) {
        error("Cannot assign to const variable '" + expr.name + "'");
        return;
    }

    expr.value->accept(*this);

    if (localSlot >= 0) {
        // Value is on stack; copy and assign to local slot
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                  static_cast<uint8_t>(localSlot));
    } else {
        // Check upvalues
        int upvalueIdx = resolveUpvalue(this, expr.name);
        if (upvalueIdx >= 0) {
            if (isUpvalueConst(upvalueIdx)) {
                error("Cannot assign to const variable '" + expr.name + "'");
                return;
            }
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE),
                      static_cast<uint8_t>(upvalueIdx));
        } else {
            int slot = resolveGlobal(expr.name);
            // Check global const (walk to root compiler)
            Compiler* root = this;
            while (root->enclosing) root = root->enclosing;
            if (slot >= 0 && static_cast<size_t>(slot) < root->globalIsConst.size() &&
                root->globalIsConst[static_cast<size_t>(slot)]) {
                error("Cannot assign to const variable '" + expr.name + "'");
                return;
            }
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL),
                      static_cast<uint8_t>(slot));
        }
    }
}

void Compiler::visitCompoundAssignmentExpr(const CompoundAssignmentExpr& expr) {
    currentLine = expr.op.line;
    currentColumn = expr.op.column;

    // Map compound op to base binary opcode
    uint8_t binOp;
    switch (expr.op.type) {
        case TokenType::PLUS_EQUAL:     binOp = static_cast<uint8_t>(OpCode::OP_ADD);     break;
        case TokenType::MINUS_EQUAL:    binOp = static_cast<uint8_t>(OpCode::OP_SUB_NN);  break;
        case TokenType::MULTIPLY_EQUAL: binOp = static_cast<uint8_t>(OpCode::OP_MUL_NN);  break;
        case TokenType::DIVIDE_EQUAL:   binOp = static_cast<uint8_t>(OpCode::OP_DIV_NN);  break;
        case TokenType::MODULO_EQUAL:   binOp = static_cast<uint8_t>(OpCode::OP_MOD_NN);  break;
        default: return;
    }

    // --- VariableExpr target: x += y ---
    if (auto* var = dynamic_cast<VariableExpr*>(expr.target.get())) {
        // Check const — local, upvalue, and global
        if (isLocalConst(var->name)) {
            error("Cannot assign to const variable '" + var->name + "'");
            return;
        }
        // Check upvalue const
        int uvIdx = resolveUpvalue(this, var->name);
        if (uvIdx >= 0 && isUpvalueConst(uvIdx)) {
            error("Cannot assign to const variable '" + var->name + "'");
            return;
        }
        // Also check global const
        {
            Compiler* root = this;
            while (root->enclosing) root = root->enclosing;
            int gSlot = resolveLocal(var->name);
            if (gSlot < 0 && uvIdx < 0) {  // not a local or upvalue, check global
                auto it = std::find(root->globalNames.begin(), root->globalNames.end(), var->name);
                if (it != root->globalNames.end()) {
                    size_t idx = static_cast<size_t>(it - root->globalNames.begin());
                    if (idx < root->globalIsConst.size() && root->globalIsConst[idx]) {
                        error("Cannot assign to const variable '" + var->name + "'");
                        return;
                    }
                }
            }
        }
        // Read current value
        var->accept(*this);
        // Compile RHS
        expr.value->accept(*this);
        // Binary op
        emitByte(binOp);
        // Store back — mirror visitAssignmentExpr resolution
        int localSlot = resolveLocal(var->name);
        if (localSlot >= 0) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                      static_cast<uint8_t>(localSlot));
        } else {
            int upvalueIdx = resolveUpvalue(this, var->name);
            if (upvalueIdx >= 0) {
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE),
                          static_cast<uint8_t>(upvalueIdx));
            } else {
                int slot = resolveGlobal(var->name);
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL),
                          static_cast<uint8_t>(slot));
            }
        }
        return;
    }

    // --- PropertyExpr target: obj.prop += y ---
    if (auto* prop = dynamic_cast<PropertyExpr*>(expr.target.get())) {
        // Compile object reference (used by SET_PROPERTY which pops value then obj)
        prop->object->accept(*this);
        // Compile property read: clone object + GET_PROPERTY
        auto objClone = prop->object->clone();
        objClone->accept(*this);
        uint8_t nameIndex = identifierConstant(prop->property);
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), nameIndex);
        // Compile RHS
        expr.value->accept(*this);
        // Binary op
        emitByte(binOp);
        // Store back
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY), nameIndex);
        return;
    }

    // --- IndexExpr target: arr[i] += y ---
    if (auto* idx = dynamic_cast<IndexExpr*>(expr.target.get())) {
        // Compile array + index (used by OP_SET_INDEX which pops value, index, target)
        idx->array->accept(*this);
        idx->index->accept(*this);
        // Compile index read: clone array + clone index + OP_INDEX
        auto arrClone = idx->array->clone();
        auto indexClone = idx->index->clone();
        arrClone->accept(*this);
        indexClone->accept(*this);
        emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
        // Compile RHS
        expr.value->accept(*this);
        // Binary op
        emitByte(binOp);
        // Store back
        emitByte(static_cast<uint8_t>(OpCode::OP_SET_INDEX));
        return;
    }
}

void Compiler::visitCallExpr(const CallExpr& expr) {
    currentLine = expr.paren.line;
    currentColumn = expr.paren.column;
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
    currentColumn = expr.leftBracket.column;
    for (const auto& elem : expr.elements) {
        elem->accept(*this);
    }
    emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY),
              static_cast<uint8_t>(expr.elements.size()));
}

void Compiler::visitDictExpr(const DictExpr& expr) {
    currentLine = expr.leftBrace.line;
    currentColumn = expr.leftBrace.column;
    for (const auto& [key, value] : expr.pairs) {
        key->accept(*this);
        value->accept(*this);
    }
    emitBytes(static_cast<uint8_t>(OpCode::OP_DICT),
              static_cast<uint8_t>(expr.pairs.size()));
}

void Compiler::visitIndexExpr(const IndexExpr& expr) {
    currentLine = expr.bracket.line;
    currentColumn = expr.bracket.column;
    expr.array->accept(*this);
    expr.index->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
}

void Compiler::visitPropertyExpr(const PropertyExpr& expr) {
    currentLine = expr.dot.line;
    currentColumn = expr.dot.column;

    // super.method → emit OP_GET_SUPER instead of OP_GET_PROPERTY
    if (dynamic_cast<const SuperExpr*>(expr.object.get())) {
        uint8_t nameIndex = identifierConstant(expr.property);
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_SUPER), nameIndex);
        return;
    }

    expr.object->accept(*this);
    uint8_t nameIndex = identifierConstant(expr.property);
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), nameIndex);
}

void Compiler::visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) {
    currentLine = expr.dot.line;
    currentColumn = expr.dot.column;
    expr.object->accept(*this);
    expr.value->accept(*this);
    uint8_t nameIndex = identifierConstant(expr.property);
    emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY), nameIndex);
}

void Compiler::visitIndexAssignmentExpr(const IndexAssignmentExpr& expr) {
    currentLine = expr.bracket.line;
    currentColumn = expr.bracket.column;
    expr.object->accept(*this);
    expr.index->accept(*this);
    expr.value->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_SET_INDEX));
}

void Compiler::visitThisExpr(const ThisExpr& expr) {
    currentLine = expr.keyword.line;
    currentColumn = expr.keyword.column;
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

void Compiler::visitSuperExpr(const SuperExpr& expr) {
    // 'super' alone is an error — only 'super.method()' is valid.
    // The parser allows `super` as a primary expression, but the compiler
    // rejects it because OP_GET_SUPER needs a property name.
    currentLine = expr.keyword.line;
    currentColumn = expr.keyword.column;
    errorAt(currentLine, currentColumn, 5, "'super' must be followed by '.method'");
}

void Compiler::visitIncDecExpr(const IncDecExpr& expr) {
    currentLine = expr.op.line;
    currentColumn = expr.op.column;
    Value delta = (expr.op.type == TokenType::PLUS_PLUS)
        ? Value(static_cast<int64_t>(1))
        : Value(static_cast<int64_t>(-1));

    if (auto var = dynamic_cast<const VariableExpr*>(expr.target.get())) {
        // Check const — local, upvalue, and global
        if (isLocalConst(var->name)) {
            error("Cannot modify const variable '" + var->name + "'");
            return;
        }
        // Check upvalue const
        int uvIdx = resolveUpvalue(this, var->name);
        if (uvIdx >= 0 && isUpvalueConst(uvIdx)) {
            error("Cannot modify const variable '" + var->name + "'");
            return;
        }
        // Also check global const
        {
            Compiler* root = this;
            while (root->enclosing) root = root->enclosing;
            int lSlot = resolveLocal(var->name);
            if (lSlot < 0 && uvIdx < 0) {
                auto it = std::find(root->globalNames.begin(), root->globalNames.end(), var->name);
                if (it != root->globalNames.end()) {
                    size_t idx = static_cast<size_t>(it - root->globalNames.begin());
                    if (idx < root->globalIsConst.size() && root->globalIsConst[idx]) {
                        error("Cannot modify const variable '" + var->name + "'");
                        return;
                    }
                }
            }
        }
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
// visitMatchExpr — desugar match to if-else chain
// =========================================================================
// Strategy:
//   1. Compile scrutinee → store in temp local _mN
//   2. For each non-wildcard case: compile condition, JUMP_IF_FALSE to next,
//      compile body, JUMP to end
//   3. Last case (or wildcard): compile body directly
//   4. End: clean up temp local
// =========================================================================

void Compiler::visitMatchExpr(const MatchExpr& expr) {
    // 1. Compile scrutinee → value on TOS
    expr.scrutinee->accept(*this);

    // Use unique global temp for scrutinee at any scope level.
    // Static counter ensures uniqueness across all nested compilers.
    static int globalMatchCounter = 0;
    std::string tempName = "__m" + std::to_string(globalMatchCounter++);
    int tempSlot = defineGlobal(tempName);
    emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), static_cast<uint8_t>(tempSlot));

    std::vector<size_t> endJumps;

    for (size_t ci = 0; ci < expr.cases.size(); ci++) {
        const auto& case_ = expr.cases[ci];
        bool isLast = (ci == expr.cases.size() - 1);
        bool hasWildcard = false;
        for (const auto& p : case_.patterns) {
            if (p.kind == PatternKind::Wildcard) { hasWildcard = true; break; }
        }

        if (hasWildcard) {
            // Wildcard: always matches.
            compileMatchCaseBody(case_);
            if (!isLast) {
                endJumps.push_back(emitJump(OpCode::OP_JUMP));
            }
            break;
        }

        // Push scrutinee for comparison (stored in global temp)
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(tempSlot));

        if (case_.patterns[0].kind == PatternKind::Literal) {
            emitConstant(case_.patterns[0].literal);
            emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));
        } else if (case_.patterns[0].kind == PatternKind::Range) {
            const auto& pat = case_.patterns[0];
            emitConstant(pat.rangeLow);
            emitByte(static_cast<uint8_t>(OpCode::OP_GREATER_EQ_NN));
            size_t rangeLowFail = emitJump(OpCode::OP_JUMP_IF_FALSE);
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(tempSlot));
            emitConstant(pat.rangeHigh);
            if (pat.rangeInclusive) {
                emitByte(static_cast<uint8_t>(OpCode::OP_LESS_EQ_NN));
            } else {
                emitByte(static_cast<uint8_t>(OpCode::OP_LESS_NN));
            }
            size_t rangeDone = emitJump(OpCode::OP_JUMP);
            patchJump(rangeLowFail);
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            emitByte(static_cast<uint8_t>(OpCode::OP_FALSE));
            patchJump(rangeDone);
        } else {
            emitByte(static_cast<uint8_t>(OpCode::OP_TRUE));
        }

        // TOS = bool condition
        size_t caseFailJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        compileMatchCaseBody(case_);
        endJumps.push_back(emitJump(OpCode::OP_JUMP));

        // Not matched
        patchJump(caseFailJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        if (isLast && !hasWildcard) {
            emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
        }
    }

    for (auto j : endJumps) {
        patchJump(j);
    }
}

void Compiler::compileMatchCaseBody(const MatchCase& case_) {
    if (case_.blockBody) {
        // Block body: { stmts; lastExpr }
        // Compile all statements except the last as normal.
        // The last statement (if ExprStmt) leaves its value on the stack.
        // NOTE: Do NOT use beginScope/endScope here — the statements inside
        // the block manage their own scopes (e.g., let creates its own scope).
        const auto& stmts = case_.blockBody->statements;
        if (stmts.empty()) {
            emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
            return;
        }
        for (size_t i = 0; i < stmts.size(); i++) {
            if (i == stmts.size() - 1) {
                // Last statement: if it's an ExprStmt, compile only the expression
                if (auto* es = dynamic_cast<ExprStmt*>(stmts[i].get())) {
                    es->expression->accept(*this);
                } else {
                    // Non-expression last statement: compile as normal, push null
                    stmts[i]->accept(*this);
                    emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
                }
            } else {
                stmts[i]->accept(*this);
            }
        }
    } else if (case_.body) {
        // Expression body: just compile it
        case_.body->accept(*this);
    } else {
        // No body at all — error case, push null
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }
}

void Compiler::visitYieldExpr(const YieldExpr& expr) {
    currentLine = expr.keyword.line;
    currentColumn = expr.keyword.column;

    // Phase 1 limitation: yield inside try blocks is not supported
    if (tryNesting > 0) {
        errorAt(expr.keyword.line, expr.keyword.column, 5,
                "'yield' cannot be used inside a try block");
        return;
    }

    // Mark this function as a generator
    isGenerator = true;

    // Compile the value to yield (or null if no value)
    if (expr.value) {
        expr.value->accept(*this);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }

    emitByte(static_cast<uint8_t>(OpCode::OP_YIELD));
}

void Compiler::visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) {
    // Bare destructuring assignment: [a, b] = arr  or  {x, y} = obj
    // TODO: Implement full destructured assignment compilation.
    // For now, compile value and leave on stack.
    if (expr.value) {
        expr.value->accept(*this);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }
}

void Compiler::visitFuncExpr(const FuncExpr& expr) {
    // Compile an anonymous function expression: func(x) { body }
    // Creates a VoraFunction closure and pushes it on the stack.

    Compiler fnCompiler(errorReporter_);
    fnCompiler.enclosing = this;
    fnCompiler.chunk.source = chunk.source;

    fnCompiler.beginScope();

    // Detect rest parameter (always last if present)
    bool hasRest = !expr.params.empty() && expr.params.back().isRest;

    // Count required params (excluding rest)
    int requiredArity = 0;
    for (const auto& param : expr.params) {
        if (param.isRest) break;
        if (!param.defaultValue) requiredArity++;
        else break;
    }
    int totalArity = hasRest
        ? static_cast<int>(expr.params.size()) - 1
        : static_cast<int>(expr.params.size());

    // Add fixed parameters as locals
    for (const auto& param : expr.params) {
        if (param.isRest) break;
        fnCompiler.addLocal(param.name);
    }
    // Add rest parameter as a local
    if (hasRest) {
        fnCompiler.addLocal(expr.params.back().name);
    }

    // Emit default-parameter preamble for fixed params with defaults
    for (int i = requiredArity; i < totalArity; i++) {
        const auto& param = expr.params[static_cast<size_t>(i)];

        fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_DEFAULT_PARAM));
        fnCompiler.emitByte(static_cast<uint8_t>(i));
        size_t skipOffsetPos = fnCompiler.chunk.code.size();
        fnCompiler.emitByte(0xFF);
        fnCompiler.emitByte(0xFF);

        param.defaultValue->accept(fnCompiler);

        fnCompiler.emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                             static_cast<uint8_t>(i));
        fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        if (!fnCompiler.hadError) {
            size_t jumpEnd = fnCompiler.chunk.code.size();
            size_t offset = static_cast<size_t>(jumpEnd - skipOffsetPos - 2);
            if (offset > 0xFFFF) {
                fnCompiler.hadError = true;
            } else {
                fnCompiler.chunk.code[skipOffsetPos] =
                    static_cast<uint8_t>(offset & 0xFF);
                fnCompiler.chunk.code[skipOffsetPos + 1] =
                    static_cast<uint8_t>((offset >> 8) & 0xFF);
            }
        }
    }

    // Compile body statements
    for (const auto& s : expr.body->statements) {
        s->accept(fnCompiler);
    }

    // Implicit return null
    fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

    fnCompiler.endScope();

    // Build function prototype
    auto capturedUpvalues = fnCompiler.upvalues;
    FunctionPrototype proto;
    proto.name = "<lambda>";
    proto.arity = totalArity;
    proto.requiredArity = requiredArity;
    proto.hasRest = hasRest;
    proto.upvalues = std::move(fnCompiler.upvalues);
    proto.chunk = std::move(fnCompiler.chunk);
    proto.isGenerator = fnCompiler.isGenerator;

    uint8_t protoIndex = addFunctionPrototype(std::move(proto));

    // Emit OP_CLOSURE to create the VoraFunction at runtime
    emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE), protoIndex);
    emitByte(static_cast<uint8_t>(capturedUpvalues.size()));
    for (const auto& uv : capturedUpvalues) {
        emitByte(uv.isLocal ? 1 : 0);
        emitByte(uv.index);
    }
    // The closure is now on the stack — caller can assign, call, etc.
}

} // namespace vora
