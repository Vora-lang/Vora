/// @file program.h
/// @brief Root AST node (Program) and templated visitor interface for
///        the entire parsed source.
///
/// The Program is the top-level node produced by the parser. It holds a
/// list of statements in source order and dispatches visitors via the
/// templated ProgramVisitor<R> interface.
///
/// Because Program has no subclasses, accept() is a non-virtual template
/// that instantiates correctly for any return type R the caller uses.
///
/// @see vora::Stmt
/// @see vora::Expr

#pragma once

#include <memory>
#include <vector>

#include "stmt.h"

namespace vora {

/// @brief Visitor interface for traversing a Program node.
///
/// ProgramVisitor<R> is templated on the return type R so that a single
/// interface definition serves all passes (e.g. void for side-effectful
/// compilation, std::string for pretty-printing).
///
/// @tparam R  The return type of the visit method. Common values:
///            - @c void (compiler, semantic analysis)
///            - @c std::string (AST printer)
template <typename R>
class ProgramVisitor {
public:
    /// Convenience typedef so implementors can reference the return type
    /// as ProgramVisitor<R>::ReturnType.
    using ReturnType = R;

    virtual ~ProgramVisitor() = default;

    /// Visit the root Program node.
    /// @param program  The program to visit (const reference).
    /// @return  A value of type R produced by the visit.
    virtual R visitProgram(const class Program& program) = 0;
};

/// @brief Root AST node representing an entire parsed source file.
///
/// Produced by Parser::parse(). Owns a sequence of top-level statements
/// in source order. Always present even when parsing fails — error
/// recovery inserts ErrorStmt placeholder nodes so that downstream
/// passes (LSP diagnostics, formatting) always have a well-formed AST
/// to inspect.
class Program {
public:
    /// Construct a Program from a vector of top-level statements.
    /// @param statements  The statement list, transferred by move.
    explicit Program(
        std::vector<std::unique_ptr<Stmt>> statements
    )
        : statements(std::move(statements)) {
    }

    /// Dispatch a visitor to this Program.
    ///
    /// Templated because Program has no subclasses, so a virtual accept()
    /// is unnecessary — the template instantiates correctly for any R the
    /// caller uses.
    ///
    /// @tparam R  Return type of the visitor (must match the visitor's R).
    /// @param visitor  The visitor to dispatch.
    /// @return  The value produced by visitor.visitProgram(*this).
    template <typename R>
    R accept(ProgramVisitor<R>& visitor) const {
        return visitor.visitProgram(*this);
    }

    /// Top-level statements in source order. Owned by the Program.
    std::vector<std::unique_ptr<Stmt>> statements;
};

}
