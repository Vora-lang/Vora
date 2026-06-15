#pragma once

#include <memory>
#include <string>

namespace vora {

class Expr;

// Parameter declaration shared by FuncStmt, ObjStmt, and FuncExpr.
// Supports optional default value expressions.
struct ParamDecl {
    std::string name;
    std::unique_ptr<Expr> defaultValue;  // nullptr = required param

    ParamDecl(std::string name, std::unique_ptr<Expr> defaultValue = nullptr);
    ~ParamDecl();
    ParamDecl(ParamDecl&&) noexcept = default;
    ParamDecl& operator=(ParamDecl&&) noexcept = default;
};

}
