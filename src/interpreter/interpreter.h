#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../ast/expr.h"
#include "../ast/expr_visitor.h"
#include "../ast/program.h"
#include "../ast/stmt.h"
#include "../ast/stmt_visitor.h"

#include "../runtime/environment.h"
#include "../runtime/native_function.h"
#include "../runtime/runtime_config.h"
#include "../runtime/runtime_error.h"
#include "../runtime/value.h"

namespace vora {

class VoraFunction;

class Interpreter : public ExprVisitor<Value>,
                     public StmtVisitor<void>,
                     public ProgramVisitor<void> {
public:

    explicit Interpreter(
        RuntimeConfig config = {}
    );

    void interpret(
        const Program* program
    );

    Value callFunction(
        const VoraFunction& function,
        const std::vector<Value>& arguments
    );

    Environment& getEnvironment() { return environment; }

    void pushScopeWithEnclosing(Environment* enclosing);

    void popScope();

    void executeBlockWithThis(
        const std::vector<std::unique_ptr<Stmt>>& statements,
        const Value& thisValue,
        const std::vector<std::string>& params,
        const std::vector<Value>& arguments
    );

    // Convenience evaluation entry point (delegates via double dispatch).
    // Made public so BoundMethodCallable and ObjectConstructorCallable can
    // evaluate sub-expressions through the interpreter.
    Value evaluate(const Expr* expr);

    // Convenience execution entry point (delegates via double dispatch).
    void execute(const Stmt* stmt);

    // Execute a block of statements with automatic scope management.
    void executeBlock(const std::vector<std::unique_ptr<Stmt>>& statements);

private:

    RuntimeConfig config;

    std::shared_ptr<Environment> globals;

    Environment environment;

    std::deque<Environment> scopeStack;

    // --- ExprVisitor overrides ---
    Value visitLiteralExpr(const LiteralExpr& expr) override;
    Value visitBinaryExpr(const BinaryExpr& expr) override;
    Value visitGroupingExpr(const GroupingExpr& expr) override;
    Value visitUnaryExpr(const UnaryExpr& expr) override;
    Value visitVariableExpr(const VariableExpr& expr) override;
    Value visitAssignmentExpr(const AssignmentExpr& expr) override;
    Value visitCallExpr(const CallExpr& expr) override;
    Value visitArrayExpr(const ArrayExpr& expr) override;
    Value visitIndexExpr(const IndexExpr& expr) override;
    Value visitPropertyExpr(const PropertyExpr& expr) override;
    Value visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) override;
    Value visitThisExpr(const ThisExpr& expr) override;
    Value visitIncDecExpr(const IncDecExpr& expr) override;
    Value visitTernaryExpr(const TernaryExpr& expr) override;

    // --- StmtVisitor overrides ---
    void visitExprStmt(const ExprStmt& stmt) override;
    void visitLetStmt(const LetStmt& stmt) override;
    void visitBlockStmt(const BlockStmt& stmt) override;
    void visitReturnStmt(const ReturnStmt& stmt) override;
    void visitIfStmt(const IfStmt& stmt) override;
    void visitWhileStmt(const WhileStmt& stmt) override;
    void visitForStmt(const ForStmt& stmt) override;
    void visitFuncStmt(const FuncStmt& stmt) override;
    void visitObjStmt(const ObjStmt& stmt) override;
    void visitBreakStmt(const BreakStmt& stmt) override;
    void visitContinueStmt(const ContinueStmt& stmt) override;
    void visitTryStmt(const TryStmt& stmt) override;
    void visitThrowStmt(const ThrowStmt& stmt) override;

    // --- ProgramVisitor<void> override ---
    void visitProgram(const Program& program) override;

    void pushScope();

    void pushScope(
        Environment* enclosing
    );

    Value invoke(
        const Value& callee,
        const std::vector<Value>& arguments,
        const Token& token
    );

    void defineNative(
        const std::string& name,
        int arity,
        NativeFunction::NativeFn function
    );

    static bool isTruthy(
        const Value& value
    );

    std::shared_ptr<Environment> captureClosure();

    void defineBinding(
        const std::string& name,
        const Value& value
    );

    std::string interpolateString(
        const std::string& str
    );
};

}
