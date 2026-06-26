#pragma once

#include <memory>
#include <string>

namespace vora {

class Expr;
class BindingPattern;

// Parameter declaration shared by FuncStmt, ObjStmt, and FuncExpr.
// Supports optional default value expressions and destructuring patterns.
struct ParamDecl {
    std::string name;                         // simple name (empty if pattern is set)
    std::unique_ptr<Expr> defaultValue;       // nullptr = required param
    bool isRest = false;                      // true if declared as ...name
    std::unique_ptr<BindingPattern> pattern;  // destructuring pattern (null = simple param)

    ParamDecl(std::string name, std::unique_ptr<Expr> defaultValue = nullptr,
              bool isRest = false);
    ~ParamDecl();
    ParamDecl(ParamDecl&&) noexcept = default;
    ParamDecl& operator=(ParamDecl&&) noexcept = default;
};

}
