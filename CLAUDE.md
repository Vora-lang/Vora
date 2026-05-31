# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```powershell
# One-shot build + run (Windows PowerShell)
.\build.ps1

# Manual CMake build
cmake -S . -B build
cmake --build build

# Run a script
./build/Debug/Vora.exe examples/main.va

# REPL mode
./build/Debug/Vora.exe --repl

# Debug: print AST as S-expression
./build/Debug/Vora.exe examples/main.va --ast-printer

# Debug: print token stream
./build/Debug/Vora.exe examples/main.va --tokens
```

No external dependencies — pure C++17 + STL. No test framework is set up yet (`tests/` is empty).

## Architecture

Vora is a tree-walking AST interpreter for a custom language (`.va` files). The pipeline is:

```
Source (.va) → Lexer → Token stream → Parser → AST (Program) → Interpreter → output
```

### Key design decisions

- **Visitor pattern for AST dispatch**: `Expr` and `Stmt` each have an `accept()` method. `Interpreter` and `ASTPrinter` both implement `ExprVisitor` + `StmtVisitor` via double dispatch. Adding a new expression or statement node requires touching: the node class in `expr.h`/`stmt.h`, the visitor interface, `expr.cpp`/`stmt.cpp` (accept + clone), the interpreter, and the AST printer.
- **Pratt parser for expressions**: `parsePrecedence()` handles binary operators with a precedence table. Assignment (`=`) and compound assignment (`+=`, etc.) are desugared inside the Pratt loop.
- **`Value` is `std::variant`**: `std::variant<std::nullptr_t, double, bool, std::string, shared_ptr<Array>, shared_ptr<Callable>, shared_ptr<ObjectInstance>>`. No tagged union hand-rolling — use `std::visit` or `std::holds_alternative` to branch on types.
- **Scope as linked list**: `Environment` has a parent pointer (`enclosing`). Variable lookup walks the chain. Closures snapshot the chain via `Environment::snapshot()`.
- **Non-local control flow via C++ exceptions**: `ReturnSignal`, `BreakSignal`, `ContinueSignal` are thrown from interpreter visit methods and caught at the appropriate boundary (function body or loop).

### Source layout

| Directory | Purpose |
|-----------|---------|
| `src/lexer/` | Lexer: source → tokens. `token.h` defines `TokenType` enum and `Token` struct. |
| `src/ast/` | AST nodes: `expr.h` (13 expression types), `stmt.h` (11 statement types), `program.h` (root). `ast_printer.h/.cpp` for S-expression debug output. |
| `src/parser/` | Recursive-descent + Pratt parser. `statement()` dispatches by keyword; `expression()` enters Pratt via `parsePrecedence(0)`. |
| `src/interpreter/` | Tree-walking interpreter. Implements both `ExprVisitor` and `StmtVisitor`. |
| `src/runtime/` | `value.h` (Value variant + Array/ObjectInstance structs), `environment.h/.cpp` (scope chain), `callable.h` (abstract base), `native_function.h/.cpp`, `vora_function.h/.cpp`, `runtime_error.h/.cpp`, `runtime_config.h`. |
| `examples/` | `.va` example files covering language features. |
| `docs/` | Project documentation in Chinese. `08-已实现功能总结.md` is the most useful reference for implemented features. |
| `std/` | Placeholder for future standard library. |

### Adding a language feature (typical workflow)

1. **Lexer** (`token.h` + `lexer.cpp`): Add `TokenType` enum value(s); update `tokenTypeToString()` in `token.cpp`; add scanning logic in `lexer.cpp` `scanToken()`.
2. **AST** (`expr.h` or `stmt.h`): Create new node class inheriting `Expr` or `Stmt` with `accept()` and `clone()`.
3. **Visitor interface** (`expr_visitor.h` / `stmt_visitor.h`): Add pure virtual `visit*` method.
4. **AST dispatch** (`expr.cpp` / `stmt.cpp`): Implement `accept()` (delegates to visitor) and `clone()` (deep copy).
5. **Parser** (`parser.cpp`): Add parsing logic — for expressions, extend `primary()` or `call()` or the Pratt loop; for statements, add a `*Statement()` method and dispatch it in `statement()`.
6. **Interpreter** (`interpreter.h` + `interpreter.cpp`): Override the new `visit*` method.
7. **AST Printer** (`ast_printer.h` + `ast_printer.cpp`): Override for debug output.

### C++ patterns in use

- All AST nodes use `std::unique_ptr` for child ownership.
- `std::shared_ptr` for runtime objects that need shared lifetime (closures, arrays, object instances, callables).
- `dynamic_cast` is used in the Pratt loop to distinguish `VariableExpr` from `PropertyExpr` for assignment targets, and in the AST printer for statement dispatch.
- The parser returns `nullptr` on error; the caller checks `hadError` or skips null expression-statements.
