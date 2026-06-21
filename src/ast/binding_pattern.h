#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>

#include "../lexer/token.h"

namespace vora {

class Expr;

// BindingKind distinguishes the three destructuring pattern types.
enum class BindingKind : uint8_t {
    Identifier,   // x
    Array,        // [a, b, ...rest]
    Object        // {x, y: renamed, ...rest}
};

// BindingPattern — abstract base for destructuring patterns.
//
// This hierarchy is separate from Expr/Stmt (just like ParamDecl).
// Patterns are structural data used by LetStmt, ForStmt, and
// DestructureAssignmentExpr. They are NOT visited through the
// ExprVisitor/StmtVisitor system.
class BindingPattern {
public:
    virtual ~BindingPattern() = default;
    virtual BindingKind kind() const = 0;

    // Returns all variable names bound by this pattern (DFS, left-to-right).
    virtual std::vector<std::string> getBoundNames() const = 0;

    // Returns the simple name for IdentifierBinding, "" for others.
    virtual std::string getSimpleName() const { return ""; }

    // Token at the start of the pattern (for position reporting).
    Token startToken;
};

// --- IdentifierBinding: `x` in `let x = 5` or a leaf in a pattern ---
class IdentifierBinding : public BindingPattern {
public:
    IdentifierBinding(std::string name, Token nameToken,
                      std::unique_ptr<Expr> defaultValue = nullptr);

    BindingKind kind() const override { return BindingKind::Identifier; }
    std::vector<std::string> getBoundNames() const override;
    std::string getSimpleName() const override { return name; }

    std::string name;
    Token nameToken;
    std::unique_ptr<Expr> defaultValue;  // nullptr = required
};

// --- ArrayBinding: `[a, b, ...rest]` ---
class ArrayBinding : public BindingPattern {
public:
    ArrayBinding(std::vector<std::unique_ptr<BindingPattern>> elements,
                 std::unique_ptr<BindingPattern> rest,
                 Token leftBracket);

    BindingKind kind() const override { return BindingKind::Array; }
    std::vector<std::string> getBoundNames() const override;

    std::vector<std::unique_ptr<BindingPattern>> elements;
    std::unique_ptr<BindingPattern> rest;   // nullptr = no rest element
    Token leftBracket;
};

// --- ObjectBinding: `{x, y: renamed, ...rest}` ---
class ObjectBinding : public BindingPattern {
public:
    struct Property {
        std::string key;                        // key in the source object
        std::unique_ptr<BindingPattern> pattern; // sub-pattern (or IdentifierBinding)
        bool isShorthand;                       // true if `{x}` rather than `{x: y}`
    };

    ObjectBinding(std::vector<Property> properties,
                  std::unique_ptr<BindingPattern> rest,
                  Token leftBrace);

    BindingKind kind() const override { return BindingKind::Object; }
    std::vector<std::string> getBoundNames() const override;

    std::vector<Property> properties;
    std::unique_ptr<BindingPattern> rest;   // nullptr = no rest element
    Token leftBrace;
};

}
