#pragma once
/**
 * @file binding_pattern.h
 * @brief Destructuring binding patterns for variable declarations and assignments.
 *
 * Defines the BindingPattern hierarchy used by `let`/`const` declarations,
 * for-in loop bindings, and destructuring assignment expressions. Patterns
 * describe the left-hand side of a destructuring operation and enumerate the
 * variable names being introduced.
 *
 * The hierarchy is separate from Expr/Stmt (just like ParamDecl). Patterns
 * are structural data embedded in their owning AST nodes and are NOT visited
 * through the ExprVisitor/StmtVisitor system. Instead, consumers walk the
 * pattern tree directly via `kind()` and `getBoundNames()`.
 *
 * Supported forms:
 *   - Identifier:  `x`                    → IdentifierBinding
 *   - Array:       `[a, b, ...rest]`      → ArrayBinding
 *   - Object:      `{x, y: renamed, ...r}` → ObjectBinding
 *
 * @see ParamDecl, LetStmt, ForStmt, DestructureAssignmentExpr
 */

#include <memory>
#include <string>
#include <vector>
#include <optional>

#include "../lexer/token.h"

namespace vora {

class Expr;

/**
 * @brief Discriminator tag for BindingPattern subtypes.
 *
 * Used with BindingPattern::kind() to avoid `dynamic_cast` when branching
 * on pattern shape. This is an intentional design choice — patterns are
 * structural data, not polymorphic behavior nodes.
 */
enum class BindingKind : uint8_t {
    Identifier,   /**< Simple name binding: `x` */
    Array,        /**< Array destructuring: `[a, b, ...rest]` */
    Object        /**< Object destructuring: `{x, y: renamed, ...rest}` */
};

/**
 * @brief Abstract base class for destructuring binding patterns.
 *
 * A pattern is a tree that describes how to decompose a runtime value into
 * named bindings. For example, `let [a, {x, y}] = expr` produces:
 *
 *   ArrayBinding
 *     IdentifierBinding("a")
 *     ObjectBinding
 *       IdentifierBinding("x")
 *       IdentifierBinding("y")
 *
 * Patterns are NOT AST nodes — they are owned by LetStmt, ForStmt, and
 * DestructureAssignmentExpr, and are consumed during compilation (not via
 * the visitor system).
 */
class BindingPattern {
public:
    virtual ~BindingPattern() = default;

    /**
     * @brief Return the discriminator tag for this pattern.
     * @return The BindingKind enum value for the concrete subclass.
     */
    virtual BindingKind kind() const = 0;

    /**
     * @brief Collect all variable names introduced by this pattern.
     *
     * Performs a depth-first, left-to-right traversal. For `[a, {x, y}]`,
     * returns `{"a", "x", "y"}`. Used by the compiler to declare locals and
     * by semantic analysis to detect shadowed/unused variables.
     *
     * @return Ordered vector of bound variable names.
     */
    virtual std::vector<std::string> getBoundNames() const = 0;

    /**
     * @brief Return the simple name for an IdentifierBinding, or "" otherwise.
     *
     * Convenience for callers that only care about the trivial single-name
     * case without walking the pattern tree.
     */
    virtual std::string getSimpleName() const { return ""; }

    /** @brief Token marking the start of this pattern (for error/diagnostic position reporting). */
    Token startToken;
};

/**
 * @brief A simple named binding: `x` in `let x = 5`, or a leaf inside a compound pattern.
 *
 * May carry an optional default value expression for destructuring contexts
 * where the source value may be absent (e.g., `let [a = 0] = arr`).
 */
class IdentifierBinding : public BindingPattern {
public:
    /**
     * @brief Construct an identifier binding.
     * @param name         The variable name being bound.
     * @param nameToken    The token for the name (for position reporting).
     * @param defaultValue Optional default value (nullptr = required).
     */
    IdentifierBinding(std::string name, Token nameToken,
                      std::unique_ptr<Expr> defaultValue = nullptr);

    BindingKind kind() const override { return BindingKind::Identifier; }
    std::vector<std::string> getBoundNames() const override;
    std::string getSimpleName() const override { return name; }

    /** @brief The variable name being bound. */
    std::string name;
    /** @brief The token for the name (carries source position). */
    Token nameToken;
    /**
     * @brief Default value expression, or nullptr if the binding is required.
     *
     * Used in destructuring contexts: `let [a = 0] = arr` gives
     * IdentifierBinding("a") with defaultValue = literal(0).
     */
    std::unique_ptr<Expr> defaultValue;
};

/**
 * @brief An array destructuring pattern: `[a, b, ...rest]`.
 *
 * Elements are matched positionally against the source array. A trailing
 * `...rest` element (if present) collects remaining items into a new array.
 * Individual elements may be holes (nullptr) to skip values, or nested
 * patterns for recursive destructuring.
 */
class ArrayBinding : public BindingPattern {
public:
    /**
     * @brief Construct an array binding pattern.
     * @param elements    Positional sub-patterns (nullptr = hole/skip).
     * @param rest        Rest-element pattern, or nullptr.
     * @param leftBracket Token for the opening `[` (position reporting).
     */
    ArrayBinding(std::vector<std::unique_ptr<BindingPattern>> elements,
                 std::unique_ptr<BindingPattern> rest,
                 Token leftBracket);

    BindingKind kind() const override { return BindingKind::Array; }
    std::vector<std::string> getBoundNames() const override;

    /** @brief Positional sub-patterns. nullptr entries indicate skipped elements. */
    std::vector<std::unique_ptr<BindingPattern>> elements;
    /** @brief Rest-element pattern (`...rest`), or nullptr if no rest element. */
    std::unique_ptr<BindingPattern> rest;
    /** @brief Token for the opening `[` bracket (source position). */
    Token leftBracket;
};

/**
 * @brief An object destructuring pattern: `{x, y: renamed, ...rest}`.
 *
 * Properties are matched by key against the source object. `isShorthand`
 * distinguishes `{x}` (key "x", bind to "x") from `{x: y}` (key "x", bind
 * to "y"). A trailing `...rest` collects remaining properties into a new
 * object.
 */
class ObjectBinding : public BindingPattern {
public:
    /**
     * @brief A single property in an object destructuring pattern.
     */
    struct Property {
        /** @brief The key name in the source object to match against. */
        std::string key;
        /** @brief Sub-pattern to bind the matched value to (usually an IdentifierBinding). */
        std::unique_ptr<BindingPattern> pattern;
        /**
         * @brief Whether this is shorthand syntax.
         *
         * `{x}` (shorthand) means key="x", bind to "x".
         * `{x: y}` (non-shorthand) means key="x", bind to "y".
         */
        bool isShorthand;
    };

    /**
     * @brief Construct an object binding pattern.
     * @param properties Property bindings (key → pattern).
     * @param rest       Rest-element pattern for remaining properties, or nullptr.
     * @param leftBrace  Token for the opening `{` (position reporting).
     */
    ObjectBinding(std::vector<Property> properties,
                  std::unique_ptr<BindingPattern> rest,
                  Token leftBrace);

    BindingKind kind() const override { return BindingKind::Object; }
    std::vector<std::string> getBoundNames() const override;

    /** @brief Named property bindings (key → sub-pattern). */
    std::vector<Property> properties;
    /** @brief Rest-element pattern for remaining properties, or nullptr. */
    std::unique_ptr<BindingPattern> rest;
    /** @brief Token for the opening `{` brace (source position). */
    Token leftBrace;
};

} // namespace vora
