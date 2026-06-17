# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Description

**Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation.**

This is the canonical description — use it verbatim in all metadata, package manifests, version info, and documentation.

## Cross-repo references

| Repository | Path | Purpose |
|-----------|------|---------|
| **Vora** (this repo) | `D:\Vora` | Language core: lexer, parser, AST, compiler, VM, runtime, stdlib |
| **Vora-LSP** | `D:\Vora-LSP` | LSP server (C++) + VS Code extension (TypeScript) |
| **Vora website** | `D:\Vora-lang.github.io` | Generated user documentation site |
| **Docs** | `D:\Vora\docs` | Design docs, roadmaps, architecture notes |

**When working on LSP-related tasks**: always consult both repos — the LSP server (`D:\Vora-LSP\server\`) depends on the Vora core (`D:\Vora\src\`), and the VS Code extension (`D:\Vora-LSP\src\`) connects to the LSP server binary. The LSP server is built as part of the Vora project (`cmake --build build --target vora-lsp`).

**When adding syntax or new features**: you MUST update all three locations:
1. `USER_GUIDE.md` — user-facing language manual
2. `D:\Vora\docs\` — detailed design docs (if the feature warrants it)
3. `D:\Vora-lang.github.io` — run `scripts/build-website-docs.py` to regenerate the website
Also update `syntaxes/vora.tmLanguage.json` in `D:\Vora-LSP` if the syntax changes (new keywords, operators, literals).

## Build & Run

**优先使用项目自带的 build 脚本**（跨平台一键构建 + 运行，支持多架构和打包）：

### Quick start

```bash
# Windows (PowerShell) — default: x64 Debug
.\build.ps1

# Linux / macOS (bash) — default: x64 debug
./build.sh
```

### Multi-architecture + packaging

```bash
# Windows: specify arch, config, and packaging
.\build.ps1 -Architecture arm64 -Config Release -Package   # → .msi installer
.\build.ps1 -Architecture x86 -Config Debug                 # → .exe (32-bit)

# Linux: short flags
./build.sh -a arm64 -c release -p     # cross-compile ARM64 → .deb/.rpm/.pkg.tar.xz
./build.sh -a x86 -c debug            # 32-bit x86 debug binary
./build.sh -c release -p              # native x64 release + package
```

### Supported targets

| Platform | Architectures | Debug | Release |
|----------|--------------|-------|---------|
| Windows | x64, x86, arm64 | `.exe` | `.msi` (WiX) + `.zip` |
| Linux   | x64, x86, aarch64, armhf | binary | `.deb`, `.rpm`, `.pkg.tar.xz` |
| macOS   | universal (x86_64+arm64) | binary | `.tar.gz` |

### CMake presets (manual build)

```bash
# List all presets
cmake --list-presets

# Configure + build + test for a specific target
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug

# Windows (MSVC multi-config)
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release --config Release

# Package (Release only)
cmake --build --preset linux-x64-release --target package    # → .deb/.rpm
cmake --build --preset windows-x64-release --target package --config Release  # → .msi
```

See `docs/09-构建系统指南.md` for detailed cross-compilation setup, toolchain requirements, and CI architecture.

No external dependencies — pure C++17 + STL. Tests live in `tests/` as `.va` scripts executed via `run_tests.ps1` / `run_tests.sh`, using the `assert()` builtin for validation.

## Mandatory Testing

Before every commit, run **all examples and tests**. Never skip this.

### Windows (PowerShell)

```powershell
.\tests\run_tests.ps1
.\tests\run_examples.ps1
```

### Linux / macOS (bash)

```bash
./tests/run_tests.sh
```

### Current expected results (2026-06-17)

| Suite | Status |
|-------|--------|
| **Tests** (41 files) | 41/41 ✅ (1 known failure: `test_input.va` — stdin piping) |
| **Examples** (39 files) | 39/39 ✅ |
| **C++ Unit Tests** (302 cases) | 302/302 ✅ (939 assertions) |

> `17-type-annotations.va` requires piped input (uses `input()`). Pipe `"42`n3.14`n100`ntest`n"` in PowerShell or `printf '42\n3.14\n100\ntest\n'` in bash.

## Architecture

Vora compiles source to bytecode and executes it on a stack-based VM:

```
Source (.va) → Lexer → Token stream → Parser → AST (Program)
                                                  └─→ Compiler → Chunk → VM (bytecode)
```

The **VM** (bytecode compiler + stack-based virtual machine) is the sole execution backend. It supports Phase 1 (expressions, global variables, control flow), Phase 2 (local variables, break/continue, for-in, functions, try/catch/throw), Phase 3 (objects with inheritance), and Phase 4 (closures, string interpolation, runtime error catch completeness).

### Key design decisions

- **Templated visitor pattern for AST dispatch**: `ExprVisitor<R>` and `StmtVisitor<R>` are templated on return type, so the same interface definition serves all passes. `Expr` and `Stmt` each have one `accept()` overload per return type in use (currently `void`/`std::string` for Expr, `void`/`std::string` for Stmt). `Program` has `ProgramVisitor<R>` and a non-virtual template `accept()` (no subclass dispatch needed). Adding a new pass only requires adding one `accept()` overload to `Expr`/`Stmt` — the visitor interface itself needs no duplication.
- **Pratt parser for expressions**: `parsePrecedence()` handles binary operators with a precedence table. Assignment (`=`) and compound assignment (`+=`, etc.) are desugared inside the Pratt loop.
- **`Value` is `std::variant`**: `std::variant<std::nullptr_t, double, int64_t, bool, std::string, GcPtr<Array>, GcPtr<Dict>, GcPtr<Callable>, GcPtr<ObjectInstance>, GcPtr<FunctionPrototype>, GcPtr<ClassDefinition>>`. No tagged union hand-rolling — use `std::visit` or `std::holds_alternative` to branch on types.
- **`Callable` hierarchy**: Abstract base `Callable` with four concrete subtypes: `NativeFunction` (pure C++ callback), `ClassConstructor` (class instance factory with MRO constructor chain), `BoundMethod` (method bound to an instance, dispatched by VM), and `VoraFunction` (bytecode-compiled Vora function). `VM::callValue()` dispatches via `dynamic_cast` in order: `BoundMethod` → `ClassConstructor` → `NativeFunction` → `VoraFunction`.
- **Unified class representation**: `ClassDefinition` (defined in `compiler.h`) replaces the former `ClassData` (compile-time) + `ObjectClass` (runtime) split. It is stored in the constant pool and modified in-place by `OP_CLASS` — parents are resolved, method `VoraFunction`s are built, and C3 MRO is computed directly on the same object.
- **Scope as linked list**: `Environment` uses `std::shared_ptr<Environment> enclosing_` exclusively — no raw pointer or dual-ownership. Variable lookup walks the chain. Closures deep-copy via `Environment::snapshot()`.
- **VM is a stack machine**: Fixed-size `Value` stack (1024 slots), global variable table (integer-indexed parallel arrays for O(1) access), call frame stack for function calls, catch handler stack for exception handling. Instruction pointer over `uint8_t*` bytecode. Jump offsets use little-endian 16-bit encoding. The `Compiler` implements `ExprVisitor<void>` + `StmtVisitor<void>` + `ProgramVisitor<void>` — compilation is purely side-effectful (emitting bytes into a `Chunk`). Jump back-patching uses a two-pass approach: `emitJump()` reserves placeholder bytes and returns the offset; `patchJump()` fills in the final offset once the target is known. Local variables are tracked per scope with `OP_GET_LOCAL`/`OP_SET_LOCAL`. For-in loops are desugared into while loops with internal `_iter`/`_i`/`_len` locals and a `_vora_len` builtin call (registered via `VM::initGlobals()`). Loop contexts track `extraLocalsToPop` (3 for for-in) so break can clean up generated locals. Functions are compiled to separate `Chunk`s stored in the constant pool as `FunctionPrototype`; `OP_CLOSURE` creates a `VoraFunction` at runtime. Objects use `OP_CLASS` with pre-compiled constructor and method prototypes.

### VM exception handling (try/catch/finally)

- **`OP_PUSH_CATCH <offset>`**: Registers a catch handler on `catchHandlers` stack. The operand is a 16-bit forward offset from the current IP to the catch block entry.
- **`OP_POP_CATCH`**: Removes the topmost catch handler (used on normal exit from try, and at catch block entry for housekeeping). `catchHandlers` is a per-VM stack — handlers from caller and callee interleave.
- **`OP_THROW`**: Calls `throwException()` which finds the innermost handler on `catchHandlers`, sets IP to the handler target (the `OP_POP_CATCH` at try exit), pushes the exception value, and sets `exceptionInFlight = true`. Critically, `throwException()` **does not pop** the handler — `OP_POP_CATCH` at the target does the pop, avoiding nested-try conflicts.
- **`OP_CLEAR_EXCEPTION`**: Emitted at catch block entry to clear `exceptionInFlight`, preventing `OP_FINALLY_END` from re-throwing after the exception was already caught.
- **`OP_FINALLY_END`**: If `exceptionInFlight` is true, pops the exception value and calls `throwException()` again to propagate to the next outer handler. Otherwise (normal flow) it's a no-op.
- **Non-local exit through finally**: When `break`/`continue`/`return` appear inside `try/finally`, the compiler captures these jumps (from `loopStack` break/continue lists and `pendingReturnJumps`), routes them through the finally block by replaying the recorded finally bytecode, then jumps to the original target. Catch handler cleanup (`OP_POP_CATCH` for each `tryNesting` level) is emitted before break/continue/return jumps. `visitTryStmt` manages a `finallyBytecodeStack` for nested finally blocks and a `finallyNesting` counter for return routing.

### Source layout

| Directory | Purpose |
|-----------|---------|
| `include/` | **Third-party dependencies** (header-only libraries, vendored sources). Currently: nlohmann/json (header-only JSON). All third-party code lives here — never mix with `src/`. |
| `src/lexer/` | Lexer: source → tokens. `token.h` defines `TokenType` enum and `Token` struct. |
| `src/ast/` | AST nodes: `expr.h` (15 expression types including `ErrorExpr`), `stmt.h` (17 statement types including `ErrorStmt`), `program.h` (root + `ProgramVisitor<R>`). `expr_visitor.h` / `stmt_visitor.h` define templated visitor interfaces. `ast_printer.h/.cpp` for S-expression debug output (implements `ExprVisitor<std::string>` + `StmtVisitor<std::string>` + `ProgramVisitor<std::string>`, no side-channel `result_` member). |
| `src/parser/` | Recursive-descent + Pratt parser with **error-tolerant parsing**. `statement()` dispatches by keyword; `expression()` enters Pratt via `parsePrecedence(0)`. On error, returns `ErrorExpr`/`ErrorStmt` (never `nullptr`). `parse()` always returns a Program — callers check `hasError()`. `synchronize()` skips to next statement boundary. |
| `src/vm/` | Bytecode VM. `opcode.h` (40+ instructions), `chunk.h/.cpp` (bytecode + constant pool + RLE line/column + disassembler), `compiler.h` (contains `FunctionPrototype` and `ClassDefinition` structs), `compiler.cpp` + `compiler_expr.cpp` + `compiler_stmt.cpp` (AST → bytecode, implements `ExprVisitor<void>` + `StmtVisitor<void>` + `ProgramVisitor<void>`), `vm.h/.cpp` (stack machine execution loop, call frames, catch handlers, value operations). |
| `src/runtime/` | `value.h` (Value variant + `Array`/`Dict`/`ObjectInstance` structs), `callable.h` (abstract base), `native_function.h/.cpp` (pure native callbacks), `class_constructor.h/.cpp` (class instance factories), `bound_method.h/.cpp` (instance-bound methods), `vora_function.h/.cpp` (bytecode functions), `environment.h/.cpp` (scope chain, shared_ptr-only ownership), `runtime_error.h/.cpp`. |
| `src/common/` | `error_reporter.h/.cpp` — abstract `ErrorReporter` interface + `StderrErrorReporter`. Used by lexer, parser, compiler, and LSP server (via `DiagnosticCollector`). |
| `src/json_rpc/` | JSON-RPC infrastructure: `json_rpc.h/.cpp` (message types + parse/serialize), `transport.h/.cpp` (stdio Content-Length framing), `message_router.h/.cpp` (handler registration + dispatch). |
| `src/formatter/` | `SourceFormatter` — AST-based source code formatter (`vora fmt`). |
| `examples/` | `.va` example files covering language features. |
| `docs/` | Project documentation in Chinese. `00-roadmap.md` is the roadmap; `08-已实现功能总结.md` summarises implemented features. |
| `std/` | Standard library modules loaded via `import`. |

### Adding a language feature (typical workflow)

1. **Lexer** (`token.h` + `lexer.cpp`): Add `TokenType` enum value(s); update `tokenTypeToString()` in `token.cpp`; add scanning logic in `lexer.cpp` `scanToken()`.
2. **AST** (`expr.h` or `stmt.h`): Create new node class inheriting `Expr` or `Stmt`; declare all `accept()` overloads (`void`/`std::string` for Expr, `void`/`std::string` for Stmt) and `clone()`.
3. **Visitor interface** (`expr_visitor.h` / `stmt_visitor.h`): Add pure virtual `visit*` method to the `ExprVisitor<R>` or `StmtVisitor<R>` template. The template applies to all `R` automatically — no per-return-type duplication.
4. **AST dispatch** (`expr.cpp` / `stmt.cpp`): Implement all `accept()` overloads (each is a one-line delegation to `visitor.visit*Node(*this)`) and `clone()` (deep copy).
5. **Parser** (`parser.cpp`): Add parsing logic — for expressions, extend `primary()` or `call()` or the Pratt loop; for statements, add a `*Statement()` method and dispatch it in `statement()`.
6. **AST Printer** (`ast_printer.h` + `ast_printer.cpp`): Override `visit*` returning `std::string` directly (inherits `ExprVisitor<std::string>`, `StmtVisitor<std::string>`, `ProgramVisitor<std::string>`).
7. **VM Compiler** (`vm/compiler.h` + `vm/compiler.cpp`): Override `visit*` emitting bytecode (inherits `ExprVisitor<void>` + `StmtVisitor<void>` + `ProgramVisitor<void>`). Use `emitByte()`/`emitJump()`/`patchJump()` for code generation. For loops, push/pop `LoopContext` on `loopStack` for break/continue back-patching. For functions, create a nested `Compiler` instance and store the result as `FunctionPrototype` in the constant pool. For objects, compile methods and constructor separately and wrap in `ClassDefinition`. For try/catch, use `emitJump(OP_PUSH_CATCH)` and manually patch to the catch block's offset.
8. **VM runtime** (if needed): Add new `OpCode` value(s) to `vm/opcode.h` and the corresponding `case` in `vm/vm.cpp` `run()`.
9. **User guide + website + LSP** (`USER_GUIDE.md` + `D:\Vora-lang.github.io` + `D:\Vora-LSP`): Update the relevant section with the new feature — add version tag, code examples, and brief explanation. If the feature adds/changes a built-in function or standard library module, also update the corresponding section (内建函数 / 标准库). Run `scripts/build-website-docs.py` to regenerate the website language reference. If the feature introduces new keywords, operators, or syntax, update `syntaxes/vora.tmLanguage.json` in `D:\Vora-LSP` so syntax highlighting stays correct. **Never skip this step** — the user guide is the single source of truth for user-facing documentation.

### C++ patterns in use

- All AST nodes use `std::unique_ptr` for child ownership.
- `std::shared_ptr` for runtime objects that need shared lifetime (closures, arrays, object instances, callables). `blockStatement()` returns `std::unique_ptr<BlockStmt>` (the concrete type, not `Stmt`), so callers that need a `shared_ptr<BlockStmt>` can move-construct it directly — no `dynamic_cast` or `release()` hack.
- `dynamic_cast` is used in the Pratt loop to distinguish `VariableExpr` from `PropertyExpr` for assignment targets, and in `objStatement()` to separate `FuncStmt` nodes (→ methods) from other statements (→ constructor body).
- Templates over runtime polymorphism for visitors: `ExprVisitor<R>`, `StmtVisitor<R>`, `ProgramVisitor<R>`. A new pass with a new return type only needs a new `accept()` overload in `Expr`/`Stmt` + all subclasses (mechanical one-liners). The visitor interface itself is generic.
- The parser uses **error-tolerant parsing**: `parse()` always returns a `Program` (never `nullptr`). On error, `ErrorExpr`/`ErrorStmt` placeholder nodes are inserted into the AST. Callers check `Parser::hasError()` to decide whether to execute. `ErrorExpr` compiles to `OP_NULL`; `ErrorStmt` compiles to a no-op. This ensures partial ASTs are available for LSP diagnostics/completion/formatting. **Statement-level recovery**: if/while/for/func/obj/try/let/const all return partial nodes (e.g., IfStmt with ErrorExpr condition) instead of bare ErrorStmt when parts are missing. **Expression-level recovery**: missing operands produce ErrorExpr placeholders rather than discarding surrounding valid sub-expressions; invalid assignment targets return the RHS value. **`synchronize()`** tracks brace/paren/bracket depth to correctly skip to the matching close-delimiter.
- **VM jump back-patching**: `emitJump(OpCode)` emits the instruction + two placeholder bytes (0xFF 0xFF) and returns the offset of the first placeholder. `patchJump(offset)` computes the forward offset from that position to the current code end and writes it in little-endian. `emitLoop(loopStart)` emits OP_LOOP + a backward offset. This avoids a separate AST→IR pass.
- **VM native function calling**: `NativeFunction::call()` takes `const std::vector<Value>&` directly — no external dependencies needed by the VM's `OP_CALL` handler.

### Portability: MSVC vs GCC/Clang includes

MSVC implicitly includes certain standard headers via `<iostream>` and other transitive paths. GCC and Clang do **not**. When adding code that uses:

| Symbol | Required header |
|--------|----------------|
| `std::fprintf`, `std::printf`, `std::snprintf` | `<cstdio>` |
| `std::map` | `<map>` |
| `std::unordered_map` | `<unordered_map>` |
| `std::function` | `<functional>` |
| `std::find_if`, `std::sort`, etc. | `<algorithm>` |
| `assert()` | `<cassert>` |

CI runs Linux (GCC), macOS (Apple Clang), and sanitizer (Clang) builds — all are strict about explicit includes. A build that passes MSVC locally may fail on CI. Always add the explicit `#include` for every standard library facility used in a translation unit.
