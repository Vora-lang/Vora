# Vora

Vora 是一门动态类型脚本语言，类 JavaScript 语法、Lua 级简单性、Wren 式面向对象。

当前为 **字节码 VM**（默认后端）+ **AST 树遍历解释器**（参考实现），~9200 行 C++17，零外部依赖。

```vora
func greet(name) {
    return "Hello, " + name + "!"
}

let names = ["World", "Vora", "你"]
for name in names {
    print(greet(name))
}
```

## 快速开始

```powershell
# 构建（Windows / CMake）
.\build.ps1

# 运行脚本
./build/Debug/Vora.exe examples/main.va

# REPL 模式
./build/Debug/Vora.exe --repl

# 打印 AST
./build/Debug/Vora.exe examples/main.va --ast-printer

# 打印 Token 流
./build/Debug/Vora.exe examples/main.va --tokens
```

## 语言概览

### 类型

```vora
let n = 42           // Number (double)
let f = 3.14
let b = true         // Boolean
let s = "hello"      // String
let a = [1, 2, 3]    // Array
let nothing = null
```

数字支持 `0x`/`0o`/`0b` 前缀：
```vora
0xFF    // 255（十六进制）
0o77    // 63（八进制）
0b101   // 5（二进制）
```

### 运算符

算术 `+` `-` `*` `/` `%` `**` · 比较 `<` `<=` `>` `>=` `==` `!=` · 逻辑 `&&` `||` `!`（短路求值）· 三元 `? :` · 自增 `++` `--` · 复合赋值 `+=` `-=` `*=` `/=` `%=`

`+` 同时支持字符串拼接、数组合并/追加。

### 变量与作用域

```vora
let x = 10           // 声明
x = 20               // 赋值（词法作用域链查找）
{ let x = 30 }       // 块作用域遮蔽
```

### 控制流

```vora
if (x > 0) {
    print("positive")
} else {
    print("non-positive")
}

while (n > 0) {
    n = n - 1
}

for item in [1, 2, 3] { print(item) }
for ch in "Vora"       { print(ch) }
for i in range(5)      { print(i) }  // 0..4
```

`break` / `continue` 有效。

### 函数与闭包

```vora
func add(a, b) {
    return a + b
}

func makeCounter() {
    let count = 0
    return func() { count = count + 1; return count }
}
let c = makeCounter()
print(c())  // 1
print(c())  // 2
```

### 对象（含继承）

```vora
Obj Animal(name) {
    this.name = name
    func speak() { print("...") }
}

Obj Dog : Animal (name, breed) {
    this.breed = breed
    func speak() { print("Woof! I'm " + this.name) }
}

let d = Dog("Rex", "Husky")
d.speak()      // "Woof! I'm Rex"
print(d.name)  // "Rex"
```

### 异常处理

```vora
try {
    throw "something went wrong"
} catch (e) {
    print("Caught: " + e)
} finally {
    print("Cleanup")
}
```

### 字符串插值

```vora
let name = "World"
print("Hello ${name}!")  // "Hello World!"
```

### 内建函数

| 函数 | 说明 |
|------|------|
| `print(...)` | 变参输出，空格分隔 |
| `clock()` | Unix 时间戳（秒） |
| `input(prompt?)` | 读取 stdin 一行 |
| `int(value)` | 转整数（截断） |
| `float(value)` | 转浮点数 |
| `range(stop)` / `range(start, stop, step)` | 生成数字数组 |
| `assert(cond, msg?)` | 断言 |
| `bin(num)` | → `"0b..."` |
| `oct(num)` | → `"0o..."` |
| `hex(num)` | → `"0x..."` |

## 项目结构

```
Vora/
├── src/
│   ├── lexer/       词法分析器（687 行）
│   ├── ast/          AST 节点 + Visitor 接口 + AST Printer（1258 行）
│   ├── parser/       Pratt 解析器（1200 行）
│   ├── interpreter/  树遍历解释器（1621 行）
│   ├── vm/           字节码 VM（编译器 + 栈式虚拟机，2974 行）
│   └── runtime/      值系统 / 作用域 / 可调用对象 / 错误（635 行）
├── examples/         24 个示例文件（按特性编号）
├── tests/            13 个测试文件 + 4 个基准测试 + 运行脚本
├── docs/             开发文档（10 篇，中文）
├── std/              标准库（规划中）
├── build.ps1         构建脚本
└── CMakeLists.txt
```

## 进度

| 版本 | 特性 | 状态 |
|------|------|------|
| v0.01 | 字面量 + 表达式 | ✅ |
| v0.02 | 变量 + 作用域 | ✅ |
| v0.03 | 函数 | ✅ |
| v0.04 | 控制流（if/while/for/break/continue） | ✅ |
| v0.05 | 对象 + 数组 + 继承 | ✅ |
| v0.06 | 闭包 | ✅ |
| v0.07 | 异常（try/catch/finally/throw） | ✅ |
| v0.08 | 字节码 VM（Phase 1-4：表达式/控制流/函数/闭包/对象/异常/插值） | ✅ |
| v0.09 | 全局变量驻留、快速数值操作码、常量折叠、移动语义 | ✅ |
| v0.1 | 模块系统（import/export） | ⏳ |
| v0.2 | 优化 / JIT | 规划中 |

## 测试

```powershell
# 运行全部测试
.\tests\run_tests.ps1

# 单个测试
./build/Debug/Vora.exe tests/interpreter/test_logical.va
```

## 文档

| 文档 | 内容 |
|------|------|
| `docs/01-技术栈概览.md` | 技术栈概览 |
| `docs/02-项目结构.md` | 项目结构 |
| `docs/03-词法分析器开发文档.md` | Lexer 设计 |
| `docs/04-AST开发文档.md` | AST 节点设计 |
| `docs/05-语法分析器开发文档.md` | Pratt Parser 设计 |
| `docs/06-解释器开发文档.md` | Interpreter 设计 |
| `docs/07-运行时系统开发文档.md` | Value / Environment / Callable |
| `docs/08-已实现功能总结.md` | 功能总结 |
| `docs/09-后续优化建议.md` | 优化路线图 |
| `docs/10-程序员视角-Vora语言深度分析与UX路线图.md` | 深度分析 + UX 路线图 |

## 参考

> [Crafting Interpreters](https://craftinginterpreters.com/)
>
> [Lua 5.4](https://www.lua.org/source/5.4/)
>
> [Wren](https://wren.io/)
>
> [CPython](https://github.com/python/cpython)
