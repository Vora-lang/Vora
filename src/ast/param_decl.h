#pragma once
/**
 * @file param_decl.h
 * @brief Parameter declaration struct shared across function and method AST nodes.
 *
 * ParamDecl represents a single formal parameter in a function signature.
 * It is used by FuncStmt (named function declarations), ObjStmt (class
 * constructor and method declarations), and FuncExpr (anonymous function
 * expressions). It supports optional default values, rest parameters (`...`),
 * and destructuring patterns.
 *
 * Unlike AST nodes, ParamDecl is NOT visited through the
 * ExprVisitor/StmtVisitor system. It is a structural data member embedded
 * directly in its owning node.
 *
 * @see BindingPattern, FuncStmt, ObjStmt, FuncExpr
 */

#include <memory>
#include <string>

namespace vora {

class Expr;
class BindingPattern;

/**
 * @brief A single formal parameter declaration.
 *
 * Each parameter has a simple name (used for non-destructured params),
 * an optional default value expression, an optional rest-parameter flag,
 * and an optional destructuring pattern. When a pattern is present, `name`
 * is empty and the pattern carries the bound variable names.
 *
 * Examples:
 *   - `x`              → name="x", defaultValue=null, isRest=false, pattern=null
 *   - `x = 5`          → name="x", defaultValue=literal(5), isRest=false, pattern=null
 *   - `...args`         → name="args", defaultValue=null, isRest=true, pattern=null
 *   - `{a, b}`          → name="", defaultValue=null, isRest=false,
 *                           pattern=ObjectBinding({a, b})
 */
struct ParamDecl {
    /** @brief Simple parameter name. Empty when a destructuring pattern is used. */
    std::string name;

    /**
     * @brief Default value expression.
     *
     * `nullptr` means the parameter is required (no default). When non-null,
     * the expression is evaluated at call time only if the argument is omitted.
     */
    std::unique_ptr<Expr> defaultValue;

    /**
     * @brief Whether this is a rest parameter (`...name`).
     *
     * A rest parameter collects trailing arguments into an array. Only the
     * last parameter in a signature may be a rest parameter.
     */
    bool isRest = false;

    /**
     * @brief Destructuring pattern, or nullptr for a simple named parameter.
     *
     * When non-null, the argument must be an array or object, and its elements
     * or properties are bound to the names in the pattern tree.
     */
    std::unique_ptr<BindingPattern> pattern;

    /**
     * @brief Construct a parameter declaration.
     * @param name         Simple name (empty for destructured params).
     * @param defaultValue Optional default value expression (nullptr = required).
     * @param isRest       Whether this is a rest parameter (`...name`).
     */
    ParamDecl(std::string name, std::unique_ptr<Expr> defaultValue = nullptr,
              bool isRest = false);
    ~ParamDecl();

    /** @brief Move-constructible (required for std::vector<ParamDecl>). */
    ParamDecl(ParamDecl&&) noexcept = default;
    /** @brief Move-assignable (required for std::vector<ParamDecl>). */
    ParamDecl& operator=(ParamDecl&&) noexcept = default;
};

} // namespace vora
