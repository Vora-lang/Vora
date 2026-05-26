#include "interpreter.h"

#include <cmath>
#include <iostream>

namespace vora {

    void Interpreter::interpret(
        const Program* program
    ) {

        for (const auto& stmt :
            program->statements) {

            execute(stmt.get());
        }
    }

    Value Interpreter::evaluate(
        const Expr* expr
    ) {

        // LiteralExpr

        if (auto literal =
            dynamic_cast<
            const LiteralExpr*
            >(expr)) {

            return literal->value;
        }

        // GroupingExpr

        if (auto grouping =
            dynamic_cast<
            const GroupingExpr*
            >(expr)) {

            return evaluate(
                grouping->expression.get()
            );
        }

        // UnaryExpr

        if (auto unary =
            dynamic_cast<
            const UnaryExpr*
            >(expr)) {

            Value right =
                evaluate(
                    unary->right.get()
                );

            // Number unary

            if (
                std::holds_alternative<double>(right)
                ) {

                double value =
                    std::get<double>(right);

                switch (unary->op.type) {

                case TokenType::MINUS:
                    return -value;

                default:
                    break;
                }
            }

            // Boolean NOT

            if (
                unary->op.type ==
                TokenType::NOT
                ) {

                bool truthy = true;

                if (
                    std::holds_alternative<std::nullptr_t>(right)
                    ) {

                    truthy = false;

                }
                else if (
                    std::holds_alternative<bool>(right)
                    ) {

                    truthy =
                        std::get<bool>(right);
                }

                return !truthy;
            }

            std::cerr
                << "Invalid unary operand"
                << std::endl;

            return nullptr;
        }

        // BinaryExpr

        if (auto binary =
            dynamic_cast<
            const BinaryExpr*
            >(expr)) {

            Value left =
                evaluate(
                    binary->left.get()
                );

            Value right =
                evaluate(
                    binary->right.get()
                );

            // Number operations

            if (
                std::holds_alternative<double>(left)
                &&
                std::holds_alternative<double>(right)
                ) {

                double l =
                    std::get<double>(left);

                double r =
                    std::get<double>(right);

                switch (binary->op.type) {

                case TokenType::PLUS:
                    return l + r;

                case TokenType::MINUS:
                    return l - r;

                case TokenType::MULTIPLY:
                    return l * r;

                case TokenType::DIVIDE:

                    if (r == 0) {

                        std::cerr
                            << "Runtime Error: Division by zero"
                            << std::endl;

                        return nullptr;
                    }

                    return l / r;

                case TokenType::POWER:
                    return std::pow(l, r);

                case TokenType::LESS:
                    return l < r;

                case TokenType::LESS_EQUAL:
                    return l <= r;

                case TokenType::GREATER:
                    return l > r;

                case TokenType::GREATER_EQUAL:
                    return l >= r;

                case TokenType::EQUAL_EQUAL:
                    return l == r;

                case TokenType::NOT_EQUAL:
                    return l != r;

                default:
                    break;
                }
            }

            std::cerr
                << "Invalid binary operands"
                << std::endl;

            return nullptr;
        }

        std::cerr
            << "Unknown expression"
            << std::endl;

        return nullptr;
    }

    void Interpreter::execute(
        const Stmt* stmt
    ) {

        // ExprStmt

        if (auto exprStmt =
            dynamic_cast<
            const ExprStmt*
            >(stmt)) {

            Value value =
                evaluate(
                    exprStmt->expression.get()
                );

            printValue(value);

            std::cout
                << std::endl;

            return;
        }
    }

}