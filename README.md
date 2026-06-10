# Vora

**Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation.**

~9400 行 C++17，零外部依赖。双后端：**字节码 VM**（默认）+ **AST 树遍历解释器**（参考实现）。跨平台，多架构，原生打包。

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

### 构建

```powershell
# Windows (PowerShell) — 默认 x64 Debug
.\build.ps1

# 指定架构 + 配置 + 打包
.\build.ps1 -Architecture arm64 -Config Release -Package   # → .msi 安装包
.\build.ps1 -Architecture x86 -Config Debug                 # → 32 位 .exe
```

```bash
# Linux / macOS (bash) — 默认 x64 debug
./build.sh

# 短标志 + 打包
./build.sh -a arm64 -c release -p     # 交叉编译 ARM64 → .deb/.rpm/.pkg.tar.xz
./build.sh -a x86 -c debug            # 32 位二进制
```

### 运行

```bash
# 执行脚本（VM 模式，默认）
vora examples/main.va

# 树遍历解释器
vora examples/main.va --interpreter

# REPL 交互模式
vora --repl

# 调试：打印 AST
vora examples/main.va --ast-printer

# 调试：打印 Token 流 / 字节码反汇编
vora examples/main.va --tokens
```

### 安装（Release 包）

| 平台 | 包格式 | 安装方式 |
|------|--------|----------|
| Windows | `.msi` | 双击安装（可选自定义目录，自动注册 PATH） |
| Linux | `.deb` | `sudo dpkg -i vora-*.deb` |
| Linux | `.rpm` | `sudo rpm -i vora-*.rpm` |
| Linux | `.pkg.tar.xz` | `sudo pacman -U vora-*.pkg.tar.xz` |
| macOS | `.tar.gz` | 解压到 `/usr/local/bin` |

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
| `int(value)` | 转 int64（截断） |
| `float(value)` | 转 float64 |
| `len(value)` | 返回数组长度或字符串字符数 |
| `type(value)` | 返回类型名：`"int"`/`"float"`/`"string"`/`"array"`/`"boolean"`/`"null"`/`"function"`/`"object"` |
| `range(stop)` / `range(start, stop, step)` | 生成数字数组 |
| `assert(cond, msg?)` | 断言 |
| `bin(num)` | → `"0b..."` |
| `oct(num)` | → `"0o..."` |
| `hex(num)` | → `"0x..."` |

数组方法：`.add(item)` `.pop()` `.length` `.insert(idx, item)` `.remove(idx)` `.indexOf(item)` `.clear()`
字符串方法：`.length` `.substring(start, end?)` `.include(sub)` `.startsWith(p)` `.endsWith(s)` `.upper()` `.lower()` `.trim()` `.replace(old, new)` `.replaceAll(old, new)` `.split(delim)`

## 项目结构

```
Vora/
├── CMakeLists.txt              # CMake 构建配置 + CPack 打包
├── CMakePresets.json           # CMake 预设（20 个，多架构 + 多配置）
├── build.ps1                   # Windows 一键构建脚本
├── build.sh                    # Linux/macOS 一键构建脚本
├── LICENSE                     # MIT License
├── vora_logo.svg               # 应用图标源文件
├── src/
│   ├── main.cpp                # 程序入口（~360 行）
│   ├── lexer/                  # 词法分析器（687 行）
│   ├── ast/                    # AST 节点 + Visitor 接口 + AST Printer（1629 行）
│   ├── parser/                 # Pratt 解析器（1200 行）
│   ├── interpreter/            # 树遍历解释器（1621 行）
│   ├── vm/                     # 字节码 VM：编译器 + 栈式虚拟机（3175 行）
│   └── runtime/                # 值系统 / 作用域 / 可调用对象 / 错误（719 行）
├── cmake/toolchains/           # 交叉编译工具链（6 个文件）
├── res/                        # 资源文件（图标、WiX 安装包、启动器）
├── .github/workflows/          # CI 多架构矩阵
├── examples/                   # 24 个示例文件（按特性编号）
├── tests/                      # 13 个测试文件 + run_tests.ps1 / run_tests.sh
├── docs/                       # 开发文档（10 篇，中文）
└── std/                        # 标准库（规划中）
```

## 支持的平台与架构

| 平台 | 架构 |
|------|------|
| Windows | x64, x86 (Win32), ARM64 |
| Linux | x64, x86 (i386), aarch64, armhf |
| macOS | universal (x86_64 + arm64) |

所有平台均支持交叉编译。CI（GitHub Actions）对全部 18 个 os×arch×config 组合进行自动化构建。

## 进度

| 版本 | 提交 | 特性 | 状态 |
|------|------|------|------|
| v0.01 | `d1cb460` | 字面量 + 表达式 | ✅ |
| v0.02 | `7cc3caf` | 变量 + 作用域 | ✅ |
| v0.03 | `2be35b9` | 函数 | ✅ |
| v0.04 | `5dc0cd8` | 控制流（if/while/for/break/continue） | ✅ |
| v0.05 | `807a07d` | 对象 + 数组 + 继承 | ✅ |
| v0.06 | `357fa7e` | 闭包 | ✅ |
| v0.07 | `bda534a` | 异常（try/catch/finally/throw） | ✅ |
| v0.08 | `0e8d810` | 字节码 VM（Phase 1-4：表达式/控制流/函数/闭包/对象/异常/插值） | ✅ |
| v0.09 | `19dbb27` | VM 性能优化（全局变量驻留、快速数值操作码、常量折叠、移动语义） | ✅ |
| v0.10 | `6666872` | VM 异常处理完善（finally 路由、异常重抛、catch handler 清理） | ✅ |
| v0.11 | `7a33678` | 多架构构建系统 + 应用图标 + 原生打包（.msi/.deb/.rpm/.pkg.tar.xz） | ✅ |
| v0.12 | `462052f` | int64/float64 双数值类型 + `len()` + `type()` + 17 个数组/字符串内建方法 | ✅ |
| v0.13 | — | 模块系统（import/export） | ⏳ |
| v0.2 | — | 优化 / JIT | 规划中 |

## 测试

```powershell
# Windows
.\tests\run_tests.ps1                   # VM 模式
.\tests\run_tests.ps1 -Interpreter      # 解释器模式
```

```bash
# Linux / macOS
./tests/run_tests.sh                    # VM 模式
./tests/run_tests.sh --interpreter      # 解释器模式
```

## 文档

| 文档 | 内容 |
|------|------|
| `docs/01-技术栈概览.md` | 技术栈、架构、构建命令 |
| `docs/02-项目结构.md` | 目录树、各文件说明 |
| `docs/03-词法分析器开发文档.md` | Lexer 设计 |
| `docs/04-AST开发文档.md` | AST 节点设计 + Visitor 模式 |
| `docs/05-语法分析器开发文档.md` | Pratt Parser 设计 |
| `docs/06-解释器开发文档.md` | 树遍历 Interpreter 设计 |
| `docs/07-运行时系统开发文档.md` | Value / Environment / Callable |
| `docs/08-已实现功能总结.md` | 功能总结 + 版本历程 |
| `docs/09-构建系统指南.md` | 多架构构建 + 交叉编译 + 打包 |
| `docs/10-后续优化建议.md` | 优化路线图 + 推荐优先级 |

## 参考

> [Crafting Interpreters](https://craftinginterpreters.com/)
>
> [Lua 5.4](https://www.lua.org/source/5.4/)
>
> [Wren](https://wren.io/)
>
> [CPython](https://github.com/python/cpython)
