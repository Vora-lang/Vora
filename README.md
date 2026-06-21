<div align="center">

# <img src="res/vora.ico" width="40"> Vora

**A dynamically typed scripting language with JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation.**

**JavaScript 式语法 · Lua 级简洁 · Wren 风格面向对象 — 动态类型脚本语言**

[![CI](https://github.com/Vora-lang/Vora/actions/workflows/ci.yml/badge.svg)](https://github.com/Vora-lang/Vora/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Version](https://img.shields.io/badge/version-0.24-orange.svg)](#changelog)

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

~17,600 lines of C++17. Zero external dependencies. **Bytecode compiler + stack-based VM** as the sole execution backend. Cross-platform with native packaging.

~17,600 行 C++17，零外部依赖。**字节码编译器 + 栈式虚拟机**为唯一执行后端。跨平台，原生打包。

---

## Table of Contents / 目录

- [Why Vora? / 为何选择 Vora](#why-vora--为何选择-vora)
- [Quick Start / 快速开始](#quick-start--快速开始)
- [Language Features / 语言特性](#language-features--语言特性)
  - [Types / 类型](#types--类型)
  - [Operators / 运算符](#operators--运算符)
  - [Variables & Scope / 变量与作用域](#variables--scope--变量与作用域)
  - [Control Flow / 控制流](#control-flow--控制流)
  - [Functions & Closures / 函数与闭包](#functions--closures--函数与闭包)
  - [Objects & Inheritance / 对象与继承](#objects--inheritance--对象与继承)
  - [Dict / 字典](#dict--字典)
  - [Set & Map / 集合与映射表](#set--map--集合与映射表-v023)
  - [Exception Handling / 异常处理](#exception-handling--异常处理)
  - [String Interpolation / 字符串插值](#string-interpolation--字符串插值)
  - [Pattern Matching / 模式匹配](#pattern-matching--模式匹配-v024)
- [Built-in Functions / 内建函数](#built-in-functions--内建函数)
- [String, Array, Dict, Set & Map Methods / 方法参考](#string-array-dict-set--map-methods--方法参考)
- [Project Architecture / 项目架构](#project-architecture--项目架构)
- [Platform Support / 平台支持](#platform-support--平台支持)
- [Testing / 测试](#testing--测试)
- [Roadmap / 路线图](#roadmap--路线图)
- [Documentation / 文档](#documentation--文档)
- [Acknowledgments / 致谢](#acknowledgments--致谢)
- [License / 许可证](#license--许可证)

---

## Why Vora? / 为何选择 Vora

| | JavaScript | Lua | Wren | **Vora** |
|---|:---:|:---:|:---:|:---:|
| C++ embeddable / 可嵌入 C++ | | | ✔ | **✔** |
| Object-oriented / 面向对象 | ✔ | ✔ | ✔ | **✔** |
| Try/catch/finally | ✔ | | | **✔** |
| Closures / 闭包 | ✔ | ✔ | ✔ | **✔** |
| Multiple inheritance / 多继承 | | ✔ | | **✔** |
| TCO / 尾调用优化 | | ✔ | | **✔** |
| const bindings / 不可变绑定 | ✔ | | | **✔** |
| Zero dependencies / 零依赖 | | ✔ | ✔ | **✔** |
| Native cross-platform builds / 原生跨平台构建 | | | | **✔** |
| Dict / Set / Map built-in / 内建数据结构 | ✔ | | | **✔** |
| Pattern matching / 模式匹配 | | | | **✔** |

Vora occupies a practical middle ground: readable syntax for scripting, fast execution via bytecode VM, and a tiny C++ runtime that embeds into any host application.

Vora 定位于实用中间地带：脚本级可读语法、字节码 VM 高效执行、微型 C++ 运行时可嵌入任何宿主应用。

---

## Quick Start / 快速开始

### Install from Release / 从发布包安装

| Platform / 平台 | Format / 格式 | Install / 安装 |
|----------|--------|---------|
| Windows | `.msi` | 双击安装（自动注册 PATH） |
| Linux | `.deb` | `sudo dpkg -i vora-*.deb` |
| Linux | `.rpm` | `sudo rpm -i vora-*.rpm` |
| Linux | `.pkg.tar.xz` | `sudo pacman -U vora-*.pkg.tar.xz` |
| macOS | `.tar.gz` | 解压至 `/usr/local/bin` |

### Build from Source / 从源码构建

**Windows (PowerShell):**

```powershell
# 默认：x64 Debug
.\build.ps1

# 自定义：ARM64 Release + 打包
.\build.ps1 -Architecture arm64 -Config Release -Package   # → .msi 安装包
.\build.ps1 -Architecture x86 -Config Debug               # → 32 位 .exe
```

**Linux / macOS (bash):**

```bash
# 默认：x64 Debug
./build.sh

# 自定义：ARM64 Release + 打包
./build.sh -a arm64 -c release -p    # → .deb/.rpm/.pkg.tar.xz
./build.sh -a x86 -c debug           # → 32 位二进制
```

### Run / 运行

```bash
vora examples/main.va                 # 执行脚本
vora --repl                           # 交互式 REPL
vora examples/main.va --ast-printer   # 打印 AST（S 表达式）
vora examples/main.va --tokens        # 打印 Token / 字节码反汇编
vora fmt examples/main.va             # 格式化输出到 stdout
vora fmt -w examples/main.va          # 原地格式化文件
```

---

## Language Features / 语言特性

### Types / 类型

```vora
let n = 42              // Number (int64 或 float64)
let f = 3.14
let b = true            // Boolean / 布尔
let s = "hello"         // String / 字符串
let a = [1, 2, 3]       // Array / 数组
let d = {name: "Vora"}  // Dict / 字典
let nothing = null
```

**Number literals / 数字字面量** 支持多种进制:

```vora
0xFF      // 255  (hexadecimal / 十六进制)
0o77      // 63   (octal / 八进制)
0b10101   // 21   (binary / 二进制)
```

### Operators / 运算符

| Category / 分类 | Operators / 运算符 |
|----------|-----------|
| Arithmetic / 算术 | `+` `-` `*` `/` `%` `**` |
| Comparison / 比较 | `<` `<=` `>` `>=` `==` `!=` |
| Logical / 逻辑 | `&&` `||` `!` (short-circuit / 短路求值) |
| Ternary / 三元 | `cond ? a : b` |
| Increment/Decrement / 自增自减 | `++` `--` |
| Compound Assignment / 复合赋值 | `+=` `-=` `*=` `/=` `%=` |

`+` also supports string concatenation, array merge, and dict merge. / `+` 也支持字符串拼接、数组合并、字典合并。

### Variables & Scope / 变量与作用域

```vora
let x = 10              // Mutable declaration / 可变声明
const y = 42            // Immutable binding / 不可变绑定（编译期保护）
x = 20                  // Assignment / 赋值（沿词法作用域链查找）
// y = 10               // ❌ Compile error / 编译错误: Cannot assign to const
{ let x = 30 }          // Block scope shadowing / 块作用域遮蔽
```

- `const` reassignment protection covers locals, globals, and closure-captured upvalues (compile-time checks on `=`, `+=`, `++`, `--`).
- `const` 赋值保护覆盖局部变量、全局变量、闭包上值捕获变量（编译期检查 `=`、`+=`、`++`、`--`）。

### Control Flow / 控制流

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

// C-style for loop / C 风格 for 循环
for (let i = 0; i < 5; i = i + 1) {
    print(i)                      // 0, 1, 2, 3, 4
}

// 初始化可省略；条件和增量也可省略
let k = 0
for (; k < 3; k = k + 1) { print(k) }

// for-in (arrays, strings, ranges, dicts, objects / 数组、字符串、range、字典、对象)
for item in [1, 2, 3] { print(item) }
for ch in "Vora"       { print(ch) }
for i in range(5)      { print(i) }   // 0..4
for k in {a: 1, b: 2} { print(k) }   // keys "a", "b"
for key in obj         { print(key) } // 对象属性键
```

`break` and `continue` work in all loop types. `break;` / `continue;` (semicolon optional). / `break` 和 `continue` 在所有循环类型中均可用，分号可选。

### Functions & Closures / 函数与闭包

```vora
func add(a, b) {
    return a + b
}

// void return / 无值返回
func noReturn() {
    return           // equivalent to / 等价于 return null
}

// Default parameters / 默认参数
func greet(name, greeting = "Hello") {
    return greeting + ", " + name
}
greet("World")        // → "Hello, World"
greet("Vora", "Hi")   // → "Hi, Vora"

// Anonymous functions (lambda) / 匿名函数（Lambda）
let square = func(x) { return x * x }
square(7)  // → 49

// 作为回调
func apply(f, a, b) { return f(a, b) }
apply(func(x, y) { return x + y }, 3, 4)  // → 7

// IIFE / 立即调用
let result = func(a, b) { return a * b }(6, 7)  // → 42

// Closures with shared upvalue indirection / 闭包（共享 Upvalue 间接引用）
func makeCounter() {
    let count = 0
    return func() { count = count + 1; return count }
}

let c = makeCounter()
print(c())  // 1
print(c())  // 2

// Local function self-recursion / 局部函数自递归
func factorial(n) {
    func fact(n) {
        if (n <= 1) { return 1 }
        return n * fact(n - 1)    // self-reference via upvalue / 通过上值自引用
    }
    return fact(n)
}
print(factorial(5))  // → 120

// Tail Call Optimization / 尾调用优化（TCO）
func tcoFact(n, acc) {
    if (n <= 1) { return acc }
    return tcoFact(n - 1, n * acc)   // tail call → OP_TAIL_CALL / 尾调用
}
tcoFact(10000, 1)  // ✅ 不栈溢出 / no stack overflow
```

- `return` without value is equivalent to `return null`. / 无值 `return` 等价于 `return null`。
- Lambda syntax: `func(params) { body }` — supports closures, default params, and IIFE patterns. / Lambda 语法：`func(params) { body }` — 支持闭包、默认参数、IIFE。
- TCO: `return f(args)` reuses the current frame when not inside try/finally. Non-Vora functions gracefully fall back to regular calls. / TCO：当不在 try/finally 内时，`return f(args)` 复用当前帧。非 Vora 函数优雅降级为常规调用。

### Objects & Inheritance / 对象与继承

```vora
// Single inheritance / 单继承
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

// Multiple inheritance with C3 linearization / 多继承 + C3 线性化
Obj Speaker() { func speak() { return "hello" } }
Obj Walker()  { func walk()  { return "walking" } }

Obj Robot : Speaker, Walker () {    // MRO: Robot, Speaker, Walker
    func work() { return "working" }
}

let r = Robot()
r.speak()  // → "hello"  (first parent wins on conflicts / 冲突时第一父类优先)
r.walk()   // → "walking"

// super keyword / super 关键字
Obj Puppy : Dog (name, breed, toy) {
    this.toy = toy
    func speak() { return super.speak() + " yip!" }
}
let p = Puppy("Max", "Lab", "ball")
p.speak()  // → "... woof! yip!"
```

### Dict / 字典

```vora
let d = {name: "Vora", version: 1}

// Subscript access / 下标读写
print(d["name"])      // → "Vora"
d["year"] = 2026

// Dict + Dict merge (right side wins on conflicts / 右侧覆盖同名键)
let a = {x: 1, y: 2}
let b = {y: 99, z: 3}
let c = a + b         // → {x: 1, y: 99, z: 3}

// Built-in methods / 内建方法
let keys = d.keys()   // → ["name", "version"]
d.has("name")         // → true
d.remove("year")      // → 2026

// for-in iteration / for-in 遍历
for k in {a: 1, b: 2} { print(k) }  // "a", "b"
```

### Set & Map / 集合与映射表 (v0.23)

```vora
// Set — deduplication, O(1) membership / 去重、成员检测
let s = Set([1, 2, 3])
s.add(4)
s.has(2)          // → true
s.size            // → 3
s.values()        // → list of elements / 元素列表

let a = Set([1, 2])
let b = Set([2, 3])
let u = a + b     // → Set([1, 2, 3]) — union / 并集
let chars = Set("hello")  // → Set(['h', 'e', 'l', 'o'])

// Map — arbitrary key types, index syntax / 任意类型 key + 索引语法
let m = Map()
m.set("name", "Vora")
m.set(123, "integer key")
m["year"] = 2026     // index syntax / 索引语法
m["name"]            // → "Vora"
m.has("name")        // → true
m.keys()             // → list of keys / 键列表
m.values()           // → list of values / 值列表

// Map + Map merge (right side wins / 右侧覆盖)
let merged = mapA + mapB

// for-in: Set iterates elements, Map iterates keys
for item in s     { print(item) }
for key in m      { print(key, m[key]) }
```

### Exception Handling / 异常处理

```vora
try {
    throw "something went wrong"
} catch (e) {
    print("Caught: " + e)    // 捕获: "something went wrong"
} finally {
    print("Cleanup")         // finally 始终执行
}
```

- `throw <expr>` can throw any Vora value (strings, objects, numbers, etc.). / `throw <expr>` 可抛出任意 Vora 值。
- `try/catch`, `try/finally`, `try/catch/finally` — all three combinations supported. / 三种组合均支持。
- Runtime errors include stack trace with file/line/column/function name. / 运行时错误含调用栈回溯。

### String Interpolation / 字符串插值

```vora
let name = "World"
print("Hello ${name}!")   // "Hello World!"
```

### Pattern Matching / 模式匹配 (v0.24)

```vora
// match as expression / match 作为表达式
let result = match n {
    1 => "one",
    2 => "two",
    _ => "other"
}

// Multiple patterns, range inclusive / 多模式、含等范围
let grade = match score {
    90..=100 => "A",
    80..=89  => "B",
    70..=79  => "C",
    _ => "F"
}

// Range exclusive / 不含等范围
let r = match x { 1..5 => "low", _ => "high" }

// Block body / 块体（多行逻辑）
let msg = match val {
    42 => {
        let doubled = val * 2
        doubled
    },
    _ => -1
}

// Nested match, boolean/null patterns, string patterns, standalone statement
let nested = match a {
    1 => match b { 2 => "ok", _ => "no" },
    _ => "nope"
}
match flag { 1 => { doSomething(); }, _ => {} }
```

---

## Built-in Functions / 内建函数

| Function / 函数 | Description / 说明 |
|----------|-------------|
| `print(...)` | Variable-argument output, space-separated / 变参输出，空格分隔 |
| `clock()` | Unix timestamp in seconds / Unix 时间戳（秒） |
| `input(prompt?)` | Read a line from stdin / 从标准输入读取一行 |
| `int(value)` | Convert to int64 (truncate) / 转换为 int64（截断小数） |
| `float(value)` | Convert to float64 / 转换为 float64 |
| `len(value)` | Array/string/dict/set/map length (int64) / 数组/字符串/字典/集合/映射表长度 |
| `type(value)` | Type name: `"int"` `"float"` `"string"` `"array"` `"dict"` `"set"` `"map"` `"boolean"` `"null"` `"function"` `"object"` |
| `range(stop)` / `range(start, stop, step?)` | Generate number array / 生成数字数组 |
| `assert(cond, msg?)` | Assertion with optional message / 断言，可选消息 |
| `bin(num)` / `oct(num)` / `hex(num)` | Number → `"0b..."` / `"0o..."` / `"0x..."` |
| `Set(iterable?)` | Create a Set (hash set) from optional iterable / 创建集合 |
| `Map()` | Create a Map (hash map) / 创建映射表 |
| `iter(value)` | Create an iterator from an iterable / 从可迭代对象创建迭代器 |
| `next(iterator)` | Advance iterator, return next value / 推进迭代器，返回下一个值 |

---

## String, Array, Dict, Set & Map Methods / 方法参考

### String / 字符串

| Method / 方法 | Description / 说明 |
|--------|-------------|
| `.length` | Character count / 字符数 |
| `.substring(start, end?)` | Slice by character index / 按索引切片 |
| `.include(sub)` | Check if substring exists / 检查子串是否存在 |
| `.startsWith(prefix)` / `.endsWith(suffix)` | Prefix/suffix check / 前缀/后缀检查 |
| `.upper()` / `.lower()` | Case conversion / 大小写转换 |
| `.trim()` | Strip whitespace / 去除两端空白 |
| `.replace(old, new)` | Replace first occurrence / 替换首次出现 |
| `.replaceAll(old, new)` | Replace all occurrences / 替换全部出现 |
| `.split(delim)` | Split into array / 分割为数组 |

### Array / 数组

| Method / 方法 | Description / 说明 |
|--------|-------------|
| `.length` | Number of elements / 元素个数 |
| `.add(item)` | Append element / 追加元素 |
| `.pop()` | Remove and return last / 弹出并返回末尾元素 |
| `.insert(idx, item)` | Insert at index / 在指定位置插入 |
| `.remove(idx)` | Remove at index / 删除指定位置元素 |
| `.indexOf(item)` | Return index or -1 / 返回索引或 -1 |
| `.clear()` | Remove all elements / 清空全部元素 |

### Dict / 字典

| Method / 方法 | Description / 说明 |
|--------|-------------|
| `.keys()` | Return array of keys / 返回键数组 |
| `.values()` | Return array of values / 返回值数组 |
| `.has(key)` | Check if key exists / 检查键是否存在 |
| `.remove(key)` | Remove and return value / 删除并返回对应值 |

### Set / 集合

| Method / 方法 | Description / 说明 |
|--------|-------------|
| `.size` | Element count / 元素个数 |
| `.add(item)` | Add element / 添加元素 |
| `.has(item)` | Check if element exists / 检查元素是否存在 |
| `.remove(item)` | Remove element / 删除元素 |
| `.clear()` | Remove all elements / 清空全部元素 |
| `.values()` | Return array of all elements / 返回全部元素的数组 |

### Map / 映射表

| Method / 方法 | Description / 说明 |
|--------|-------------|
| `.size` | Key-value pair count / 键值对数量 |
| `.set(key, value)` | Set key-value pair / 设置键值对 |
| `.get(key)` | Get value by key / 按键取值 |
| `.has(key)` | Check if key exists / 检查键是否存在 |
| `.remove(key)` | Remove and return value / 删除并返回对应值 |
| `.clear()` | Remove all entries / 清空全部条目 |
| `.keys()` | Return array of keys / 返回键数组 |
| `.values()` | Return array of values / 返回值数组 |

---

## Project Architecture / 项目架构

```
.va source  →  Lexer  →  Token stream  →  Parser (Pratt)  →  AST (Program)
                                                              └→ Compiler → Chunk (bytecode) → VM
```

**Execution pipeline** in `src/` / **执行管线**（`src/` 目录下）:

| Module / 模块 | Lines / 行数 | Description / 说明 |
|--------|-------|-------------|
| `lexer/` | ~600 | Hand-written scanner, 23 keywords, O(1) lookup, nested block comments, Unicode, 0x/0o/0b / 手写扫描器，23 个关键字 |
| `parser/` | ~1,030 | Pratt (precedence climbing), 7-level precedence table, panic-mode error recovery / Pratt 解析器，7 级优先级表 |
| `ast/` | ~1,450 | 30 node types (16 exprs + 14 stmts), templated Visitor pattern / 30 种节点类型，模板化 Visitor 模式 |
| `vm/` | ~4,200 | Bytecode compiler + stack-based VM, 58 opcodes (incl. OP_TAIL_CALL), constant folding, fast numeric ops / 字节码编译器 + 栈式 VM，58 条操作码 |
| `runtime/` | ~1,200 | `Value` (std::variant, 11 types), `Environment` (lexical scope chain), `Callable` hierarchy, `builtins` module, `Upvalue` (index-based) |
| `gc/` | ~300 | Mark-sweep garbage collector, `GcPtr<T>`, `GcHeap` singleton / 标记-清除 GC |
| `formatter/` | ~630 | AST-based source code formatter (`vora fmt`) / AST 驱动的代码格式化器 |

**Design highlights / 设计亮点:**

- **Templated Visitor / 模板化 Visitor** — A single generic `ExprVisitor<R>` / `StmtVisitor<R>` interface serves multiple backends: Compiler (`R=void`), ASTPrinter (`R=string`). / 单一泛型接口服务多个后端。
- **Zero external dependencies / 零外部依赖** — Pure C++17 standard library. No Boost, no LLVM, no third-party code. / 纯 C++17 标准库。
- **Mark-sweep GC / 标记-清除 GC** — `GcHeap` singleton with `GcPtr<T>` non-owning pointers. Automatic tracing and collection of heap objects. / `GcHeap` 单例 + `GcPtr<T>` 非拥有指针，自动追踪回收堆对象。
- **Index-based Upvalues / 基于索引的 Upvalue** — Upvalue stores `std::vector<Value>*` + `size_t slotIndex` instead of raw `Value*`. Immune to stack vector reallocation. / Upvalue 存储栈向量指针 + 槽位索引，免疫栈向量重分配。
- **Tail Call Optimization / 尾调用优化** — `OP_TAIL_CALL` reuses the current call frame, enabling infinite tail recursion. Graceful fallback for non-Vora callables. / `OP_TAIL_CALL` 复用当前调用帧，支持无限尾递归。
- **Enterprise build system / 企业级构建系统** — CMakePresets with 20 presets, 6 cross-compilation toolchains, 18-matrix CI, native packaging (`.msi`, `.deb`, `.rpm`, `.tar.gz`).

---

## Platform Support / 平台支持

| Platform / 平台 | Architectures / 架构 |
|----------|---------------|
| Windows | x64, x86 (Win32), ARM64 |
| Linux | x64, x86 (i386), aarch64, armhf |
| macOS | Universal (x86_64 + arm64) |

All platforms support cross-compilation. CI (GitHub Actions) automates builds across all 18 os × arch × config combinations. / 全部平台支持交叉编译。CI 自动化覆盖全部 18 种组合。

---

## Testing / 测试

```powershell
# Windows (PowerShell)
.\tests\run_tests.ps1
```

```bash
# Linux / macOS
./tests/run_tests.sh
```

**Current status / 当前状态:**

| Suite / 套件 | Count / 数量 | Status / 状态 |
|---------|--------|--------|
| C++ unit tests / C++ 单元测试 | 52 test cases, 330 assertions | ✅ 全通过 |
| Script tests / 脚本测试 | 124 files | ✅ 全通过 |
| Examples / 示例 | 44 files | ✅ 全通过 |
| LeetCode integration / LeetCode 集成 | 66 solutions | ✅ 全通过 |

---

## Roadmap / 路线图

| Version / 版本 | Status / 状态 | Milestone / 里程碑 |
|---------|--------|-----------|
| v0.01 ~ v0.12 | **Done / 已完成** | Literals, variables, scope, functions, closures, objects, inheritance, try/catch/finally, bytecode VM, performance optimization, multi-platform builds, int64/float64, built-in methods |
| v0.13 | **Done / 已完成** | C++ unit test system (doctest), builtins module centralization, P0/P1 correctness fixes |
| v0.14 | **Done / 已完成** | `weak_ptr` cycle breaking, runtime error call stack traces |
| v0.15 | **Done / 已完成** | Default parameters, multi-inheritance C3 linearization, `super` keyword |
| v0.16 | **Done / 已完成** | Dict type, for-in object iteration, VM stack dynamic resize, 16-bit constant pool index |
| v0.17 | **Done / 已完成** | Error messages with source line + caret indicator |
| v0.18 | **Done / 已完成** | Architecture refactor (Callable hierarchy split, unified ClassDefinition), mark-sweep GC, `vora fmt` formatter, LeetCode tests |
| v0.19 | **Done / 已完成** | C-style for loops, anonymous functions (lambdas), syntax fixes (void return, unary-minus precedence, break/continue semicolons), upvalue indirection (shared mutation + local recursion) |
| v0.20 | **Done / 已完成** | Tail Call Optimization (TCO), `const` immutable bindings (compile-time enforcement on locals/globals/upvalues), index-based Upvalue safety |
| v0.21 | **Done / 已完成** | Module system (`import`/`export`), iterator protocol (`iter`/`next`), `yield` generators, `StopIteration` exception |
| v0.22 | **Done / 已完成** | Rest parameters (`...name`), LSP server infrastructure, AST node position (nameToken), `json_rpc` refactor |
| v0.23 | **Done / 已完成** | Set (hash set) + Map (hash map) data structures, O(1) `has`/`add`/`remove`, union/merge via `+`, for-in iteration |
| v0.24 | **Done / 已完成** | `match` pattern matching — literal/wildcard/range patterns, `\|` multi-pattern, `=>` expression/block body, `..=`/`..` range syntax |
| v0.25+ | Planned / 规划中 | Standard library (`std/fs`, `std/os`), call-site spread (`...expr`), async/await coroutines, guard conditions, exhaustiveness checking, JIT compilation |

---

## Documentation / 文档

| Document / 文档 | Content / 内容 |
|----------|---------|
| [`docs/00-roadmap.md`](docs/00-roadmap.md) | Optimization roadmap + known defects & limitations / 优化路线图 + 已知缺陷与限制 |
| [`docs/01-技术栈概览.md`](docs/01-技术栈概览.md) | Tech stack, architecture, build commands / 技术栈、架构、构建命令 |
| [`docs/02-项目结构.md`](docs/02-项目结构.md) | Directory tree, file descriptions / 目录树、文件说明 |
| [`docs/03-词法分析器开发文档.md`](docs/03-词法分析器开发文档.md) | Lexer design / 词法分析器设计 |
| [`docs/04-AST开发文档.md`](docs/04-AST开发文档.md) | AST node design + Visitor pattern / AST 节点设计 + Visitor 模式 |
| [`docs/05-语法分析器开发文档.md`](docs/05-语法分析器开发文档.md) | Pratt Parser design / Pratt 解析器设计 |
| [`docs/06-解释器开发文档.md`](docs/06-解释器开发文档.md) | Tree-walking Interpreter design (historical) / 树遍历解释器设计（历史） |
| [`docs/07-运行时系统开发文档.md`](docs/07-运行时系统开发文档.md) | Value / Environment / Callable / GC / Upvalue |
| [`docs/08-已实现功能总结.md`](docs/08-已实现功能总结.md) | Feature summary + version history / 功能总结 + 版本历程 |
| [`docs/09-构建系统指南.md`](docs/09-构建系统指南.md) | Multi-architecture builds, cross-compilation, packaging / 多架构构建、交叉编译、打包 |
| [`docs/10-后续优化建议.md`](docs/10-后续优化建议.md) | Future optimization suggestions / 后续优化建议 |
| [`docs/11-深度分析与UX路线图.md`](docs/11-深度分析与UX路线图.md) | In-depth analysis + UX roadmap / 深度分析 + UX 路线图 |

---

## Acknowledgments / 致谢

Vora draws inspiration from / Vora 从以下项目汲取灵感:

- [Crafting Interpreters](https://craftinginterpreters.com/) by Robert Nystrom — foundational interpreter design / 解释器设计基础
- [Lua 5.4](https://www.lua.org/source/5.4/) — embeddable language philosophy and simplicity / 可嵌入语言哲学与简洁性
- [Wren](https://wren.io/) — clean object orientation and bytecode VM design / 清晰面向对象与字节码 VM 设计
- [CPython](https://github.com/python/cpython) — reference implementation patterns / 参考实现模式

---

## License / 许可证

MIT License — Copyright (c) 2026 Vora-lang

See [LICENSE](LICENSE) for details. / 详见 [LICENSE](LICENSE)。
