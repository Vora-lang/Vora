#include "param_decl.h"
#include "expr.h"  // complete type for Expr (needed by ~unique_ptr<Expr>)

namespace vora {

ParamDecl::ParamDecl(std::string name, std::unique_ptr<Expr> defaultValue, bool isRest)
    : name(std::move(name)), defaultValue(std::move(defaultValue)), isRest(isRest) {}

ParamDecl::~ParamDecl() = default;

}
