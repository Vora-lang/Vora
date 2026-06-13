<div align="center">

# <img src="res/vora.ico" width="40"> Vora

**A dynamically typed scripting language with JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation.**

[![CI](https://github.com/Vora-lang/Vora/actions/workflows/ci.yml/badge.svg)](https://github.com/Vora-lang/Vora/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Version](https://img.shields.io/badge/version-0.15-orange.svg)](#changelog)

</div>

---

```vora
func greet(name) {
    return "Hello, " + name + "!"
}

let names = ["World", "Vora", "你"]
for name in names {
    print(greet(name))
}
```

~10,500 lines of C++17. Zero external dependencies. Two execution backends: a **bytecode VM** (default) and an **AST tree-walking interpreter** (reference implementation). Cross-platform with native packaging.

---

## Table of Contents

- [Why Vora?](#why-vora)
- [Quick Start](#quick-start)
- [Language Features](#language-features)
  - [Types](#types)
  - [Operators](#operators)
  - [Variables & Scope](#variables--scope)
  - [Control Flow](#control-flow)
  - [Functions & Closures](#functions--closures)
  - [Objects & Inheritance](#objects--inheritance)
  - [Exception Handling](#exception-handling)
  - [String Interpolation](#string-interpolation)
- [Built-in Functions](#built-in-functions)
- [Array Methods](#array-methods)
- [String Methods](#string-methods)
- [Project Architecture](#project-architecture)
- [Platform Support](#platform-support)
- [Testing](#testing)
- [Roadmap](#roadmap)
- [Documentation](#documentation)
- [Acknowledgments](#acknowledgments)
- [License](#license)

---

## Why Vora?

| | JavaScript | Lua | Wren | **Vora** |
|---|:---:|:---:|:---:|:---:|
| C++ embeddable | | | ✔ | **✔** |
| Object-oriented | ✔ | ✔ | ✔ | **✔** |
| Try/catch/finally | ✔ | | | **✔** |
| Closures | ✔ | ✔ | ✔ | **✔** |
| Multiple inheritance | | ✔ | | **✔** |
| Zero dependencies | | ✔ | ✔ | **✔** |
| Native cross-platform builds | | | | **✔** |

Vora occupies a practical middle ground: readable syntax for scripting, fast execution via bytecode VM, and a tiny C++ runtime that embeds into any host application.

---

## Quick Start

### Install from Release

| Platform | Format | Install |
|----------|--------|---------|
| Windows | `.msi` | Double-click to install (auto-registers PATH) |
| Linux | `.deb` | `sudo dpkg -i vora-*.deb` |
| Linux | `.rpm` | `sudo rpm -i vora-*.rpm` |
| Linux | `.pkg.tar.xz` | `sudo pacman -U vora-*.pkg.tar.xz` |
| macOS | `.tar.gz` | Extract to `/usr/local/bin` |

### Build from Source

**Windows (PowerShell):**

```powershell
# Default: x64 Debug
.\build.ps1

# Custom: ARM64 Release with packaging
.\build.ps1 -Architecture arm64 -Config Release -Package   # → .msi installer
.\build.ps1 -Architecture x86 -Config Debug               # → 32-bit .exe
```

**Linux / macOS (bash):**

```bash
# Default: x64 Debug
./build.sh

# Custom: ARM64 Release with packaging
./build.sh -a arm64 -c release -p    # → .deb/.rpm/.pkg.tar.xz
./build.sh -a x86 -c debug           # → 32-bit binary
```

### Run

```bash
vora examples/main.va                 # Execute script (VM mode)
vora --repl                           # Interactive REPL
vora examples/main.va --ast-printer   # Print AST as S-expressions
vora examples/main.va --tokens        # Print tokens / bytecode disassembly
```

---

## Language Features

### Types

```vora
let n = 42              // Number (int64 or float64)
let f = 3.14
let b = true            // Boolean
let s = "hello"         // String
let a = [1, 2, 3]       // Array
let nothing = null
```

**Number literals** support multiple bases:

```vora
0xFF      // 255  (hexadecimal)
0o77      // 63   (octal)
0b10101   // 21   (binary)
```

### Operators

| Category | Operators |
|----------|-----------|
| Arithmetic | `+` `-` `*` `/` `%` `**` |
| Comparison | `<` `<=` `>` `>=` `==` `!=` |
| Logical | `&&` `||` `!` (short-circuit) |
| Ternary | `cond ? a : b` |
| Increment/Decrement | `++` `--` |
| Compound Assignment | `+=` `-=` `*=` `/=` `%=` |

`+` also supports string concatenation and array merge/append.

### Variables & Scope

```vora
let x = 10              // Declaration
x = 20                  // Assignment (lexical scope chain lookup)
{ let x = 30 }          // Block scope shadowing
```

### Control Flow

```vora
// if / else
if (x > 0) {
    print("positive")
} else {
    print("non-positive")
}

// while
while (n > 0) {
    n = n - 1
}

// for-in (arrays, strings, ranges)
for item in [1, 2, 3] { print(item) }
for ch in "Vora"       { print(ch) }
for i in range(5)      { print(i) }   // 0..4
```

`break` and `continue` work as expected.

### Functions & Closures

```vora
func add(a, b) {
    return a + b
}

// Default parameters
func greet(name, greeting = "Hello") {
    return greeting + ", " + name
}
greet("World")        // → "Hello, World"
greet("Vora", "Hi")   // → "Hi, Vora"

// Closures
func makeCounter() {
    let count = 0
    return func() { count = count + 1; return count }
}

let c = makeCounter()
print(c())  // 1
print(c())  // 2
```

### Objects & Inheritance

```vora
// Single inheritance
Obj Animal(name) {
    this.name = name
    func speak() { print("...") }
}

Obj Dog : Animal (name, breed) {
    this.breed = breed
    func speak() { print("Woof! I'm " + this.name) }
}

let d = Dog("Rex", "Husky")
d.speak()       // "Woof! I'm Rex"
print(d.name)   // "Rex"

// Multiple inheritance with C3 linearization
Obj Speaker() { func speak() { return "hello" } }
Obj Walker()  { func walk()  { return "walking" } }

Obj Robot : Speaker, Walker () {    // MRO: Robot, Speaker, Walker
    func work() { return "working" }
}

let r = Robot()
r.speak()  // → "hello"  (first parent wins on conflicts)
r.walk()   // → "walking"

// super keyword
Obj Puppy : Dog (name, breed, toy) {
    this.toy = toy
    func speak() { return super.speak() + " yip!" }
}
let p = Puppy("Max", "Lab", "ball")
p.speak()  // → "... woof! yip!"
```

### Exception Handling

```vora
try {
    throw "something went wrong"
} catch (e) {
    print("Caught: " + e)
} finally {
    print("Cleanup")
}
```

### String Interpolation

```vora
let name = "World"
print("Hello ${name}!")   // "Hello World!"
```

---

## Built-in Functions

| Function | Description |
|----------|-------------|
| `print(...)` | Variable-argument output, space-separated |
| `clock()` | Unix timestamp in seconds |
| `input(prompt?)` | Read a line from stdin |
| `int(value)` | Convert to int64 (truncate) |
| `float(value)` | Convert to float64 |
| `len(value)` | Array length or string character count |
| `type(value)` | Type name: `"int"` `"float"` `"string"` `"array"` `"boolean"` `"null"` `"function"` `"object"` |
| `range(stop)` / `range(start, stop, step?)` | Generate number array |
| `assert(cond, msg?)` | Assertion with optional message |
| `bin(num)` | String like `"0b101"` |
| `oct(num)` | String like `"0o77"` |
| `hex(num)` | String like `"0xff"` |

---

## Array Methods

| Method | Description |
|--------|-------------|
| `.length` | Number of elements |
| `.add(item)` | Append element |
| `.pop()` | Remove and return last element |
| `.insert(idx, item)` | Insert at index |
| `.remove(idx)` | Remove at index |
| `.indexOf(item)` | Return index, or -1 |
| `.clear()` | Remove all elements |

---

## String Methods

| Method | Description |
|--------|-------------|
| `.length` | Character count |
| `.substring(start, end?)` | Slice by character index |
| `.include(sub)` | Check if substring exists |
| `.startsWith(prefix)` | Check prefix |
| `.endsWith(suffix)` | Check suffix |
| `.upper()` | Uppercase |
| `.lower()` | Lowercase |
| `.trim()` | Strip whitespace |
| `.replace(old, new)` | Replace first occurrence |
| `.replaceAll(old, new)` | Replace all occurrences |
| `.split(delim)` | Split into array |

---

## Project Architecture

```
.vasource  →  Lexer  →  Token stream  →  Parser (Pratt)  →  AST (Program)
                                                          └→ Compiler → Chunk (bytecode) → VM
```

**Execution pipeline** in `src/`:

| Module | Lines | Description |
|--------|-------|-------------|
| `lexer/` | ~687 | Hand-written scanner, 21 keywords, O(1) lookup, nested block comments, Unicode, 0x/0o/0b |
| `parser/` | ~1,185 | Pratt (precedence climbing), 7-level precedence table, panic-mode error recovery |
| `ast/` | ~1,629 | 27 node types (14 expressions + 13 statements), templated Visitor pattern |
| `vm/` | ~3,175 | Bytecode compiler + stack-based VM, 50 opcodes, constant folding, fast numeric ops |
| `runtime/` | ~1,200 | `Value` (std::variant), `Environment` (lexical scope chain), `Callable` abstraction, `builtins` module |

**Design highlights:**

- **Templated Visitor** — A single generic `ExprVisitor<R>` / `StmtVisitor<R>` interface serves multiple backends: Compiler (`R=void`), ASTPrinter (`R=string`).
- **Zero external dependencies** — Pure C++17 standard library. No Boost, no LLVM, no third-party code.
- **Enterprise build system** — CMakePresets with 20 presets, 6 cross-compilation toolchains, 18-matrix CI, native packaging for Windows (.msi), Linux (.deb/.rpm), macOS (.tar.gz).

---

## Platform Support

| Platform | Architectures |
|----------|---------------|
| Windows | x64, x86 (Win32), ARM64 |
| Linux | x64, x86 (i386), aarch64, armhf |
| macOS | Universal (x86_64 + arm64) |

All platforms support cross-compilation. CI (GitHub Actions) automates builds across all 18 os x arch x config combinations.

---

## Testing

```powershell
# Windows (PowerShell)
.\tests\run_tests.ps1                   # VM mode
```

```bash
# Linux / macOS
./tests/run_tests.sh                    # VM mode
```

**Current status:** 16/16 VM tests pass + 16/16 Interpreter tests pass + 24/24 examples pass. C++ unit tests: 6 modules (~100+ cases).

---

## Roadmap

| Version | Status | Milestone |
|---------|--------|-----------|
| v0.01 ~ v0.12 | **Done** | Literals, variables, scope, functions, closures, objects, inheritance, try/catch/finally, bytecode VM, performance optimization, multi-platform builds, int64/float64, `len()`, `type()`, 17 built-in methods |
| v0.13 | **Done** | C++ unit test system (doctest), builtins module centralization, P0/P1 correctness fixes |
| v0.14 | **Done** | `weak_ptr` cycle breaking, runtime error call stack traces |
| v0.15 | **Done** | Default parameters, multi-inheritance C3 linearization, `super` keyword |
| v0.16 | Planned | Module system (`import` / `export`), standard library |
| v0.2 | Planned | Further optimization / JIT compilation |

---

## Documentation

| Document | Content |
|----------|---------|
| [`docs/01-技术栈概览.md`](docs/01-技术栈概览.md) | Tech stack, architecture, build commands |
| [`docs/02-项目结构.md`](docs/02-项目结构.md) | Directory tree, file descriptions |
| [`docs/03-词法分析器开发文档.md`](docs/03-词法分析器开发文档.md) | Lexer design |
| [`docs/04-AST开发文档.md`](docs/04-AST开发文档.md) | AST node design + Visitor pattern |
| [`docs/05-语法分析器开发文档.md`](docs/05-语法分析器开发文档.md) | Pratt Parser design |
| [`docs/06-解释器开发文档.md`](docs/06-解释器开发文档.md) | Tree-walking Interpreter design |
| [`docs/07-运行时系统开发文档.md`](docs/07-运行时系统开发文档.md) | Value / Environment / Callable |
| [`docs/08-已实现功能总结.md`](docs/08-已实现功能总结.md) | Feature summary + version history |
| [`docs/09-构建系统指南.md`](docs/09-构建系统指南.md) | Multi-architecture builds, cross-compilation, packaging |
| [`docs/10-深度分析与UX路线图.md`](docs/10-深度分析与UX路线图.md) | In-depth analysis + UX roadmap |

---

## Acknowledgments

Vora draws inspiration from:

- [Crafting Interpreters](https://craftinginterpreters.com/) by Robert Nystrom — foundational interpreter design
- [Lua 5.4](https://www.lua.org/source/5.4/) — embeddable language philosophy and simplicity
- [Wren](https://wren.io/) — clean object orientation and bytecode VM design
- [CPython](https://github.com/python/cpython) — reference implementation patterns

---

## License

MIT License — Copyright (c) 2026 Vora-lang

See [LICENSE](LICENSE) for details.
