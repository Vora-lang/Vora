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
            // use OP_GET_GLOBAL (not _SAFE). The _SAFE variant would fall back
            // to the literal "${obj.prop}" string, which would then fail with a
            // confusing "property not found on String" error. A direct "undefined
            // global" error is clearer.
            int slot = resolveGlobal(base);
            emitGetGlobal(slot);
        } else {
            // Simple global: use safe lookup with fallback to literal text.
            int slot = resolveGlobal(base);
            std::string fallback = "${" + name + "}";
            size_t fallbackIndex = identifierConstant(fallback);
            emitGetGlobalSafe(slot, fallbackIndex);
            return;  // no property chain to walk
        }
    }

    // Walk property chain
    while (dot != std::string::npos) {
        size_t start = dot + 1;
        dot = name.find('.', start);
        std::string prop = name.substr(start, (dot == std::string::npos)
            ? std::string::npos : dot - start);
        size_t propIndex = identifierConstant(prop);
        emitGetProperty(propIndex);
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

    // Null coalescing operator: a ?? b
    // If a is not null, short-circuit and return a; otherwise evaluate b.
    // Pattern:
    //   <left>
    //   DUP                  -- save copy for null check
    //   OP_JUMP_IF_NULL -> else  -- pop dup; if null, jump. Stack: [left]
    //   OP_JUMP -> end        -- left is non-null, skip the b branch. Stack: [left]
    // else:
    //   OP_POP                -- pop original null left. Stack: []
    //   <right>               -- Stack: [right]
    // end:
    if (expr.op.type == TokenType::QUESTION_QUESTION) {
        expr.left->accept(*this);
        emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
        size_t elseJump = emitJump(OpCode::OP_JUMP_IF_NULL);
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
                    case TokenType::PLUS:
                        // Check for signed overflow before folding
                        if ((b > 0 && a > INT64_MAX - b) ||
                            (b < 0 && a < INT64_MIN - b)) break;
                        emitConstant(a + b); return;
                    case TokenType::MINUS:
                        if ((b < 0 && a > INT64_MAX + b) ||
                            (b > 0 && a < INT64_MIN + b)) break;
                        emitConstant(a - b); return;
                    case TokenType::MULTIPLY:
                        if (a != 0) {
                            if (a > 0) {
                                if (b > 0 && a > INT64_MAX / b) break;
                                if (b < 0 && b < INT64_MIN / a) break;
                            } else {
                                if (b > 0 && a < INT64_MIN / b) break;
                                if (b < 0 && a < INT64_MAX / b) break;
                            }
                        }
                        emitConstant(a * b); return;
                    case TokenType::DIVIDE:
                        if (b == 0) break;
                        // INT64_MIN / -1 overflows
                        if (a == INT64_MIN && b == -1) break;
                        emitConstant(static_cast<double>(a) / static_cast<double>(b)); return;
                    case TokenType::MODULO:
                        if (b == 0) break;
                        // INT64_MIN % -1 overflows
                        if (a == INT64_MIN && b == -1) break;
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
            emitGetGlobal(slot);
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
            emitSetGlobal(slot);
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
            emitSetGlobal(slot);
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
        size_t nameIndex = identifierConstant(prop->property);
        emitGetProperty(nameIndex);
        // Compile RHS
        expr.value->accept(*this);
        // Binary op
        emitByte(binOp);
        // Store back
        emitSetProperty(nameIndex);
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

    // Detect if any argument uses call-site spread.
    bool hasSpread = false;
    uint8_t fixedCount = 0;
    for (const auto& arg : expr.arguments) {
        if (dynamic_cast<SpreadExpr*>(arg.get())) {
            hasSpread = true;
        } else {
            fixedCount++;
        }
    }

    // Detect if any argument is named (keyword).
    bool hasNamed = false;
    for (const auto& name : expr.argumentNames) {
        if (!name.empty()) { hasNamed = true; break; }
    }

    if (!hasSpread && !hasNamed) {
        // Fast path: no spread, no named args — existing behaviour unchanged.
        expr.callee->accept(*this);
        for (const auto& arg : expr.arguments) {
            arg->accept(*this);
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_CALL),
                  static_cast<uint8_t>(expr.arguments.size()));
        return;
    }

    if (hasSpread && !hasNamed) {
        // Spread path: push a counter entry, compile callee + args, emit OP_CALL_N.
        emitByte(static_cast<uint8_t>(OpCode::OP_PUSH_SPREAD));
        expr.callee->accept(*this);
        for (const auto& arg : expr.arguments) {
            arg->accept(*this);
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_CALL_N), fixedCount);
        return;
    }

    if (hasSpread && hasNamed) {
        error("Cannot mix named arguments with spread (...expr)");
        // Fall through: still compile as best-effort named call
    }

    // Named args path: compile callee, then positional args, then named args.
    // Stack layout: [callee] [posArg0..] [kwVal0..]
    expr.callee->accept(*this);

    // Count positional vs named args
    uint8_t posCount = 0;
    uint8_t kwCount = 0;
    std::vector<size_t> kwNameIndices;

    for (size_t i = 0; i < expr.arguments.size(); i++) {
        if (!expr.argumentNames[i].empty()) {
            kwCount++;
            kwNameIndices.push_back(identifierConstant(expr.argumentNames[i]));
        } else {
            posCount++;
        }
    }

    if (posCount > 255 || kwCount > 255) {
        error("Too many arguments");
        return;
    }

    // Compile positional args first, then named args (interleaved on stack)
    for (size_t i = 0; i < expr.arguments.size(); i++) {
        if (expr.argumentNames[i].empty()) {
            expr.arguments[i]->accept(*this);
        }
    }
    for (size_t i = 0; i < expr.arguments.size(); i++) {
        if (!expr.argumentNames[i].empty()) {
            expr.arguments[i]->accept(*this);
        }
    }

    // Emit OP_CALL_KW with metadata
    emitCallKw(posCount, kwCount, kwNameIndices);
}

void Compiler::visitSpreadExpr(const SpreadExpr& expr) {
    // Compile the inner expression (must evaluate to an Array), then
    // OP_SPREAD will expand it into individual stack values at runtime.
    expr.expr->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_SPREAD));
}

void Compiler::visitListCompExpr(const ListCompExpr& expr) {
    currentLine = expr.leftBracket.line;
    currentColumn = expr.leftBracket.column;
    // Desugar list comprehension using the iter()/next() iterator protocol:
    //
    //   [resultExpr for var in iterable if condition]
    // becomes:
    //   {
    //     let _lcN = []                    // result array
    //     {
    //       let _iter = iter(iterable)
    //       while (true) {
    //         try {
    //           let var = next(_iter)
    //           if condition:              // optional
    //             _lcN = _lcN + [resultExpr]
    //         } catch (_exn) {
    //           if (_exn == "StopIteration") break
    //           else throw _exn
    //         }
    //       }
    //     }
    //     // _lcN stays on stack as expression value
    //   }

    // ── Wrapping scope (so addLocal works even at function level) ──
    beginScope();

    // ── Step 1: Create empty result array ──
    std::string resultName = "_lc" + std::to_string(listCompCounter++);
    emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY), 0);  // push []
    addLocal(resultName);
    int resultSlot = resolveLocal(resultName);

    // ── Step 2: For-in scope ──
    beginScope();

    // let _iter = iter(iterable)
    int iterBuiltinSlot = resolveGlobal("iter");
    emitGetGlobal(iterBuiltinSlot);
    expr.iterable->accept(*this);
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
    addLocal("_iter");
    int iterLocalSlot = resolveLocal("_iter");

    // ── Step 3: while (true) loop ──
    size_t loopStart = chunk.code.size();
    // continueTarget = loopStart, extraLocalsToPopOnBreak = 1 (_iter)
    loopStack.push_back({loopStart, loopStart, {}, {}, scopeDepth, 1, 0});

    // ── Step 4: OP_PUSH_CATCH ──
    emitByte(static_cast<uint8_t>(OpCode::OP_PUSH_CATCH));
    emitByte(static_cast<uint8_t>(currentLocalCount()));
    size_t pushCatchPlaceholder = chunk.code.size();
    emitByte(0xFF);
    emitByte(0xFF);

    tryNesting++;

    // ── Step 5: var = next(_iter) ──
    int nextBuiltinSlot = resolveGlobal("next");
    emitGetGlobal(nextBuiltinSlot);
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iterLocalSlot));
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);

    beginScope();
    addLocal(expr.variable);

    // ── Step 6: Optional if condition ──
    size_t skipAppendJump = 0;
    size_t skipConditionPopJump = 0;
    if (expr.condition) {
        expr.condition->accept(*this);
        skipAppendJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));  // pop condition when true
    }

    // ── Step 7: Append to result ──
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(resultSlot));
    std::string tmpName = "_lx" + std::to_string(listCompCounter++);
    addLocal(tmpName);

    expr.resultExpr->accept(*this);

    emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY), 1);
    emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
    emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
              static_cast<uint8_t>(resultSlot));
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    if (locals.empty() || locals.back().name != tmpName) {
        error("Internal compiler error: list comprehension temp local mismatch");
        return;
    }
    scopeLocalCounts.back()--;
    locals.pop_back();

    if (expr.condition) {
        skipConditionPopJump = emitJump(OpCode::OP_JUMP);
        patchJump(skipAppendJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        patchJump(skipConditionPopJump);
    }

    // ── Step 8: End body scope (pops loop variable) ──
    endScope();

    tryNesting--;

    // Normal exit: pop catch handler, skip catch block
    emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    size_t skipCatchJump = emitJump(OpCode::OP_JUMP);

    // ── Step 9: Catch handler (StopIteration → break) ──
    size_t catchTarget = chunk.code.size();
    size_t catchJumpSize = catchTarget - pushCatchPlaceholder - 2;
    if (catchJumpSize > UINT16_MAX) catchJumpSize = UINT16_MAX;
    chunk.writeAt(pushCatchPlaceholder,
                  static_cast<uint8_t>(catchJumpSize & 0xFF));
    chunk.writeAt(pushCatchPlaceholder + 1,
                  static_cast<uint8_t>((catchJumpSize >> 8) & 0xFF));

    emitByte(static_cast<uint8_t>(OpCode::OP_CLEAR_EXCEPTION));
    emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));

    beginScope();
    addLocal("_exn");

    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(resolveLocal("_exn")));
    emitConstant(GcHeap::instance().alloc<GcString>("StopIteration"));
    emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));
    size_t notStopIterJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    visitBreakStmt(BreakStmt(expr.leftBracket));  // break uses leftBracket token for location
    patchJump(notStopIterJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(resolveLocal("_exn")));
    emitByte(static_cast<uint8_t>(OpCode::OP_THROW));

    endScope();

    patchJump(skipCatchJump);

    // ── Step 10: Loop back ──
    emitLoop(loopStart);

    // Patch break jumps
    std::vector<size_t> savedBreakJumps = std::move(loopStack.back().breakJumps);
    loopStack.pop_back();

    // ── Step 11: End for-in scope (pops _iter) ──
    endScope();

    for (size_t jumpOffset : savedBreakJumps) {
        patchJump(jumpOffset);
    }

    // ── Step 12: Manual wrapper cleanup (leave _lcN on stack) ──
    int wrapperCount = scopeLocalCounts.back();
    scopeLocalCounts.pop_back();
    for (int i = 0; i < wrapperCount; i++) {
        locals.pop_back();
    }
    scopeDepth--;
}

void Compiler::visitDictCompExpr(const DictCompExpr& expr) {
    currentLine = expr.leftBrace.line;
    currentColumn = expr.leftBrace.column;
    // Desugar dict comprehension into a for-in loop that accumulates results:
    //
    //   {keyExpr: valueExpr for var in iterable if condition}
    // becomes:
    //   {
    //     let _dcN = {}                    // result dict (OP_DICT 0)
    //     let _iter = iterable
    //     let _i = 0
    //     let _len = _vora_len(_iter)
    //     while (_i < _len) {
    //       let var = _iter[_i]
    //       if condition:
    //         _dcN = _dcN + {keyExpr: valueExpr}   // one-pair dict + OP_ADD merge
    //       _i += 1
    //     }
    //     // _dcN stays on stack as expression value
    //   }
    //
    // Bug #1 fix: wrapping beginScope() ensures scopeLocalCounts is non-empty.
    // Bug #2 fix: unconditional OP_JUMP over false-path OP_POP.
    // Bug #3 fix: _dx temp local tracks the OP_GET_LOCAL result on the stack.

    // ── Wrapping scope (Bug #1: so addLocal works even at scopeDepth == 0) ──
    beginScope();

    // ── Step 1: Create empty result dict ──
    std::string resultName = "_dc" + std::to_string(dictCompCounter++);
    emitBytes(static_cast<uint8_t>(OpCode::OP_DICT), 0);  // push {}
    addLocal(resultName);
    int resultSlot = resolveLocal(resultName);

    // ── Step 2: For-in scope ──
    beginScope();

    // let _iter = iter(iterable)
    int iterBuiltinSlot = resolveGlobal("iter");
    emitGetGlobal(iterBuiltinSlot);
    expr.iterable->accept(*this);
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
    addLocal("_iter");
    int iterLocalSlot = resolveLocal("_iter");

    // ── Step 3: while (true) loop ──
    size_t loopStart = chunk.code.size();
    loopStack.push_back({loopStart, loopStart, {}, {}, scopeDepth, 1, 0});

    // ── Step 4: OP_PUSH_CATCH ──
    emitByte(static_cast<uint8_t>(OpCode::OP_PUSH_CATCH));
    emitByte(static_cast<uint8_t>(currentLocalCount()));
    size_t pushCatchPlaceholder = chunk.code.size();
    emitByte(0xFF);
    emitByte(0xFF);

    tryNesting++;

    // ── Step 5: var = next(_iter) ──
    int nextBuiltinSlot = resolveGlobal("next");
    emitGetGlobal(nextBuiltinSlot);
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iterLocalSlot));
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);

    beginScope();
    addLocal(expr.variable);

    // ── Step 6: Optional if condition ──
    size_t skipAppendJump = 0;
    size_t skipConditionPopJump = 0;
    if (expr.condition) {
        expr.condition->accept(*this);
        skipAppendJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));  // pop condition when true
    }

    // ── Step 7: Merge one-pair dict: _dcN = _dcN + {keyExpr: valueExpr} ──
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(resultSlot));
    std::string dxName = "_dx" + std::to_string(dictCompCounter++);
    addLocal(dxName);

    expr.keyExpr->accept(*this);
    std::string dkName = "_dk" + std::to_string(dictCompCounter++);
    addLocal(dkName);

    expr.valueExpr->accept(*this);
    std::string dvName = "_dv" + std::to_string(dictCompCounter++);
    addLocal(dvName);

    emitBytes(static_cast<uint8_t>(OpCode::OP_DICT), 1);
    emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
    emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
              static_cast<uint8_t>(resultSlot));
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    if (locals.empty() || locals.back().name != dvName) {
        error("Internal compiler error: dict comprehension temp local mismatch (_dv)");
        return;
    }
    scopeLocalCounts.back()--;
    locals.pop_back();  // _dv
    if (locals.empty() || locals.back().name != dkName) {
        error("Internal compiler error: dict comprehension temp local mismatch (_dk)");
        return;
    }
    scopeLocalCounts.back()--;
    locals.pop_back();  // _dk
    if (locals.empty() || locals.back().name != dxName) {
        error("Internal compiler error: dict comprehension temp local mismatch (_dx)");
        return;
    }
    scopeLocalCounts.back()--;
    locals.pop_back();  // _dx

    if (expr.condition) {
        skipConditionPopJump = emitJump(OpCode::OP_JUMP);
        patchJump(skipAppendJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        patchJump(skipConditionPopJump);
    }

    // ── Step 8: End body scope (pops loop variable) ──
    endScope();

    tryNesting--;

    // Normal exit: pop catch handler, skip catch block
    emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    size_t skipCatchJump = emitJump(OpCode::OP_JUMP);

    // ── Step 9: Catch handler (StopIteration → break) ──
    size_t catchTarget = chunk.code.size();
    size_t catchJumpSize = catchTarget - pushCatchPlaceholder - 2;
    if (catchJumpSize > UINT16_MAX) catchJumpSize = UINT16_MAX;
    chunk.writeAt(pushCatchPlaceholder,
                  static_cast<uint8_t>(catchJumpSize & 0xFF));
    chunk.writeAt(pushCatchPlaceholder + 1,
                  static_cast<uint8_t>((catchJumpSize >> 8) & 0xFF));

    emitByte(static_cast<uint8_t>(OpCode::OP_CLEAR_EXCEPTION));
    emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));

    beginScope();
    addLocal("_exn");

    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(resolveLocal("_exn")));
    emitConstant(GcHeap::instance().alloc<GcString>("StopIteration"));
    emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));
    size_t notStopIterJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    visitBreakStmt(BreakStmt(expr.leftBrace));
    patchJump(notStopIterJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(resolveLocal("_exn")));
    emitByte(static_cast<uint8_t>(OpCode::OP_THROW));

    endScope();

    patchJump(skipCatchJump);

    // ── Step 10: Loop back ──
    emitLoop(loopStart);

    // Patch break jumps
    std::vector<size_t> savedBreakJumps = std::move(loopStack.back().breakJumps);
    loopStack.pop_back();

    // ── Step 11: End for-in scope (pops _iter) ──
    endScope();

    for (size_t jumpOffset : savedBreakJumps) {
        patchJump(jumpOffset);
    }

    // ── Step 12: Manual wrapper cleanup (leave _dcN on stack) ──
    int wrapperCount = scopeLocalCounts.back();
    scopeLocalCounts.pop_back();
    for (int i = 0; i < wrapperCount; i++) {
        locals.pop_back();
    }
    scopeDepth--;
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
        size_t nameIndex = identifierConstant(expr.property);
        emitGetSuper(nameIndex);
        return;
    }

    expr.object->accept(*this);
    size_t nameIndex = identifierConstant(expr.property);
    emitGetProperty(nameIndex);
}

void Compiler::visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) {
    currentLine = expr.dot.line;
    currentColumn = expr.dot.column;
    expr.object->accept(*this);
    expr.value->accept(*this);
    size_t nameIndex = identifierConstant(expr.property);
    emitSetProperty(nameIndex);
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
        emitGetGlobal(slot);
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
            emitSetGlobal(globalSlot);
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
            emitSetGlobal(globalSlot);
            }
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        return;
    }

    // Property target: ++obj.prop / obj.prop++
    if (auto prop = dynamic_cast<const PropertyExpr*>(expr.target.get())) {
        size_t propIndex = identifierConstant(prop->property);

        if (expr.isPrefix) {
            // Prefix: ++obj.prop → dup obj, get old, inc, set, leave new.
            prop->object->accept(*this);
            emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
            emitGetProperty(propIndex);
            emitConstant(delta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            emitSetProperty(propIndex);
        } else {
            // Postfix: obj.prop++ → return old value, store new value.
            prop->object->accept(*this);
            emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
            emitGetProperty(propIndex);
            emitConstant(delta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            emitSetProperty(propIndex);
            Value negDelta = (expr.op.type == TokenType::PLUS_PLUS)
                ? Value(static_cast<int64_t>(-1))
                : Value(static_cast<int64_t>(1));
            emitConstant(negDelta);
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
        }
        return;
    }

    // Index target: ++arr[i] / arr[i]++
    // Full support requires stack manipulation opcodes (DUP2/SWAP).
    // For now, report a clear error rather than silently miscompiling.
    if (dynamic_cast<const IndexExpr*>(expr.target.get())) {
        error("++/-- on index expressions (arr[i]) is not yet supported");
        // Fall through: push null to keep stack balanced
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
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

    // Use unique per-compilation-unit temp for scrutinee.
    // Access counter from the root compiler so nested functions don't
    // reset it and cause "__m0 already defined" conflicts.
    Compiler* root = this;
    while (root->enclosing) root = root->enclosing;
    std::string tempName = "__m" + std::to_string(root->matchCounter++);
    int tempSlot = defineGlobal(tempName);
    emitDefineGlobal(tempSlot);

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
        emitGetGlobal(tempSlot);

        if (case_.patterns[0].kind == PatternKind::Literal) {
            emitConstant(case_.patterns[0].literal);
            emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));
        } else if (case_.patterns[0].kind == PatternKind::Range) {
            const auto& pat = case_.patterns[0];
            emitConstant(pat.rangeLow);
            emitByte(static_cast<uint8_t>(OpCode::OP_GREATER_EQ_NN));
            size_t rangeLowFail = emitJump(OpCode::OP_JUMP_IF_FALSE);
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            emitGetGlobal(tempSlot);
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
    // Compile RHS, then unpack pattern assigning to existing variables.
    // compileAssignPattern saves the source in a temp and leaves it on
    // the stack as the expression result (assignment is an expression).
    if (!expr.value) {
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
        return;
    }

    expr.value->accept(*this);
    compileAssignPattern(*expr.binding);
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

    // Add fixed parameters as locals — destructured use compileBindPattern.
    for (const auto& param : expr.params) {
        if (param.isRest) break;
        if (param.pattern) {
            fnCompiler.compileBindPattern(*param.pattern, -1, false);
        } else {
            fnCompiler.addLocal(param.name);
        }
    }
    // Add rest parameter as a local
    if (hasRest) {
        fnCompiler.addLocal(expr.params.back().name);
    }

    // Emit default-parameter preamble for fixed params with defaults
    compileDefaultParamPreamble(fnCompiler, expr.params, /*slotBase=*/0,
                                requiredArity, totalArity);

    // Compile body statements
    for (const auto& s : expr.body->statements) {
        s->accept(fnCompiler);
    }

    // Execute deferred calls (LIFO) before implicit return
    fnCompiler.emitDeferCalls();

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
    for (const auto& param : expr.params) {
        if (param.isRest) break;
        proto.paramNames.push_back(param.name);
    }

    size_t protoIndex = addFunctionPrototype(std::move(proto));

    // Emit OP_CLOSURE to create the VoraFunction at runtime
    emitClosure(protoIndex, capturedUpvalues);
    // The closure is now on the stack — caller can assign, call, etc.
}

void Compiler::visitOptionalChainExpr(const OptionalChainExpr& expr) {
    // Compile: evaluate object, if null short-circuit to null; otherwise do operation.
    // Pattern:
    //   <object>
    //   DUP
    //   OP_JUMP_IF_NULL -> end    (pops the dup; if null, jumps leaving [object]=null)
    //   <operation: property/call/index>  (stack: [object], ready for operation)
    // end:

    expr.object->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
    size_t nullJump = emitJump(OpCode::OP_JUMP_IF_NULL);
    // No OP_POP here — OP_JUMP_IF_NULL already consumed the dup'd value.
    // The original object is on the stack, ready for the operation.

    currentLine = expr.questionDot.line;
    currentColumn = expr.questionDot.column;

    switch (expr.kind) {
        case OptionalChainExpr::Kind::PROPERTY: {
            size_t propIndex = identifierConstant(expr.property);
            emitGetPropertySafe(propIndex);
            break;
        }
        case OptionalChainExpr::Kind::CALL: {
            // Detect spread and named args in optional-chain call arguments.
            bool hasSpread = false;
            uint8_t fixedCount = 0;
            for (auto& arg : expr.arguments) {
                if (dynamic_cast<SpreadExpr*>(arg.get())) {
                    hasSpread = true;
                } else {
                    fixedCount++;
                }
            }
            bool hasNamed = false;
            for (const auto& name : expr.argumentNames) {
                if (!name.empty()) { hasNamed = true; break; }
            }

            if (!hasSpread && !hasNamed) {
                // Fast path: positional only
                for (auto& arg : expr.arguments) {
                    arg->accept(*this);
                }
                emitBytes(static_cast<uint8_t>(OpCode::OP_CALL),
                          static_cast<uint8_t>(expr.arguments.size()));
            } else if (hasSpread && !hasNamed) {
                emitByte(static_cast<uint8_t>(OpCode::OP_PUSH_SPREAD));
                for (auto& arg : expr.arguments) {
                    arg->accept(*this);
                }
                emitBytes(static_cast<uint8_t>(OpCode::OP_CALL_N), fixedCount);
            } else {
                // Named args path (spread + named = error, per visitCallExpr)
                if (hasSpread && hasNamed) {
                    error("Cannot mix named arguments with spread (...expr)");
                }
                uint8_t posCount = 0;
                uint8_t kwCount = 0;
                std::vector<size_t> kwNameIndices;
                for (size_t i = 0; i < expr.arguments.size(); i++) {
                    if (!expr.argumentNames[i].empty()) {
                        kwCount++;
                        kwNameIndices.push_back(
                            identifierConstant(expr.argumentNames[i]));
                    } else {
                        posCount++;
                    }
                }
                // Compile positional first, then named
                for (size_t i = 0; i < expr.arguments.size(); i++) {
                    if (expr.argumentNames[i].empty()) {
                        expr.arguments[i]->accept(*this);
                    }
                }
                for (size_t i = 0; i < expr.arguments.size(); i++) {
                    if (!expr.argumentNames[i].empty()) {
                        expr.arguments[i]->accept(*this);
                    }
                }
                emitCallKw(posCount, kwCount, kwNameIndices);
            }
            break;
        }
        case OptionalChainExpr::Kind::INDEX: {
            expr.index->accept(*this);
            emitByte(static_cast<uint8_t>(OpCode::OP_INDEX_SAFE));
            break;
        }
    }

    patchJump(nullJump);
}

} // namespace vora
