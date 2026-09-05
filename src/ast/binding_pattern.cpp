#include "binding_pattern.h"
#include "expr.h"  // complete type for Expr (needed by ~unique_ptr<Expr>)

namespace vora {

// ============================================================================
// IdentifierBinding
// ============================================================================

IdentifierBinding::IdentifierBinding(std::string name, Token nameToken,
                                     std::unique_ptr<Expr> defaultValue)
    : name(std::move(name)),
      nameToken(std::move(nameToken)),
      defaultValue(std::move(defaultValue)) {
    startToken = this->nameToken;
}

std::vector<std::string> IdentifierBinding::getBoundNames() const {
    return {name};
}

// ============================================================================
// ArrayBinding
// ============================================================================

ArrayBinding::ArrayBinding(std::vector<std::unique_ptr<BindingPattern>> elements,
                           std::unique_ptr<BindingPattern> rest,
                           Token leftBracket)
    : elements(std::move(elements)),
      rest(std::move(rest)),
      leftBracket(std::move(leftBracket)) {
    startToken = this->leftBracket;
}

std::vector<std::string> ArrayBinding::getBoundNames() const {
    std::vector<std::string> names;
    for (const auto& elem : elements) {
        auto elemNames = elem->getBoundNames();
        names.insert(names.end(), elemNames.begin(), elemNames.end());
    }
    if (rest) {
        auto restNames = rest->getBoundNames();
        names.insert(names.end(), restNames.begin(), restNames.end());
    }
    return names;
}

// ============================================================================
// ObjectBinding
// ============================================================================

ObjectBinding::ObjectBinding(std::vector<Property> properties,
                             std::unique_ptr<BindingPattern> rest,
                             Token leftBrace)
    : properties(std::move(properties)),
      rest(std::move(rest)),
      leftBrace(std::move(leftBrace)) {
    startToken = this->leftBrace;
}

std::vector<std::string> ObjectBinding::getBoundNames() const {
    std::vector<std::string> names;
    for (const auto& prop : properties) {
        auto propNames = prop.pattern->getBoundNames();
        names.insert(names.end(), propNames.begin(), propNames.end());
    }
    if (rest) {
        auto restNames = rest->getBoundNames();
        names.insert(names.end(), restNames.begin(), restNames.end());
    }
    return names;
}

}
