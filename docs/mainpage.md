# Vora Language — API Reference {#mainpage}

**Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation.**

## Architecture

```
Source (.va) → Lexer → Token stream → Parser → AST (Program)
                                                └─→ Compiler → Chunk → VM (bytecode)
```

## Key Modules

| Module | Directory | Description |
|--------|----------|-------------|
| **Lexer** | `src/lexer/` | Tokenizes source into a stream of 47 token types |
| **AST** | `src/ast/` | Abstract syntax tree — 15 expression types, 17 statement types |
| **Parser** | `src/parser/` | Recursive-descent + Pratt parser with error-tolerant parsing |
| **Compiler** | `src/vm/compiler.h` | AST → bytecode emission into a `Chunk` |
| **VM** | `src/vm/vm.h` | Stack-based bytecode virtual machine |
| **Runtime** | `src/runtime/` | Value types, callables, environments, builtins |
| **GC** | `src/gc/` | Mark-sweep garbage collector |
| **Formatter** | `src/formatter/` | AST-driven source code formatter |
| **JSON-RPC** | `src/json_rpc/` | JSON-RPC 2.0 transport (shared with LSP server) |
| **LSP** | `src/lsp/` | Semantic analyzer for the LSP server |

## Key Classes

| Class | Description |
|-------|-------------|
| `vora::Lexer` | Tokenizes Vora source code into a token stream |
| `vora::Parser` | Parses tokens into an AST (error-tolerant) |
| `vora::Compiler` | Compiles AST to bytecode in a `Chunk` |
| `vora::VM` | Stack-based virtual machine that executes bytecode |
| `vora::Value` | Universal runtime value type (NaN-boxed, 8 bytes) |
| `vora::GcHeap` | Mark-sweep garbage collector |
| `vora::Chunk` | Bytecode container with constant pool and RLE line/column info |

## Namespaces

| Namespace | Description |
|-----------|-------------|
| `vora` | Core language: lexer, parser, AST, compiler, VM, runtime |
| `vora::lsp` | LSP infrastructure: JSON-RPC, message routing, transport |

## Embedding API

Vora can be embedded in C++ applications via the single-header API:

```cpp
#include "vora.hpp"  // or vora.h

vora::VM vm;
vm.initGlobals({});
auto result = vm.interpret(compiledChunk);
vm.setGlobal("myVar", vora::Value(42.0));
```

## Version

This documentation covers Vora v0.27.0 (NaN-boxing, superinstructions, DAP debugger).

## License

MIT License — see the [GitHub repository](https://github.com/Vora-lang/Vora) for details.
