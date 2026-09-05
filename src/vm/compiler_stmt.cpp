#include "compiler.h"

#include <cctype>
#include <cmath>
#include <iostream>
#include <variant>

#include "../gc/gc_heap.h"
#include "../runtime/vora_function.h"

namespace vora {
// =========================================================================
// StmtVisitor — statement compilation
// =========================================================================

void Compiler::visitLetStmt(const LetStmt& stmt) {
    // Destructured binding: let [a, b] = expr  or  let {x, y} = expr
    if (stmt.binding) {
        compileDestructuredBinding(*stmt.binding, stmt.initializer.get(), stmt.isConst, stmt.typeAnnotation);
        return;
    }

    // Compile initializer (or null if none)
    if (stmt.initializer) {
        stmt.initializer->accept(*this);
    } else {
        // const must have an initializer
        if (stmt.isConst) {
            error("const variable '" + stmt.name + "' must be initialized");
            return;
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }

    // Apply type annotation conversion (let x:float = 1 → converts 1 to 1.0)
    if (!stmt.typeAnnotation.empty()) {
        emitConvert(stmt.typeAnnotation);
    }

    if (scopeDepth == 0) {
        // Global variable — use interned slot ID (errors on redefinition).
        int slot;
        if (stmt.isConst) {
            slot = defineGlobalConst(stmt.name);
        } else {
            slot = defineGlobal(stmt.name);
        }
        if (hadError) return;
        emitDefineGlobal(slot);
    } else {
        // Local variable — the initializer value is already on the stack.
        // It stays there as the local's slot. Just record the slot.
        addLocal(stmt.name, stmt.isConst);
        // The value is already on the stack at the local slot position.
        // No OP_SET_LOCAL needed — the initializer result IS the local.
    }
}

// =========================================================================
// Destructuring compilation helpers
// =========================================================================

void Compiler::compileDestructuredBinding(const BindingPattern& pattern,
                                          const Expr* initializer, bool isConst,
                                          const std::string& typeAnnotation) {
    // Compile the RHS value (the source array/object to destructure).
    if (initializer) {
        initializer->accept(*this);
    } else {
        if (isConst) {
            error("const destructuring requires an initializer");
            return;
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    }

    // For global scope, we pop values via OP_DEFINE_GLOBAL.
    // For local scope, they stay on the stack as locals.
    // The pattern compilation handles both cases.
    compileBindPattern(pattern, scopeDepth == 0 ? -1 : -1, isConst, typeAnnotation);
}

void Compiler::compileBindPattern(const BindingPattern& pattern,
                                  int sourceSlot, bool isConst,
                                  const std::string& typeAnnotation) {
    switch (pattern.kind()) {

    case BindingKind::Identifier: {
        const auto& id = static_cast<const IdentifierBinding&>(pattern);
        if (id.defaultValue) {
            emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
            size_t defaultJump = emitJump(OpCode::OP_JUMP_IF_NULL);
            size_t skipJump = emitJump(OpCode::OP_JUMP);
            patchJump(defaultJump);
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            id.defaultValue->accept(*this);
            patchJump(skipJump);
        }
        // Apply type annotation conversion before binding (let [a]:int = ["42"] → a==42)
        if (!typeAnnotation.empty()) {
            emitConvert(typeAnnotation);
        }
        // The value to bind is already on top of stack.
        if (scopeDepth == 0) {
            int slot;
            if (isConst) {
                slot = defineGlobalConst(id.name);
            } else {
                slot = defineGlobal(id.name);
            }
            if (hadError) return;
            emitDefineGlobal(slot);
        } else {
            addLocal(id.name, isConst);
        }
        break;
    }

    case BindingKind::Array: {
        const auto& arr = static_cast<const ArrayBinding&>(pattern);
        // Source array is on top of stack. Save it in a temp local so we
        // can access it repeatedly while processing elements.
        bool hasRest = arr.rest != nullptr;
        bool isGlobal = (scopeDepth == 0);
        int tempId = destructureTempCounter++;
        int srcSlot = -1;

        if (isGlobal) {
            // Global scope: use OP_DUP to keep array accessible.
            // OP_DEFINE_GLOBAL pops the bound value, leaving the DUP'd array.
        } else {
            // Local scope: save array in a temp local so we can reload it.
            // We can't DUP because addLocal doesn't pop — the local value
            // would stay on stack and the next DUP would dup it, not the array.
            std::string srcName = "_ds" + std::to_string(tempId) + "_src";
            addLocal(srcName);
            srcSlot = resolveLocal(srcName);
        }

        for (size_t i = 0; i < arr.elements.size(); i++) {
            if (isGlobal) {
                emitByte(static_cast<uint8_t>(OpCode::OP_DUP));  // dup array
            } else {
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));         // load array
            }
            emitConstant(static_cast<double>(i));             // push index
            emitByte(static_cast<uint8_t>(OpCode::OP_INDEX_SAFE)); // safe access
            compileBindPattern(*arr.elements[i], -1, isConst, typeAnnotation);
        }

        // Rest element: ...rest
        if (hasRest) {
            if (scopeDepth == 0) {
                error("Rest element in destructuring is only supported inside functions");
                emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
                compileBindPattern(*arr.rest, -1, isConst, typeAnnotation);
            } else {
                int restStartIdx = static_cast<int>(arr.elements.size());

                // Create empty rest array
                std::string arrName = "_ds" + std::to_string(tempId) + "_arr";
                emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY), 0);
                addLocal(arrName);
                int arrSlot = resolveLocal(arrName);

                // Loop counter: _rest_i = restStartIdx
                std::string iName = "_ds" + std::to_string(tempId) + "_i";
                emitConstant(static_cast<double>(restStartIdx));
                addLocal(iName);
                int iSlot = resolveLocal(iName);

                // Get array length: _rest_len = _vora_len(_arr_src)
                std::string lenName = "_ds" + std::to_string(tempId) + "_len";
                int lenBuiltinSlot = resolveGlobal("_vora_len");
                emitGetGlobal(lenBuiltinSlot);
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
                addLocal(lenName);
                int lenSlot = resolveLocal(lenName);

                // While loop: _rest_i < _rest_len
                size_t loopStart = chunk.code.size();
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(lenSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_LESS_NN));
                size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

                // Body: _rest_arr = _rest_arr + [arr[i]]
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(arrSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
                emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY), 1);
                emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(arrSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                // i = i + 1
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitConstant(1.0);
                emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                emitLoop(loopStart);
                patchJump(exitJump);
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                // Push rest array and bind
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(arrSlot));
                compileBindPattern(*arr.rest, -1, isConst, typeAnnotation);
            }
        }

        // Pop the source array temp (no longer needed)
        // Actually, the temp will be cleaned up by endScope.
        // For global scope, pop the original array.
        if (scopeDepth == 0) {
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        break;
    }

    case BindingKind::Object: {
        const auto& obj = static_cast<const ObjectBinding&>(pattern);
        // Source object is on top of stack. Save in temp local.
        int tempId = destructureTempCounter++;
        bool isGlobal = (scopeDepth == 0);
        int srcSlot = -1;

        if (isGlobal) {
            // Global scope: OP_DUP works because OP_DEFINE_GLOBAL pops.
        } else {
            std::string srcName = "_ds" + std::to_string(tempId) + "_obj";
            addLocal(srcName);
            srcSlot = resolveLocal(srcName);
        }

        for (size_t i = 0; i < obj.properties.size(); i++) {
            const auto& prop = obj.properties[i];
            if (isGlobal) {
                emitByte(static_cast<uint8_t>(OpCode::OP_DUP));  // dup object
            } else {
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));         // load object
            }
            size_t keyIndex = identifierConstant(prop.key);
            emitGetPropertySafe(keyIndex);
            compileBindPattern(*prop.pattern, -1, isConst, typeAnnotation);
        }

        // Object rest: ...rest
        if (obj.rest) {
            if (scopeDepth == 0) {
                error("Object rest in destructuring is only supported inside functions");
                emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
                compileBindPattern(*obj.rest, -1, isConst, typeAnnotation);
            } else {
                // ── Strategy: copy all keys to rest dict, then remove explicit ones ──
                // Desugars to:
                //   let _rest = {}
                //   let _keys = _src.keys()
                //   loop _i over _keys:
                //     let _k = _keys[_i]
                //     _rest[_k] = _src[_k]
                //   for each explicit property: _rest.remove(propName)
                //   let rest = _rest

                // ── Create empty rest dict ──
                std::string restName = "_ds" + std::to_string(tempId) + "_rest";
                emitBytes(static_cast<uint8_t>(OpCode::OP_DICT), 0);
                addLocal(restName);
                int restSlot = resolveLocal(restName);

                // ── Get source keys: _keys = _src.keys() ──
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));
                size_t keysMethodIdx = identifierConstant("keys");
                emitGetProperty(keysMethodIdx);
                emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 0);
                std::string keysName = "_ds" + std::to_string(tempId) + "_keys";
                addLocal(keysName);
                int keysSlot = resolveLocal(keysName);

                // ── Loop counter: _i = 0 ──
                std::string iName = "_ds" + std::to_string(tempId) + "_i";
                emitConstant(0.0);
                addLocal(iName);
                int iSlot = resolveLocal(iName);

                // ── Loop bound: _len = _vora_len(_keys) ──
                std::string lenName = "_ds" + std::to_string(tempId) + "_len";
                int lenBuiltin = resolveGlobal("_vora_len");
                emitGetGlobal(lenBuiltin);
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(keysSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
                addLocal(lenName);
                int lenSlot = resolveLocal(lenName);

                // ── While loop: _i < _len ──
                size_t restLoopStart = chunk.code.size();
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(lenSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_LESS_NN));
                size_t restExitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                // ── _k = _keys[_i] ──
                beginScope();
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(keysSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
                std::string kName = "_ds" + std::to_string(tempId) + "_k";
                addLocal(kName);
                int kSlot = resolveLocal(kName);

                // ── _rest[_k] = _src[_k]  (copy value) ──
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(restSlot));       // _rest
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(kSlot));          // _k (key)
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));        // _src
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(kSlot));          // _k (key)
                emitByte(static_cast<uint8_t>(OpCode::OP_INDEX)); // _src[_k]
                emitByte(static_cast<uint8_t>(OpCode::OP_SET_INDEX)); // _rest[_k] = val
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));  // discard value
                endScope();

                // ── _i = _i + 1 ──
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitConstant(1.0);
                emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                emitLoop(restLoopStart);
                patchJump(restExitJump);
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                // ── Remove explicit properties from rest dict ──
                for (size_t pi = 0; pi < obj.properties.size(); pi++) {
                    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                              static_cast<uint8_t>(restSlot));
                    size_t removeIdx = identifierConstant("remove");
                    emitGetProperty(removeIdx);
                    size_t keyIdx = identifierConstant(obj.properties[pi].key);
                    emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT),
                              static_cast<uint8_t>(keyIdx));
                    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
                    emitByte(static_cast<uint8_t>(OpCode::OP_POP));  // discard return
                }

                // ── Push rest dict and bind to rest variable ──
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(restSlot));
                compileBindPattern(*obj.rest, -1, isConst, typeAnnotation);
            }
        }

        // Pop source object at global scope
        if (scopeDepth == 0) {
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        break;
    }
    }
}

void Compiler::compileAssignPattern(const BindingPattern& pattern) {
    // Bare destructuring assignment: [a, b] = arr  or  {x, y} = obj
    // Unlike compileBindPattern (which declares NEW variables), this assigns
    // to EXISTING variables. The value to assign is on top of the stack;
    // after assignment we POP to keep the stack clean (the source temp
    // managed by visitDestructureAssignmentExpr stays as expression result).

    switch (pattern.kind()) {

    case BindingKind::Identifier: {
        const auto& id = static_cast<const IdentifierBinding&>(pattern);
        if (id.defaultValue) {
            emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
            size_t defaultJump = emitJump(OpCode::OP_JUMP_IF_NULL);
            size_t skipJump = emitJump(OpCode::OP_JUMP);
            patchJump(defaultJump);
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            id.defaultValue->accept(*this);
            patchJump(skipJump);
        }
        // Value is on top of stack. Assign to existing variable.
        int localSlot = resolveLocal(id.name);
        if (localSlot >= 0) {
            if (isLocalConst(id.name)) {
                error("Cannot assign to const variable '" + id.name + "'");
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
                return;
            }
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                      static_cast<uint8_t>(localSlot));
        } else {
            int upvalueIdx = resolveUpvalue(this, id.name);
            if (upvalueIdx >= 0) {
                if (isUpvalueConst(upvalueIdx)) {
                    error("Cannot assign to const variable '" + id.name + "'");
                    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
                    return;
                }
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE),
                          static_cast<uint8_t>(upvalueIdx));
            } else {
                int slot = resolveGlobal(id.name);
                // Check global const
                Compiler* root = this;
                while (root->enclosing) root = root->enclosing;
                if (slot >= 0 && static_cast<size_t>(slot) < root->globalIsConst.size() &&
                    root->globalIsConst[static_cast<size_t>(slot)]) {
                    error("Cannot assign to const variable '" + id.name + "'");
                    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
                    return;
                }
                emitSetGlobal(slot);
            }
        }
        // OP_SET_LOCAL/SET_UPVALUE/SET_GLOBAL don't pop — clean up manually
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        break;
    }

    case BindingKind::Array: {
        const auto& arr = static_cast<const ArrayBinding&>(pattern);
        // Use OP_DUP to keep source accessible — avoids temp locals that
        // would interfere with the expression result at the call site.
        int tempId = destructureTempCounter++;

        for (size_t i = 0; i < arr.elements.size(); i++) {
            emitByte(static_cast<uint8_t>(OpCode::OP_DUP));  // dup source
            emitConstant(static_cast<double>(i));             // push index
            emitByte(static_cast<uint8_t>(OpCode::OP_INDEX)); // source[i]
            compileAssignPattern(*arr.elements[i]);           // assign + pop
        }

        // Rest element: ...rest (local scope only — needs temp locals)
        if (arr.rest) {
            if (scopeDepth == 0) {
                error("Rest element in destructuring is only supported inside functions");
                emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
                compileAssignPattern(*arr.rest);
            } else {
                // Save source in a temp so the rest loop can reload it.
                // The source is at the TOP of stack; addLocal names it.
                std::string srcName = "_da" + std::to_string(tempId) + "_src";
                addLocal(srcName);
                int srcSlot = resolveLocal(srcName);

                int restStartIdx = static_cast<int>(arr.elements.size());

                // Create empty rest array
                std::string restArrName = "_da" + std::to_string(tempId) + "_arr";
                emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY), 0);
                addLocal(restArrName);
                int arrSlot = resolveLocal(restArrName);

                // Loop counter: _i = restStartIdx
                std::string iName = "_da" + std::to_string(tempId) + "_i";
                emitConstant(static_cast<double>(restStartIdx));
                addLocal(iName);
                int iSlot = resolveLocal(iName);

                // Get array length: _len = _vora_len(_src)
                std::string lenName = "_da" + std::to_string(tempId) + "_len";
                int lenBuiltinSlot = resolveGlobal("_vora_len");
                emitGetGlobal(lenBuiltinSlot);
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
                addLocal(lenName);
                int lenSlot = resolveLocal(lenName);

                // While loop: _i < _len
                size_t loopStart = chunk.code.size();
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(lenSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_LESS_NN));
                size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

                // Body: _arr = _arr + [_src[_i]]
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(arrSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
                emitBytes(static_cast<uint8_t>(OpCode::OP_ARRAY), 1);
                emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(arrSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                // i = i + 1
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitConstant(1.0);
                emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                emitLoop(loopStart);
                patchJump(exitJump);
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                // Push rest array and assign
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(arrSlot));
                compileAssignPattern(*arr.rest);
            }
        }
        break;
    }

    case BindingKind::Object: {
        const auto& obj = static_cast<const ObjectBinding&>(pattern);
        // Use OP_DUP to keep source accessible (same reasoning as Array case).
        int tempId = destructureTempCounter++;

        for (size_t i = 0; i < obj.properties.size(); i++) {
            const auto& prop = obj.properties[i];
            emitByte(static_cast<uint8_t>(OpCode::OP_DUP));  // dup source
            size_t keyIndex = identifierConstant(prop.key);
            emitGetProperty(keyIndex);
            compileAssignPattern(*prop.pattern);             // assign + pop
        }

        // Object rest: ...rest (local scope only — needs temp locals)
        if (obj.rest) {
            if (scopeDepth == 0) {
                error("Object rest in destructuring is only supported inside functions");
                emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
                compileAssignPattern(*obj.rest);
            } else {
                // Save source in a temp so the rest logic can reload it
                std::string srcName = "_da" + std::to_string(tempId) + "_obj";
                addLocal(srcName);
                int srcSlot = resolveLocal(srcName);

                std::string restName = "_da" + std::to_string(tempId) + "_rest";
                emitBytes(static_cast<uint8_t>(OpCode::OP_DICT), 0);
                addLocal(restName);
                int restSlot = resolveLocal(restName);

                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));
                size_t keysMethodIdx = identifierConstant("keys");
                emitGetProperty(keysMethodIdx);
                emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 0);
                std::string keysName = "_da" + std::to_string(tempId) + "_keys";
                addLocal(keysName);
                int keysSlot = resolveLocal(keysName);

                std::string iName = "_da" + std::to_string(tempId) + "_i";
                emitConstant(0.0);
                addLocal(iName);
                int iSlot = resolveLocal(iName);

                std::string lenName = "_da" + std::to_string(tempId) + "_len";
                int lenBuiltin = resolveGlobal("_vora_len");
                emitGetGlobal(lenBuiltin);
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(keysSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
                addLocal(lenName);
                int lenSlot = resolveLocal(lenName);

                size_t restLoopStart = chunk.code.size();
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(lenSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_LESS_NN));
                size_t restExitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                beginScope();
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(keysSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
                std::string kName = "_da" + std::to_string(tempId) + "_k";
                addLocal(kName);
                int kSlot = resolveLocal(kName);

                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(restSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(kSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(srcSlot));
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(kSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_INDEX));
                emitByte(static_cast<uint8_t>(OpCode::OP_SET_INDEX));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
                endScope();

                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitConstant(1.0);
                emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL),
                          static_cast<uint8_t>(iSlot));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                emitLoop(restLoopStart);
                patchJump(restExitJump);
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                for (size_t pi = 0; pi < obj.properties.size(); pi++) {
                    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                              static_cast<uint8_t>(restSlot));
                    size_t removeIdx = identifierConstant("remove");
                    emitGetProperty(removeIdx);
                    size_t keyIdx = identifierConstant(obj.properties[pi].key);
                    emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT),
                              static_cast<uint8_t>(keyIdx));
                    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
                    emitByte(static_cast<uint8_t>(OpCode::OP_POP));
                }

                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
                          static_cast<uint8_t>(restSlot));
                compileAssignPattern(*obj.rest);
            }
        }
        break;
    }
    }
}

void Compiler::visitReturnStmt(const ReturnStmt& stmt) {
    // Execute deferred calls (LIFO) before returning
    emitDeferFlush();

    // Pop catch handlers for any enclosing try blocks before returning
    for (int t = 0; t < tryNesting; t++) {
        emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    }

    // Tail call optimization (TCO): when returning a direct function call
    // and we're not inside a try/finally block, emit OP_TAIL_CALL to
    // reuse the current call frame instead of growing the stack.
    // This enables infinite tail recursion without stack overflow.
    if (stmt.value && tryNesting == 0 && finallyNesting == 0) {
        if (auto* call = dynamic_cast<CallExpr*>(stmt.value.get())) {
            // Skip TCO if any argument uses call-site spread — OP_TAIL_CALL
            // uses a fixed arg count and can't handle dynamically-sized spreads.
            // The regular path below routes through visitCallExpr → OP_CALL_N.
            bool hasSpread = false;
            for (const auto& arg : call->arguments) {
                if (dynamic_cast<SpreadExpr*>(arg.get())) {
                    hasSpread = true;
                    break;
                }
            }
            if (!hasSpread) {
                // Compile callee first (goes below arguments on stack)
                call->callee->accept(*this);
                // Compile arguments (pushed on top of callee)
                for (const auto& arg : call->arguments) {
                    arg->accept(*this);
                }
                emitBytes(static_cast<uint8_t>(OpCode::OP_TAIL_CALL),
                          static_cast<uint8_t>(call->arguments.size()));
                return;  // OP_TAIL_CALL handles frame reuse, no OP_RETURN needed
            }
        }
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
    loopStack.push_back({loopStart, loopStart, {}, {}, scopeDepth, 0, 0});

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

void Compiler::visitDoWhileStmt(const DoWhileStmt& stmt) {
    size_t loopStart = chunk.code.size();

    // Push loop context with continueTarget = SIZE_MAX initially.
    // Unlike while (where continueTarget == loopStart for backward jump),
    // do-while has continue jump forward to the condition, which hasn't
    // been compiled yet. This matches the for-in/C-for pattern.
    loopStack.push_back({loopStart, SIZE_MAX, {}, {}, scopeDepth, 0, 0});

    // --- Body (always executes at least once, unconditionally) ---
    stmt.body->accept(*this);

    // --- Condition (continue target) ---
    // Record this position as the continue target and patch any forward
    // continue jumps emitted during body compilation.
    loopStack.back().continueTarget = chunk.code.size();
    for (size_t jumpOffset : loopStack.back().continueJumps) {
        patchJump(jumpOffset);
    }
    loopStack.back().continueJumps.clear();

    stmt.condition->accept(*this);
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Pop truthy condition value from successful check
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Loop back to body start (not condition — body re-executes)
    emitLoop(loopStart);

    // --- Exit ---
    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Patch all break jumps
    for (size_t jumpOffset : loopStack.back().breakJumps) {
        patchJump(jumpOffset);
    }

    loopStack.pop_back();
}

void Compiler::visitForStmt(const ForStmt& stmt) {
    currentLine = stmt.forToken.line;
    currentColumn = stmt.forToken.column;
    // Desugar for-in using the iter()/next() iterator protocol:
    //
    //   for x in iterable { body }
    // becomes:
    //   {
    //     let _iter = iter(iterable)
    //     while (true) {
    //       try {
    //         let x = next(_iter)
    //         body
    //       } catch (_exn) {
    //         if (_exn == "StopIteration") break
    //         else throw _exn
    //       }
    //     }
    //   }
    //
    // This works for all iterable types: arrays, strings, dicts, sets, maps,
    // objects, and generators — the iter()/next() protocol handles each.

    beginScope();

    // --- let _iter = iter(iterable) ---
    int iterBuiltinSlot = resolveGlobal("iter");
    emitGetGlobal(iterBuiltinSlot);
    stmt.iterable->accept(*this);
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
    addLocal("_iter");
    int iterLocalSlot = resolveLocal("_iter");

    // --- while (true) ---
    size_t loopStart = chunk.code.size();
    // continueTarget = loopStart (continue jumps go back to top via OP_LOOP).
    // extraLocalsToPopOnBreak = 1 (_iter), extraLocalsToPopOnContinue = 0.
    loopStack.push_back({loopStart, loopStart, {}, {}, scopeDepth, 1, 0});

    // --- OP_PUSH_CATCH: try { let x = next(_iter); body } ---
    emitByte(static_cast<uint8_t>(OpCode::OP_PUSH_CATCH));
    emitByte(static_cast<uint8_t>(currentLocalCount()));
    size_t pushCatchPlaceholder = chunk.code.size();
    emitByte(0xFF);  // offset low placeholder
    emitByte(0xFF);  // offset high placeholder

    tryNesting++;

    // Compile next(_iter) and bind to loop variable or pattern.
    int nextBuiltinSlot = resolveGlobal("next");
    emitGetGlobal(nextBuiltinSlot);
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(iterLocalSlot));
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);

    beginScope();
    if (stmt.variablePattern) {
        compileBindPattern(*stmt.variablePattern, -1, false);
    } else {
        error("Internal compiler error: missing for-loop binding pattern");
    }
    stmt.body->accept(*this);
    endScope();

    tryNesting--;

    // Normal try exit: pop catch handler and skip the catch block
    emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));
    size_t skipCatchJump = emitJump(OpCode::OP_JUMP);

    // --- Catch handler: if StopIteration → break, else → re-throw ---
    size_t catchTarget = chunk.code.size();
    size_t catchJumpSize = catchTarget - pushCatchPlaceholder - 2;
    if (catchJumpSize > UINT16_MAX) catchJumpSize = UINT16_MAX;
    chunk.writeAt(pushCatchPlaceholder,
                  static_cast<uint8_t>(catchJumpSize & 0xFF));
    chunk.writeAt(pushCatchPlaceholder + 1,
                  static_cast<uint8_t>((catchJumpSize >> 8) & 0xFF));

    // Clear exception-in-flight and pop the catch handler at catch entry
    emitByte(static_cast<uint8_t>(OpCode::OP_CLEAR_EXCEPTION));
    emitByte(static_cast<uint8_t>(OpCode::OP_POP_CATCH));

    // Bind the exception value to internal variable _exn
    beginScope();
    addLocal("_exn");

    // if (_exn == "StopIteration") { pop _exn; break; } else { throw _exn; }
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(resolveLocal("_exn")));
    emitConstant(GcHeap::instance().alloc<GcString>("StopIteration"));
    emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));
    size_t notStopIterJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));  // pop comparison result
    // Break out of the while loop (emit break jump → patched after endScope)
    visitBreakStmt(BreakStmt(stmt.forToken));  // uses forToken as approximate location
    // else: not StopIteration — re-throw
    patchJump(notStopIterJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));  // pop comparison result
    // Re-throw: push _exn and throw
    emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL),
              static_cast<uint8_t>(resolveLocal("_exn")));
    emitByte(static_cast<uint8_t>(OpCode::OP_THROW));

    endScope();  // _exn scope

    // After catch (normal flow after each iteration)
    patchJump(skipCatchJump);

    // Loop back to try the next element
    emitLoop(loopStart);

    // --- Patch break jumps (from StopIteration catch and user break in body) ---
    // Save break jumps before popping the loop context. Must be patched AFTER
    // endScope() because break paths already handled _iter cleanup via
    // extraLocalsToPopOnBreak — landing before endScope() would double-pop.
    std::vector<size_t> savedBreakJumps = std::move(loopStack.back().breakJumps);
    loopStack.pop_back();

    // End outer scope (pops _iter for normal exit path)
    endScope();

    // Patch break jumps after endScope()
    for (size_t jumpOffset : savedBreakJumps) {
        patchJump(jumpOffset);
    }
}

void Compiler::visitCForStmt(const CForStmt& stmt) {
    // Desugar C-style for into a while loop:
    //
    //   for (init; cond; incr) { body }
    // becomes (pseudo-bytecode):
    //   {
    //     init;                        // let locals at scope N
    //     loopStart:
    //       if (!cond) goto exit;
    //       body;                      // may add nested scopes
    //     continueTarget:
    //       incr;
    //       goto loopStart;
    //     exit:
    //   }
    //
    // Scoping follows the for-in pattern:
    //   - beginScope wraps the whole loop (initializer locals are scoped here)
    //   - enclosingScopeDepth = scopeDepth (inside the scope) so continue only
    //     pops body-locals, never the initializer locals
    //   - extraLocalsToPopOnBreak tracks initializer locals so break can clean
    //     them up (extraLocalsToPopOnContinue is 0 — initializers persist)
    //   - Break jumps land after endScope(); continue jumps land at increment

    beginScope();

    // Record how many locals the initializer adds (for break cleanup)
    size_t localsBeforeInit = locals.size();

    // --- Initializer ---
    if (stmt.initializer) {
        stmt.initializer->accept(*this);
    }

    int extraLocals = static_cast<int>(locals.size() - localsBeforeInit);

    size_t loopStart = chunk.code.size();
    // continueTarget = SIZE_MAX signals "target will be set later"
    loopStack.push_back({loopStart, SIZE_MAX, {}, {}, scopeDepth, extraLocals, 0});

    // --- Condition ---
    if (stmt.condition) {
        stmt.condition->accept(*this);
    } else {
        // Empty condition = always true
        emitByte(static_cast<uint8_t>(OpCode::OP_TRUE));
    }
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // --- Body ---
    stmt.body->accept(*this);

    // --- Increment (continue target) ---
    loopStack.back().continueTarget = chunk.code.size();
    if (stmt.increment) {
        stmt.increment->accept(*this);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));  // discard increment result
    }

    // --- Loop back ---
    emitLoop(loopStart);

    // --- Exit ---
    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::OP_POP));

    // Patch continue jumps to the increment section
    for (size_t jumpOffset : loopStack.back().continueJumps) {
        size_t target = loopStack.back().continueTarget;
        size_t jumpSize = target - jumpOffset - 2;
        if (jumpSize > UINT16_MAX) jumpSize = UINT16_MAX;
        chunk.writeAt(jumpOffset, static_cast<uint8_t>(jumpSize & 0xFF));
        chunk.writeAt(jumpOffset + 1, static_cast<uint8_t>((jumpSize >> 8) & 0xFF));
    }

    // Save break jumps before popping the loop context.
    // Break path: pops extraLocals (init locals) + body locals → OP_JUMP.
    // Normal path: endScope() pops init locals.
    std::vector<size_t> savedBreakJumps = std::move(loopStack.back().breakJumps);
    loopStack.pop_back();

    // End the C-for scope (normal exit path — pops initializer locals)
    endScope();

    // Patch break jumps after endScope() — break already cleaned up init locals
    // via extraLocalsToPopOnBreak, so it skips endScope().
    for (size_t jumpOffset : savedBreakJumps) {
        patchJump(jumpOffset);
    }
}

void Compiler::visitFuncStmt(const FuncStmt& stmt) {
    // Create a separate compiler for the function body
    Compiler fnCompiler(errorReporter_);
    fnCompiler.enclosing = this;
    fnCompiler.chunk.source = chunk.source;  // propagate source for error display
    fnCompiler.isAsync = stmt.isAsync;
    if (stmt.isAsync) fnCompiler.isGenerator = true;  // async funcs are always generators

    // Function body starts a new scope with parameters as locals
    fnCompiler.beginScope();

    // Detect rest parameter (always last if present)
    bool hasRest = !stmt.params.empty() && stmt.params.back().isRest;

    // Count required params (without defaults, excluding rest)
    int requiredArity = 0;
    for (const auto& param : stmt.params) {
        if (param.isRest) break;
        if (!param.defaultValue) requiredArity++;
        else break;  // defaults must come after required params (parser enforces)
    }
    int totalArity = hasRest
        ? static_cast<int>(stmt.params.size()) - 1
        : static_cast<int>(stmt.params.size());

    // Add fixed parameters as locals (they'll be bound by callVoraFunction).
    // Destructured params use compileBindPattern which may create temp locals.
    for (const auto& param : stmt.params) {
        if (param.isRest) break;
        if (param.pattern) {
            fnCompiler.compileBindPattern(*param.pattern, -1, false);
        } else {
            fnCompiler.addLocal(param.name);
        }
    }
    // Add rest parameter as a local (value set by callVoraFunction as array)
    if (hasRest) {
        fnCompiler.addLocal(stmt.params.back().name);
    }

    // Emit default-parameter preamble for fixed params with defaults
    compileDefaultParamPreamble(fnCompiler, stmt.params, /*slotBase=*/0,
                                requiredArity, totalArity);

    // Pre-allocate local for self-recursion: add the function name as a
    // local in the ENCLOSING scope BEFORE compiling the body. This lets
    // recursive references resolve via upvalue capture. The OP_CLOSURE
    // result will be pushed onto the stack at this local's position.
    if (scopeDepth > 0) {
        addLocal(stmt.name);
    }

    // Compile the function body
    for (const auto& s : stmt.body->statements) {
        s->accept(fnCompiler);
    }

    // Execute deferred calls (LIFO) before implicit return
    fnCompiler.emitDeferFlush();

    // Implicit return null at end of function
    fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

    // End parameter scope (will POPN params)
    fnCompiler.endScope();

    // Build function prototype (copy upvalues before moving)
    auto capturedUpvalues = fnCompiler.upvalues;  // copy before move
    FunctionPrototype proto;
    proto.name = stmt.name;
    proto.arity = totalArity;
    proto.requiredArity = requiredArity;
    proto.hasRest = hasRest;
    proto.upvalues = std::move(fnCompiler.upvalues);
    proto.chunk = std::move(fnCompiler.chunk);
    proto.isGenerator = fnCompiler.isGenerator;
    proto.isAsync = fnCompiler.isAsync;
    for (const auto& param : stmt.params) {
        if (param.isRest) break;
        proto.paramNames.push_back(param.name);
    }
    // Collect local variable names for debugger
    for (const auto& local : fnCompiler.locals) {
        if (local.depth >= 0) {
            proto.localNames.push_back(local.name);
        }
    }

    // Store prototype in constant pool
    size_t protoIndex = addFunctionPrototype(std::move(proto));

    // Emit OP_CLOSURE to create the runtime VoraFunction
    emitClosure(protoIndex, capturedUpvalues);

    // Bind to name
    if (scopeDepth == 0) {
        // Global function — use interned slot ID (errors on redefinition).
        int slot = defineGlobal(stmt.name);
        if (hadError) return;
        emitDefineGlobal(slot);
    }
    // For local functions (scopeDepth > 0): the name was pre-allocated via
    // addLocal before body compilation. OP_CLOSURE already pushed the
    // function onto the stack — since addLocal was called beforehand and
    // the stack grows from the local region, the pushed value is already
    // at the correct position for the pre-allocated local slot. No extra
    // OP_SET_LOCAL or OP_POP needed.
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
    std::vector<GcPtr<FunctionPrototype>> methodProtos;
    std::vector<GcPtr<FunctionPrototype>> staticMethodProtos;
    for (const auto& methodStmt : stmt.methods) {
        if (auto funcStmt = dynamic_cast<const FuncStmt*>(methodStmt.get())) {
            Compiler methodCompiler(errorReporter_);
            methodCompiler.enclosing = this;
            methodCompiler.chunk.source = chunk.source;  // propagate source for error display

            bool isStatic = funcStmt->isStatic;

            // Detect rest parameter for methods
            bool mHasRest = !funcStmt->params.empty() && funcStmt->params.back().isRest;

            // Count required params for methods (excluding rest)
            int mRequiredArity = 0;
            for (const auto& mp : funcStmt->params) {
                if (mp.isRest) break;
                if (!mp.defaultValue) mRequiredArity++;
                else break;
            }
            int mTotalArity = mHasRest
                ? static_cast<int>(funcStmt->params.size()) - 1
                : static_cast<int>(funcStmt->params.size());

            methodCompiler.beginScope();
            // Instance methods: 'this' at slot 0, params at slots 1+
            // Static methods: no 'this', params at slots 0+
            if (!isStatic) {
                methodCompiler.addLocal("this");
            }
            // Add fixed parameters as locals — destructured params use compileBindPattern.
            for (const auto& param : funcStmt->params) {
                if (param.isRest) break;
                if (param.pattern) {
                    methodCompiler.compileBindPattern(*param.pattern, -1, false);
                } else {
                    methodCompiler.addLocal(param.name);
                }
            }
            // Add rest parameter as a local
            if (mHasRest) {
                methodCompiler.addLocal(funcStmt->params.back().name);
            }

            // Emit default-parameter preamble.
            // Instance methods: slotBase=1 (after 'this')
            // Static methods:  slotBase=0 (no 'this')
            int slotBase = isStatic ? 0 : 1;
            compileDefaultParamPreamble(methodCompiler, funcStmt->params,
                                        slotBase, mRequiredArity, mTotalArity);

            // Compile method body
            for (const auto& s : funcStmt->body->statements) {
                s->accept(methodCompiler);
            }

            methodCompiler.emitDeferFlush();
            methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
            methodCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

            methodCompiler.endScope();

            auto proto = GcHeap::instance().alloc<FunctionPrototype>();
            proto->name = funcStmt->name;
            proto->arity = mTotalArity;
            proto->requiredArity = mRequiredArity;
            proto->hasRest = mHasRest;
            proto->chunk = std::move(methodCompiler.chunk);
            for (const auto& param : funcStmt->params) {
                if (param.isRest) break;
                proto->paramNames.push_back(param.name);
            }
            if (isStatic) {
                staticMethodProtos.push_back(proto);
            } else {
                methodProtos.push_back(proto);
            }
        }
    }

    // Compile constructor body
    // Detect rest parameter for constructor
    bool ctorHasRest = !stmt.params.empty() && stmt.params.back().isRest;

    // Count required params (excluding rest)
    int ctorRequiredArity = 0;
    for (const auto& cp : stmt.params) {
        if (cp.isRest) break;
        if (!cp.defaultValue) ctorRequiredArity++;
        else break;
    }
    int ctorTotalArity = ctorHasRest
        ? static_cast<int>(stmt.params.size()) - 1
        : static_cast<int>(stmt.params.size());

    Compiler ctorCompiler(errorReporter_);
    ctorCompiler.enclosing = this;
    ctorCompiler.chunk.source = chunk.source;  // propagate source for error display
    ctorCompiler.beginScope();
    ctorCompiler.addLocal("this");  // slot 0
    // Add fixed parameters (slots 1, 2, ...) — destructured use compileBindPattern.
    for (const auto& param : stmt.params) {
        if (param.isRest) break;
        if (param.pattern) {
            ctorCompiler.compileBindPattern(*param.pattern, -1, false);
        } else {
            ctorCompiler.addLocal(param.name);
        }
    }
    // Add rest parameter as a local
    if (ctorHasRest) {
        ctorCompiler.addLocal(stmt.params.back().name);
    }

    // Emit default-parameter preamble for constructor params with defaults
    compileDefaultParamPreamble(ctorCompiler, stmt.params,
                                /*slotBase=*/1, ctorRequiredArity, ctorTotalArity);

    for (const auto& s : stmt.body->statements) {
        s->accept(ctorCompiler);
    }
    ctorCompiler.emitDeferFlush();
    ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NULL));
    ctorCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    ctorCompiler.endScope();

    auto ctorProto = GcHeap::instance().alloc<FunctionPrototype>();
    ctorProto->name = stmt.name;
    ctorProto->arity = ctorTotalArity;
    ctorProto->requiredArity = ctorRequiredArity;
    ctorProto->hasRest = ctorHasRest;
    ctorProto->chunk = std::move(ctorCompiler.chunk);

    // Extract param names for ClassDefinition and FunctionPrototype
    std::vector<std::string> paramNames;
    for (const auto& p : stmt.params) {
        paramNames.push_back(p.name);
    }
    ctorProto->paramNames = paramNames;

    // Store class definition in constant pool
    auto classDef = GcHeap::instance().alloc<ClassDefinition>();
    classDef->name = stmt.name;
    classDef->parentNames = stmt.parentNames;
    classDef->params = paramNames;
    classDef->ctorProto = ctorProto;
    classDef->methodProtos = methodProtos;
    classDef->staticMethodProtos = staticMethodProtos;

    size_t classIndex = makeConstant(classDef);

    // Emit OP_CLASS to create the class constructor callable
    emitClass(classIndex, static_cast<uint8_t>(methodProtos.size()));

    // Bind to name
    if (scopeDepth == 0) {
        int slot = defineGlobal(stmt.name);
        if (hadError) return;
        emitDefineGlobal(slot);
    } else {
        addLocal(stmt.name);
    }
}

void Compiler::visitBreakStmt(const BreakStmt& stmt) {
    currentLine = stmt.keyword.line;
    currentColumn = stmt.keyword.column;
    if (loopStack.empty()) {
        error("'break' outside of loop");
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
    localsToPop += loopStack.back().extraLocalsToPopOnBreak;

    if (localsToPop > 0) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_POPN),
                  static_cast<uint8_t>(localsToPop));
    }

    // Emit jump to after the loop. Patch later.
    size_t jumpOffset = emitJump(OpCode::OP_JUMP);
    loopStack.back().breakJumps.push_back(jumpOffset);
}

void Compiler::visitContinueStmt(const ContinueStmt& stmt) {
    currentLine = stmt.keyword.line;
    currentColumn = stmt.keyword.column;
    if (loopStack.empty()) {
        error("'continue' outside of loop");
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
    // Pop any loop-infrastructure locals that should not persist through
    // continue (currently 0 for both for-in and C-for; this is explicit so
    // future loop constructs can use it).
    localsToPop += loopStack.back().extraLocalsToPopOnContinue;

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
    // Emit OP_PUSH_CATCH <localCount> <offset16>.
    // localCount tells the VM where to place the exception value on the stack
    // (frameBaseIndex + localCount). This ensures it lands after all existing
    // locals, matching the slot where the catch variable will be allocated.
    emitByte(static_cast<uint8_t>(OpCode::OP_PUSH_CATCH));
    emitByte(static_cast<uint8_t>(currentLocalCount()));
    size_t pushCatchPlaceholder = chunk.code.size();
    emitByte(0xFF);  // offset low placeholder
    emitByte(0xFF);  // offset high placeholder

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
    currentLine = stmt.keyword.line;
    currentColumn = stmt.keyword.column;
    // Execute deferred cleanup before throwing (RAII semantics)
    emitDeferFlush();
    // Compile the value to throw
    stmt.value->accept(*this);
    emitByte(static_cast<uint8_t>(OpCode::OP_THROW));
}

void Compiler::visitImportStmt(const ImportStmt& stmt) {
    currentLine = stmt.keyword.line;
    currentColumn = stmt.keyword.column;

    // --- from "path" import a, b, c ---
    if (!stmt.importNames.empty()) {
        // Emit OP_IMPORT with the path and a dummy name (not used for binding).
        size_t pathIndex = makeConstant(
            GcHeap::instance().alloc<GcString>(stmt.modulePath));
        size_t nameIndex = makeConstant(
            GcHeap::instance().alloc<GcString>(""));
        emitImport(pathIndex, nameIndex);
        // Stack: [moduleDict]

        for (size_t i = 0; i < stmt.importNames.size(); i++) {
            const auto& name = stmt.importNames[i];
            bool isLast = (i == stmt.importNames.size() - 1);

            if (!isLast) {
                // Duplicate the dict so OP_GET_PROPERTY can pop its copy.
                emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
            }
            // OP_GET_PROPERTY pops the dict, looks up `name`, pushes the value.
            size_t propIdx = makeConstant(
                GcHeap::instance().alloc<GcString>(name));
            emitGetProperty(propIdx);
            // Stack: [moduleDict (or gone if last), value]

            // Bind the extracted value.
            if (scopeDepth > 0) {
                addLocal(name, false);
            } else {
                int slot = resolveGlobal(name);
                emitDefineGlobal(slot);
            }
        }
        return;
    }

    // --- import "path" / import "path" as alias ---
    // Derive variable name: use `as` alias if provided, otherwise derive
    // from the module path basename.
    std::string varName;
    if (!stmt.alias.empty()) {
        varName = stmt.alias;
    } else {
        varName = stmt.modulePath;
        auto slashPos = varName.find_last_of("/\\");
        if (slashPos != std::string::npos) {
            varName = varName.substr(slashPos + 1);
        }
        if (varName.size() > 3 && varName.substr(varName.size() - 3) == ".va") {
            varName = varName.substr(0, varName.size() - 3);
        }
        while (!varName.empty() && !std::isalpha(static_cast<unsigned char>(varName[0]))) {
            varName = varName.substr(1);
        }
        if (varName.empty()) {
            varName = stmt.modulePath;
            auto sp = varName.find_last_of("/\\");
            if (sp != std::string::npos) varName = varName.substr(sp + 1);
        }
    }

    // Emit OP_IMPORT with path and variable name as constants
    size_t pathIndex = makeConstant(
        GcHeap::instance().alloc<GcString>(stmt.modulePath));
    size_t nameIndex = makeConstant(
        GcHeap::instance().alloc<GcString>(varName));
    emitImport(pathIndex, nameIndex);

    // OP_IMPORT pushes the module dict onto the stack.
    if (scopeDepth > 0) {
        addLocal(varName, false);
    } else {
        int slot = resolveGlobal(varName);
        emitDefineGlobal(slot);
    }
}

void Compiler::visitExportStmt(const ExportStmt& stmt) {
    // Compile the inner declaration as normal
    stmt.declaration->accept(*this);

    // Track the exported name based on declaration type
    if (auto* fs = dynamic_cast<FuncStmt*>(stmt.declaration.get())) {
        exportNames.push_back(fs->name);
    } else if (auto* ls = dynamic_cast<LetStmt*>(stmt.declaration.get())) {
        // 'const' declarations also use LetStmt (with isConst=true).
        if (!ls->name.empty()) {
            exportNames.push_back(ls->name);
        } else if (ls->binding) {
            // Extract names from destructuring pattern (e.g. export let {x, y} = obj)
            auto names = ls->binding->getBoundNames();
            exportNames.insert(exportNames.end(), names.begin(), names.end());
        }
    } else if (auto* os = dynamic_cast<ObjStmt*>(stmt.declaration.get())) {
        exportNames.push_back(os->name);
    }
}

void Compiler::visitDeferStmt(const DeferStmt& stmt) {
    // Compile the deferred expression as a 0-argument function prototype.
    // Store it so that at return points we emit OP_CLOSURE + OP_CALL for
    // each deferred prototype in LIFO order.

    Compiler fnCompiler(errorReporter_);
    fnCompiler.enclosing = this;
    fnCompiler.chunk.source = chunk.source;

    fnCompiler.beginScope();

    // Compile the expression as the body of a 0-arg function
    stmt.expression->accept(fnCompiler);

    // Implicit return of the expression value
    fnCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

    fnCompiler.endScope();

    // Capture upvalues BEFORE moving from fnCompiler
    auto capturedUpvalues = fnCompiler.upvalues;

    // Build function prototype (0 args, 0 required)
    FunctionPrototype proto;
    proto.name = "<defer>";
    proto.arity = 0;
    proto.requiredArity = 0;
    proto.hasRest = false;
    proto.upvalues = std::move(fnCompiler.upvalues);
    proto.chunk = std::move(fnCompiler.chunk);
    proto.isGenerator = fnCompiler.isGenerator;

    uint8_t protoIndex = addFunctionPrototype(std::move(proto));

    // Emit closure creation immediately (at defer declaration site)
    // instead of at exit points. This captures upvalues at the correct
    // scope and pushes the closure onto the frame's defer stack for
    // cross-function unwind support.
    emitClosure(protoIndex, capturedUpvalues);
    emitByte(static_cast<uint8_t>(OpCode::OP_DEFER_PUSH));
    deferCount_++;
}

} // namespace vora
